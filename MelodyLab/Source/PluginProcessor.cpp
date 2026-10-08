// MELODY LAB — processor
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
static const char* style = "style";        static const char* melodyMode = "melodyMode";
static const char* harmonyMode = "harmonyMode"; static const char* keyMode = "keyMode";
static const char* chordRate = "chordRate"; static const char* bpmSource = "bpmSource";
static const char* bpm = "bpm";            static const char* offsetMs = "offsetMs";
static const char* density = "density";    static const char* complexity = "complexity";
static const char* octave = "octave";
static const char* vocalVol = "vocalVol";  static const char* melodyVol = "melodyVol";
static const char* chordsVol = "chordsVol"; static const char* bassVol = "bassVol";
static const char* melodyOn = "melodyOn";  static const char* chordsOn = "chordsOn"; static const char* bassOn = "bassOn";
}

static const char* kGenParams[] = { ids::style, ids::melodyMode, ids::harmonyMode, ids::keyMode, ids::chordRate,
                                    ids::bpmSource, ids::bpm, ids::offsetMs, ids::density, ids::complexity, ids::octave };

juce::StringArray MelodyLabProcessor::keyChoices()
{
    juce::StringArray k { "Auto" };
    for (int t = 0; t < 12; ++t)
    {
        k.add (ml::keyName (t, false));
        k.add (ml::keyName (t, true));
    }
    return k;
}

juce::AudioProcessorValueTreeState::ParameterLayout MelodyLabProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::style, 1 }, "Style",
                                                   StringArray { "Pop", "R&B", "Trap", "House", "Emotional", "Afrobeats" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::melodyMode, 1 }, "Melody Type",
                                                   StringArray { "Counter-melody", "Lead hook", "Vocal harmony", "Arp" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::harmonyMode, 1 }, "Chords",
                                                   StringArray { "Loop progression", "Follow vocal" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::keyMode, 1 }, "Key", keyChoices(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::chordRate, 1 }, "Chord Rate",
                                                   StringArray { "1 bar", "2 bars" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::bpmSource, 1 }, "Tempo Source",
                                                   StringArray { "Host tempo", "Manual" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bpm, 1 }, "BPM", NormalisableRange<float> (60.0f, 200.0f, 0.1f), 120.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::offsetMs, 1 }, "Grid Offset", NormalisableRange<float> (-2000.0f, 2000.0f, 1.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::density, 1 }, "Density", 0.0f, 1.0f, 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::complexity, 1 }, "Complexity", 0.0f, 1.0f, 0.5f));
    l.add (std::make_unique<AudioParameterInt>   (ParameterID { ids::octave, 1 }, "Octave", -1, 1, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::vocalVol, 1 }, "Vocal Vol", 0.0f, 1.0f, 0.8f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::melodyVol, 1 }, "Melody Vol", 0.0f, 1.0f, 0.7f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::chordsVol, 1 }, "Chords Vol", 0.0f, 1.0f, 0.6f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bassVol, 1 }, "Bass Vol", 0.0f, 1.0f, 0.7f));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::melodyOn, 1 }, "Melody On", true));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::chordsOn, 1 }, "Chords On", true));
    l.add (std::make_unique<AudioParameterBool>  (ParameterID { ids::bassOn, 1 }, "Bass On", true));
    return l;
}

MelodyLabProcessor::MelodyLabProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MelodyLab", createLayout())
{
    formats.registerBasicFormats();
    for (auto* id : kGenParams) apvts.addParameterListener (id, this);
    pVocalVol = apvts.getRawParameterValue (ids::vocalVol);
    pMelVol   = apvts.getRawParameterValue (ids::melodyVol);
    pChdVol   = apvts.getRawParameterValue (ids::chordsVol);
    pBassVol  = apvts.getRawParameterValue (ids::bassVol);
    pMelOn    = apvts.getRawParameterValue (ids::melodyOn);
    pChdOn    = apvts.getRawParameterValue (ids::chordsOn);
    pBassOn   = apvts.getRawParameterValue (ids::bassOn);
    events.reserve (4096);
    startTimerHz (15);
}

MelodyLabProcessor::~MelodyLabProcessor()
{
    *alive = false;
    stopTimer();
    for (auto* id : kGenParams) apvts.removeParameterListener (id, this);
    pool.removeAllJobs (true, 30000);
}

bool MelodyLabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MelodyLabProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    synth.prepare (sr);
    lastEndBeat = -1.0;
    wasPlaying = false;
}

double MelodyLabProcessor::effectiveBpm() const
{
    const bool manual = apvts.getRawParameterValue (ids::bpmSource)->load() > 0.5f;
    return manual ? (double) apvts.getRawParameterValue (ids::bpm)->load() : hostBpm.load();
}

//==============================================================================
void MelodyLabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    {
        const juce::SpinLock::ScopedTryLockType tl (dataLock);
        if (tl.isLocked() && pending != audioData) audioData = pending;
    }

    bool hostPlay = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) if (*b > 1.0) hostBpm = *b;
            hostPlay = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            else if (auto t = pos->getTimeInSeconds()) ppq = *t * hostBpm.load() / 60.0;
        }
    }
    hostPlaying = hostPlay;
    if (hostPlay) internalPlay = false;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    const PlaybackData* d = audioData.get();
    const bool playing = (hostPlay || internalPlay.load()) && d != nullptr;
    const std::array<float, 3> gains { pMelOn->load() > 0.5f ? pMelVol->load() : 0.0f,
                                       pChdOn->load() > 0.5f ? pChdVol->load() : 0.0f,
                                       pBassOn->load() > 0.5f ? pBassVol->load() : 0.0f };

    if (! playing)
    {
        if (wasPlaying) synth.allOff (false);
        wasPlaying = false;
        if (R != nullptr) synth.render (L, R, n, gains);
        else { synth.render (L, L, n, gains); buffer.applyGain (0.5f); }
        return;
    }

    const double bpm = std::max (20.0, effectiveBpm());
    const double bps = bpm / 60.0 / sampleRate;
    double beat0 = hostPlay ? ppq : internalBeat.load();
    if (! hostPlay && beat0 >= d->gen.totalBeats) beat0 = 0.0;           // internal transport loops
    const double beat1 = beat0 + n * bps;

    if (! wasPlaying || std::abs (beat0 - lastEndBeat) > 0.02) synth.allOff (false);

    // ---- schedule notes in this block ----------------------------------------
    events.clear();
    auto schedule = [&] (const std::vector<ml::Note>& v, int part)
    {
        auto it = std::lower_bound (v.begin(), v.end(), beat0, [] (const ml::Note& a, double b) { return a.start < b; });
        for (; it != v.end() && it->start < beat1; ++it)
            events.push_back ({ juce::jlimit (0, n - 1, (int) ((it->start - beat0) / bps)), part, it->pitch, it->velocity,
                                it->length * 60.0 / bpm * sampleRate });
    };
    schedule (d->gen.melody, PreviewSynth::Melody);
    schedule (d->gen.chords, PreviewSynth::Chords);
    schedule (d->gen.bass,   PreviewSynth::Bass);
    std::sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });

    float* Rr = R != nullptr ? R : L;
    int pos = 0;
    size_t e = 0;
    while (pos < n)
    {
        while (e < events.size() && events[e].offset <= pos)
        {
            synth.noteOn (events[e].part, events[e].pitch, events[e].vel, events[e].len);
            ++e;
        }
        const int next = e < events.size() ? events[e].offset : n;
        const int len = std::max (1, next - pos);
        synth.render (L + pos, Rr + pos, std::min (len, n - pos), gains);
        pos += len;
    }
    if (R == nullptr) buffer.applyGain (0.5f);

    // ---- acapella playback ------------------------------------------------------
    if (d->audio != nullptr)
    {
        const auto& a = *d->audio;
        const int alen = a.getNumSamples();
        const float vg = pVocalVol->load();
        const double secPerBeat = 60.0 / bpm;
        for (int i = 0; i < n && vg > 0.0f; ++i)
        {
            const double sec = (beat0 + i * bps) * secPerBeat + d->offsetSec;
            const double sp = sec * d->audioRate;
            const int idx = (int) std::floor (sp);
            if (idx < 0 || idx + 1 >= alen) continue;
            const float fr = (float) (sp - idx);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float* src = a.getReadPointer (juce::jmin (ch, a.getNumChannels() - 1));
                buffer.addSample (ch, i, vg * (src[idx] + fr * (src[idx + 1] - src[idx])));
            }
        }
    }

    lastEndBeat = beat1;
    wasPlaying = true;
    playheadBeat = beat0;
    if (! hostPlay) internalBeat = beat1;
}

//==============================================================================
void MelodyLabProcessor::togglePlay()
{
    if (hostPlaying.load()) return;
    internalPlay = ! internalPlay.load();
}

void MelodyLabProcessor::stopAndRewind()
{
    internalPlay = false;
    internalBeat = 0.0;
    playheadBeat = 0.0;
}

ml::GenSettings MelodyLabProcessor::currentSettings() const
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ml::GenSettings s;
    s.bpm = std::max (20.0, effectiveBpm());
    s.offsetSec = raw (ids::offsetMs) / 1000.0;
    const int km = (int) raw (ids::keyMode);
    if (km <= 0) { s.keyTonic = analysis.key.tonic; s.keyMinor = analysis.key.minor; }
    else         { s.keyTonic = (km - 1) / 2;       s.keyMinor = ((km - 1) % 2) == 1; }
    s.style        = (ml::Style) juce::jlimit (0, 5, (int) raw (ids::style));
    s.melodyMode   = (ml::MelodyMode) juce::jlimit (0, 3, (int) raw (ids::melodyMode));
    s.harmonyMode  = (ml::HarmonyMode) juce::jlimit (0, 1, (int) raw (ids::harmonyMode));
    s.barsPerChord = (int) raw (ids::chordRate) + 1;
    s.density      = raw (ids::density);
    s.complexity   = raw (ids::complexity);
    s.octave       = (int) std::lround (raw (ids::octave));
    s.harmonySeed  = harmonySeed;
    s.melodySeed   = melodySeed;
    return s;
}

void MelodyLabProcessor::regenerate()
{
    if (! analysis.valid) return;
    const auto s = currentSettings();
    lastBpmUsed = s.bpm;
    auto d = std::make_shared<PlaybackData>();
    d->audio = audio;
    d->audioRate = audioRate;
    d->offsetSec = s.offsetSec;
    d->bpm = s.bpm;
    d->keyName = ml::keyName (s.keyTonic, s.keyMinor);
    d->gen = ml::generate (analysis, s);
    publish (d);
}

void MelodyLabProcessor::publish (std::shared_ptr<const PlaybackData> d)
{
    if (published != nullptr) graveyard.push_back (published);
    published = d;
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        pending = d;
    }
    ++dataVersion;
}

void MelodyLabProcessor::parameterChanged (const juce::String&, float) { needsRegen = true; }

void MelodyLabProcessor::timerCallback()
{
    // free old data on the message thread, never on the audio thread
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const std::shared_ptr<const PlaybackData>& p) { return p.use_count() <= 1; }),
                     graveyard.end());

    const bool hostTempo = apvts.getRawParameterValue (ids::bpmSource)->load() < 0.5f;
    if (hostTempo && analysis.valid && std::abs (hostBpm.load() - lastBpmUsed) > 0.01)
        needsRegen = true;

    if (needsRegen.exchange (false))
        regenerate();
}

//==============================================================================
void MelodyLabProcessor::loadFile (const juce::File& file, uint32_t hSeed, uint32_t mSeed)
{
    if (loading.load() || ! file.existsAsFile()) return;
    loading = true;
    setStatus ("Analysing " + file.getFileName() + " ...");
    auto flag = alive;

    pool.addJob ([this, file, flag, hSeed, mSeed]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            juce::MessageManager::callAsync ([this, flag]
            {
                if (! *flag) return;
                loading = false;
                setStatus ("Couldn't read that file - try a WAV, AIFF, FLAC or MP3");
            });
            return;
        }

        const double rate = reader->sampleRate;
        const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (rate * 600.0));
        const int nch = (int) juce::jlimit (1u, 2u, reader->numChannels);
        auto buf = std::make_shared<juce::AudioBuffer<float>> (nch, len);
        reader->read (buf.get(), 0, len, 0, true, nch > 1);

        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < nch; ++c) s += buf->getSample (c, i);
            mono[(size_t) i] = s / (float) nch;
        }
        auto res = ml::analyse (mono.data(), len, rate);

        std::vector<float> ov (1500, 0.0f);
        for (int i = 0; i < len; ++i)
        {
            auto& o = ov[(size_t) ((juce::int64) i * 1500 / len)];
            o = std::max (o, std::abs (mono[(size_t) i]));
        }

        juce::MessageManager::callAsync ([this, flag, file, buf, rate, res, ov, hSeed, mSeed]
        {
            if (! *flag) return;
            audio = buf;
            audioRate = rate;
            analysis = res;
            overview = ov;
            loadedFile = file;
            harmonySeed = hSeed;
            melodySeed = mSeed;
            loading = false;
            if (analysis.notes.empty())
                setStatus ("No sung notes found - is this an acapella?");
            else
                setStatus (juce::String ((int) analysis.notes.size()) + " vocal notes detected");
            regenerate();
        });
    });
}

//==============================================================================
juce::File MelodyLabProcessor::writeMidiFile (int mask, const juce::File& target)
{
    auto d = published;
    if (d == nullptr) return {};

    const int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    bool first = true;
    juce::String partName;

    auto addPart = [&] (const std::vector<ml::Note>& v, int channel, const juce::String& name)
    {
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
        if (first)
        {
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / d->bpm)), 0.0);
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
            first = false;
        }
        for (auto& nt : v)
        {
            const int p = juce::jlimit (0, 127, nt.pitch);
            const float vel = juce::jlimit (0.05f, 1.0f, nt.velocity);
            seq.addEvent (juce::MidiMessage::noteOn (channel, p, vel), nt.start * tpq);
            seq.addEvent (juce::MidiMessage::noteOff (channel, p), (nt.start + nt.length) * tpq);
        }
        seq.updateMatchedPairs();
        seq.sort();
        mf.addTrack (seq);
        partName = partName.isEmpty() ? name : "All";
    };
    if (mask & kMelody) addPart (d->gen.melody, 1, "Melody");
    if (mask & kChords) addPart (d->gen.chords, 2, "Chords");
    if (mask & kBass)   addPart (d->gen.bass,   3, "Bass");
    if (first) return {};

    const juce::String fname = juce::File::createLegalFileName ("MelodyLab " + partName + " - " + d->keyName + " - "
                                                                + juce::String (d->bpm, 1) + "bpm.mid");
    juce::File out = target;
    if (out == juce::File())
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MelodyLab");
        dir.createDirectory();
        out = dir.getChildFile (fname);
    }
    else if (out.isDirectory())
    {
        out = out.getChildFile (fname);
    }
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (! os.openedOk()) return {};
    mf.writeTo (os, 1);
    os.flush();
    return out;
}

//==============================================================================
void MelodyLabProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("file", loadedFile.getFullPathName(), nullptr);
    state.setProperty ("hSeed", (int) harmonySeed, nullptr);
    state.setProperty ("mSeed", (int) melodySeed, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void MelodyLabProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);
    const auto path = state.getProperty ("file").toString();
    const auto hs = (uint32_t) (int) state.getProperty ("hSeed", 1);
    const auto ms = (uint32_t) (int) state.getProperty ("mSeed", 1);
    if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
        loadFile (juce::File (path), hs, ms);
}

juce::AudioProcessorEditor* MelodyLabProcessor::createEditor() { return new MelodyLabEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MelodyLabProcessor(); }

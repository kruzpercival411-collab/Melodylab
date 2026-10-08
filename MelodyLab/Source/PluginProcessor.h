// MELODY LAB — processor
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MelodyEngine.h"
#include "PreviewSynth.h"

struct PlaybackData
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double audioRate = 44100.0;
    double offsetSec = 0.0;
    double bpm = 120.0;
    juce::String keyName;
    ml::Generated gen;
};

class MelodyLabProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::Timer
{
public:
    MelodyLabProcessor();
    ~MelodyLabProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Melody Lab"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // called from the editor (message thread)
    void loadFile (const juce::File& file, uint32_t hSeed = 1, uint32_t mSeed = 1);
    void newChords()  { ++harmonySeed; regenerate(); }
    void newMelody()  { ++melodySeed;  regenerate(); }
    void togglePlay();
    void stopAndRewind();
    void seek (double beat) { internalBeat = beat; playheadBeat = beat; }

    enum PartMask { kMelody = 1, kChords = 2, kBass = 4, kAll = 7 };
    juce::File writeMidiFile (int partMask, const juce::File& targetOrEmpty);

    std::shared_ptr<const PlaybackData> getData() const { return published; }
    const ml::AnalysisResult& getAnalysis() const { return analysis; }
    const std::vector<float>& getOverview() const { return overview; }
    juce::String getStatus() const { const juce::ScopedLock sl (statusLock); return status; }
    void setStatus (const juce::String& s) { const juce::ScopedLock sl (statusLock); status = s; }
    juce::String getFileName() const { return loadedFile.getFileName(); }
    bool isLoading() const { return loading.load(); }
    ml::GenSettings currentSettings() const;
    double effectiveBpm() const;

    int getDataVersion() const { return dataVersion.load(); }
    double getPlayheadBeat() const { return playheadBeat.load(); }
    bool isPreviewPlaying() const { return hostPlaying.load() || internalPlay.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    juce::AudioProcessorValueTreeState apvts;
    static juce::StringArray keyChoices();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;
    void timerCallback() override;
    void regenerate();
    void publish (std::shared_ptr<const PlaybackData> d);

    // message-thread state
    ml::AnalysisResult analysis;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    double audioRate = 44100.0;
    std::vector<float> overview;
    juce::File loadedFile;
    uint32_t harmonySeed = 1, melodySeed = 1;
    std::shared_ptr<const PlaybackData> published;
    std::vector<std::shared_ptr<const PlaybackData>> graveyard;
    std::atomic<bool> needsRegen { false };
    std::atomic<bool> loading { false };
    double lastBpmUsed = 0.0;
    juce::CriticalSection statusLock;
    juce::String status { "Drop an acapella here (WAV / MP3 / AIFF / FLAC)" };
    std::atomic<int> dataVersion { 0 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    juce::ThreadPool pool { 1 };
    juce::AudioFormatManager formats;

    // audio-thread hand-off
    juce::SpinLock dataLock;
    std::shared_ptr<const PlaybackData> pending, audioData;

    // audio thread state
    PreviewSynth synth;
    double sampleRate = 44100.0;
    double lastEndBeat = -1.0;
    bool wasPlaying = false;
    std::atomic<bool> hostPlaying { false }, internalPlay { false };
    std::atomic<double> internalBeat { 0.0 }, playheadBeat { 0.0 }, hostBpm { 120.0 };
    struct Ev { int offset; int part; int pitch; float vel; double len; };
    std::vector<Ev> events;

    std::atomic<float>* pVocalVol = nullptr; std::atomic<float>* pMelVol = nullptr;
    std::atomic<float>* pChdVol = nullptr;   std::atomic<float>* pBassVol = nullptr;
    std::atomic<float>* pMelOn = nullptr;    std::atomic<float>* pChdOn = nullptr;
    std::atomic<float>* pBassOn = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyLabProcessor)
};

// MELODY LAB — editor
#include "PluginEditor.h"

using namespace mlui;

//==============================================================================
// Look and feel
//==============================================================================
LabLook::LabLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, amber);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, amber);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, amber);
    setColour (juce::ToggleButton::tickDisabledColourId, dim);
    setColour (juce::ScrollBar::thumbColourId, line.brighter (0.3f));
}

void LabLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);

    g.setColour (juce::Colour (0xff0d0e10));
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 2.5f, r - 2.5f, 0.0f, a0, a1, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, r - 2.5f, r - 2.5f, 0.0f, a0, ang, true);
    g.setColour (amber);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float kr = r * 0.62f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3f45), c.x, c.y - kr, juce::Colour (0xff1c1f22), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (amber.withAlpha (0.9f));
    const juce::Point<float> tip (c.x + std::sin (ang) * kr * 0.8f, c.y - std::cos (ang) * kr * 0.8f);
    g.drawLine (c.x + std::sin (ang) * kr * 0.25f, c.y - std::cos (ang) * kr * 0.25f, tip.x, tip.y, 2.0f);
}

void LabLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? panelHi.brighter (0.15f) : (over ? panelHi.brighter (0.07f) : panelHi));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.getToggleState() ? amber : line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void LabLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (panelHi);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path tri;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    tri.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (amber);
    g.fillPath (tri);
}

//==============================================================================
// Piano roll
//==============================================================================
void PianoRoll::refreshSize()
{
    auto d = proc.getData();
    const double beats = d != nullptr ? d->gen.totalBeats : 64.0;
    if (auto* parent = getParentComponent())
        setSize ((int) std::max<double> (parent->getWidth(), beats * pxPerBeat + 20), parent->getHeight());
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1113));
    auto d = proc.getData();
    const int headerH = 18;

    if (d == nullptr)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (proc.isLoading() ? "Listening to the vocal..." : "Drop an acapella onto the plugin to start",
                    getParentComponent() != nullptr ? getParentComponent()->getLocalBounds() : getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    const auto& gen = d->gen;
    int lo = 127, hi = 0;
    auto scan = [&] (const std::vector<ml::Note>& v) { for (auto& n : v) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); } };
    scan (gen.vocal); scan (gen.melody); scan (gen.chords); scan (gen.bass);
    if (lo > hi) { lo = 48; hi = 84; }
    lo -= 2; hi += 2;
    const float rowH = (float) (getHeight() - headerH) / (float) (hi - lo + 1);
    auto yOf = [&] (int pitch) { return headerH + (hi - pitch) * rowH; };
    auto xOf = [&] (double beat) { return 10.0f + (float) beat * pxPerBeat; };

    const auto clip = g.getClipBounds();

    // black-key shading
    for (int p = lo; p <= hi; ++p)
    {
        const int pc = p % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) { g.setColour (juce::Colour (0xff0b0c0e)); g.fillRect ((float) clip.getX(), yOf (p), (float) clip.getWidth(), rowH); }
        if (pc == 0) { g.setColour (line.withAlpha (0.5f)); g.drawHorizontalLine ((int) (yOf (p) + rowH), (float) clip.getX(), (float) clip.getRight()); }
    }

    // grid
    const int firstBeat = std::max (0, (int) ((clip.getX() - 10) / pxPerBeat));
    const int lastBeat = std::min ((int) gen.totalBeats, (int) ((clip.getRight() - 10) / pxPerBeat) + 1);
    for (int b = firstBeat; b <= lastBeat; ++b)
    {
        g.setColour (b % 4 == 0 ? line : line.withAlpha (0.35f));
        g.drawVerticalLine ((int) xOf (b), (float) headerH, (float) getHeight());
        if (b % 4 == 0 && pxPerBeat * 4 > 26)
        {
            g.setColour (dim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (b / 4 + 1), (int) xOf (b) + 2, getHeight() - 12, 30, 11, juce::Justification::left);
        }
    }

    // chord names
    g.setColour (panel);
    g.fillRect (clip.getX(), 0, clip.getWidth(), headerH);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (auto& c : gen.chordNames)
    {
        const float x0 = xOf (c.start), x1 = xOf (c.start + c.length);
        if (x1 < clip.getX() || x0 > clip.getRight()) continue;
        g.setColour (green);
        g.drawText (c.name, (int) x0 + 4, 1, (int) (x1 - x0) - 6, headerH - 2, juce::Justification::centredLeft, true);
        g.setColour (line);
        g.drawVerticalLine ((int) x0, 0.0f, (float) headerH);
    }

    auto drawNotes = [&] (const std::vector<ml::Note>& v, juce::Colour col, float alpha, bool outline)
    {
        for (auto& n : v)
        {
            const float x0 = xOf (n.start), w = std::max (2.0f, (float) n.length * pxPerBeat - 1.0f);
            if (x0 > clip.getRight() || x0 + w < clip.getX()) continue;
            juce::Rectangle<float> r (x0, yOf (n.pitch) + 0.5f, w, std::max (2.0f, rowH - 1.0f));
            g.setColour (col.withAlpha (alpha * (0.55f + 0.45f * n.velocity)));
            g.fillRoundedRectangle (r, 1.5f);
            if (outline) { g.setColour (col.brighter (0.4f)); g.drawRoundedRectangle (r, 1.5f, 0.8f); }
        }
    };
    drawNotes (gen.bass,   rust,  proc.apvts.getRawParameterValue ("bassOn")->load()   > 0.5f ? 0.8f : 0.2f, false);
    drawNotes (gen.chords, green, proc.apvts.getRawParameterValue ("chordsOn")->load() > 0.5f ? 0.45f : 0.12f, false);
    drawNotes (gen.vocal,  teal,  0.85f, false);
    drawNotes (gen.melody, amber, proc.apvts.getRawParameterValue ("melodyOn")->load() > 0.5f ? 1.0f : 0.25f, true);

    // playhead
    const float px = xOf (proc.getPlayheadBeat());
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    proc.seek (std::max (0.0, (e.position.x - 10.0) / pxPerBeat));
    repaint();
}

//==============================================================================
// Overview strip (waveform + drop zone)
//==============================================================================
void Overview::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (dragHover ? panelHi.brighter (0.1f) : panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (dragHover ? amber : line);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, dragHover ? 2.0f : 1.0f);

    const auto& ov = proc.getOverview();
    if (ov.empty())
    {
        g.setColour (dragHover ? amber : dim);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (proc.isLoading() ? "ANALYSING..." : "DROP ACAPELLA HERE", getLocalBounds(), juce::Justification::centred);
        return;
    }

    float mx = 1e-4f;
    for (float v : ov) mx = std::max (mx, v);
    const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
    const float step = (r.getWidth() - 16.0f) / (float) ov.size();
    g.setColour (teal.withAlpha (0.75f));
    for (size_t i = 0; i < ov.size(); ++i)
    {
        const float h = std::max (0.5f, ov[i] / mx * half);
        g.fillRect (8.0f + (float) i * step, mid - h, std::max (1.0f, step), h * 2.0f);
    }

    auto d = proc.getData();
    const double dur = proc.getAnalysis().durationSec;
    if (d != nullptr && dur > 0)
    {
        const double sec = proc.getPlayheadBeat() * 60.0 / d->bpm + d->offsetSec;
        const float x = 8.0f + (float) (sec / dur) * (r.getWidth() - 16.0f);
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) x, 2.0f, r.getBottom() - 2.0f);
    }
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (proc.getFileName(), getLocalBounds().reduced (10, 4), juce::Justification::topLeft);
}

void Overview::mouseDown (const juce::MouseEvent& e)
{
    auto d = proc.getData();
    const double dur = proc.getAnalysis().durationSec;
    if (d == nullptr || dur <= 0) return;
    const double sec = juce::jlimit (0.0, 1.0, (e.position.x - 8.0) / (getWidth() - 16.0)) * dur;
    proc.seek (std::max (0.0, (sec - d->offsetSec) * d->bpm / 60.0));
}

//==============================================================================
// Drag tiles
//==============================================================================
void DragTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (hover ? panelHi.brighter (0.08f) : panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (r.withWidth (5.0f), 2.0f);
    g.setColour (hover ? colour : line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().withTrimmedLeft (14).withTrimmedBottom (14), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("drag to Ableton / click to save", getLocalBounds().withTrimmedLeft (14).withTrimmedTop (20), juce::Justification::centredLeft);
}

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    const auto f = proc.writeMidiFile (partMask, {});
    if (! f.existsAsFile()) { proc.setStatus ("Nothing to export yet - drop an acapella first"); return; }
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
}

void DragTile::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() >= 6) return;
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Melody Lab");
    dir.createDirectory();
    const auto f = proc.writeMidiFile (partMask, dir);
    proc.setStatus (f.existsAsFile() ? "Saved " + f.getFileName() + " to Documents/Melody Lab"
                                     : "Nothing to export yet - drop an acapella first");
}

//==============================================================================
// Editor
//==============================================================================
juce::Slider& MelodyLabEditor::knob (const juce::String& id, const juce::String& label)
{
    auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
    addAndMakeVisible (s);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *s));
    auto* l = labels.add (new juce::Label ({}, label.toUpperCase()));
    l->setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l->setJustificationType (juce::Justification::centred);
    l->attachToComponent (s, false);
    byId[id] = s;
    return *s;
}

juce::ComboBox& MelodyLabEditor::combo (const juce::String& id, const juce::String& label)
{
    auto* c = combos.add (new juce::ComboBox());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->addItemList (p->choices, 1);
    addAndMakeVisible (c);
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, id, *c));
    auto* l = labels.add (new juce::Label ({}, label.toUpperCase()));
    l->setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l->setJustificationType (juce::Justification::centredRight);
    l->attachToComponent (c, true);
    byId[id] = c;
    return *c;
}

MelodyLabEditor::MelodyLabEditor (MelodyLabProcessor& p)
    : AudioProcessorEditor (&p), proc (p), overview (p), roll (p),
      tileMelody (p, "MELODY", MelodyLabProcessor::kMelody, amber),
      tileChords (p, "CHORDS", MelodyLabProcessor::kChords, green),
      tileBass   (p, "BASS",   MelodyLabProcessor::kBass,   rust),
      tileAll    (p, "ALL PARTS", MelodyLabProcessor::kAll, teal)
{
    setLookAndFeel (&look);

    addAndMakeVisible (overview);
    viewport.setViewedComponent (&roll, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    zoom.setSliderStyle (juce::Slider::LinearHorizontal);
    zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoom.setRange (4.0, 60.0);
    zoom.setValue (roll.pxPerBeat, juce::dontSendNotification);
    zoom.setColour (juce::Slider::thumbColourId, amber);
    zoom.setColour (juce::Slider::trackColourId, line);
    zoom.onValueChange = [this] { roll.pxPerBeat = (float) zoom.getValue(); roll.refreshSize(); roll.repaint(); };
    addAndMakeVisible (zoom);

    combo ("style", "Style");
    combo ("melodyMode", "Melody");
    combo ("harmonyMode", "Chords");
    combo ("chordRate", "Chord rate");
    combo ("keyMode", "Key");
    combo ("bpmSource", "Tempo");

    knob ("density", "Density");
    knob ("complexity", "Complexity");
    knob ("octave", "Octave");
    knob ("bpm", "BPM");
    knob ("offsetMs", "Grid offset");
    knob ("vocalVol", "Vocal");
    knob ("melodyVol", "Melody");
    knob ("chordsVol", "Chords");
    knob ("bassVol", "Bass");

    for (auto* t : { &melodyOn, &chordsOn, &bassOn }) addAndMakeVisible (t);
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "melodyOn", melodyOn));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "chordsOn", chordsOn));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, "bassOn", bassOn));
    for (auto* t : { &melodyOn, &chordsOn, &bassOn }) t->onClick = [this] { roll.repaint(); };

    for (auto* b : { &loadBtn, &playBtn, &stopBtn, &newChordsBtn, &newMelodyBtn }) addAndMakeVisible (b);
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an acapella", juce::File(), "*.wav;*.mp3;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadFile (f);
                              });
    };
    playBtn.onClick      = [this] { proc.togglePlay(); };
    stopBtn.onClick      = [this] { proc.stopAndRewind(); viewport.setViewPosition (0, 0); };
    newChordsBtn.onClick = [this] { proc.newChords(); };
    newMelodyBtn.onClick = [this] { proc.newMelody(); };

    for (auto* t : { &tileMelody, &tileChords, &tileBass, &tileAll }) addAndMakeVisible (t);

    setSize (1060, 700);
    startTimerHz (30);
}

MelodyLabEditor::~MelodyLabEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MelodyLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // header
    g.setColour (amber);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("MELODY LAB", 14, 8, 220, 30, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("acapella in - melodies out", 16, 34, 220, 14, juce::Justification::centredLeft);

    g.setColour (text);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (proc.getStatus(), 240, 10, 360, 34, juce::Justification::centredLeft, true);

    // key / tempo readout
    auto d = proc.getData();
    const auto& an = proc.getAnalysis();
    juce::String keyLine = "--", sub = "no vocal loaded";
    if (d != nullptr)
    {
        keyLine = d->keyName.toUpperCase();
        sub = "detected " + juce::String (ml::keyName (an.key.tonic, an.key.minor)) + " ("
              + juce::String (juce::roundToInt (an.key.confidence * 100.0f)) + "%)  -  "
              + juce::String (d->bpm, 1) + " BPM";
    }
    g.setColour (amber);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (keyLine, 600, 6, 280, 26, juce::Justification::centredRight);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (sub, 560, 32, 320, 14, juce::Justification::centredRight);

    // legend
    auto legend = [&] (int x, juce::Colour c, const char* t)
    {
        g.setColour (c); g.fillRoundedRectangle ((float) x, 410.0f, 14.0f, 8.0f, 2.0f);
        g.setColour (dim); g.setFont (juce::FontOptions (10.5f));
        g.drawText (t, x + 18, 406, 70, 16, juce::Justification::centredLeft);
    };
    legend (14, teal, "Vocal"); legend (84, amber, "Melody"); legend (160, green, "Chords"); legend (236, rust, "Bass");
    g.setColour (dim);
    g.drawText ("click the roll to move the playhead", 310, 406, 260, 16, juce::Justification::centredLeft);
    g.drawText ("ZOOM", 812, 406, 40, 16, juce::Justification::centredRight);

    // section panels
    auto section = [&] (juce::Rectangle<int> r, const char* title)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        g.setColour (amber.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14, juce::Justification::centredLeft);
    };
    section ({ 12, 430, 300, 258 },  "GENERATE");
    section ({ 324, 430, 330, 258 }, "SHAPE & TIMING");
    section ({ 666, 430, 220, 258 }, "PREVIEW MIX");
    section ({ 898, 430, 150, 258 }, "EXPORT MIDI");
}

void MelodyLabEditor::resized()
{
    loadBtn.setBounds (getWidth() - 162, 12, 150, 30);
    overview.setBounds (12, 58, getWidth() - 24, 68);
    viewport.setBounds (12, 134, getWidth() - 24, 266);
    roll.refreshSize();
    zoom.setBounds (858, 404, 190, 20);

    // GENERATE
    const char* gen[] = { "style", "melodyMode", "harmonyMode", "chordRate", "keyMode" };
    for (int i = 0; i < 5; ++i) byId[gen[i]]->setBounds (102, 458 + i * 31, 198, 24);
    newChordsBtn.setBounds (24, 624, 132, 34);
    newMelodyBtn.setBounds (166, 624, 134, 34);

    // SHAPE & TIMING
    byId["density"]->setBounds    (336, 468, 96, 86);
    byId["complexity"]->setBounds (440, 468, 96, 86);
    byId["octave"]->setBounds     (544, 468, 96, 86);
    byId["bpmSource"]->setBounds  (390, 600, 120, 24);
    byId["bpm"]->setBounds        (516, 578, 66, 78);
    byId["offsetMs"]->setBounds   (584, 578, 66, 78);

    // PREVIEW MIX
    const char* mix[] = { "vocalVol", "melodyVol", "chordsVol", "bassVol" };
    for (int i = 0; i < 4; ++i) byId[mix[i]]->setBounds (672 + i * 53, 468, 52, 74);
    melodyOn.setBounds (676, 556, 70, 22);
    chordsOn.setBounds (746, 556, 70, 22);
    bassOn.setBounds   (816, 556, 66, 22);
    playBtn.setBounds  (678, 610, 98, 40);
    stopBtn.setBounds  (782, 610, 92, 40);

    // EXPORT
    int y = 456;
    for (auto* t : { &tileMelody, &tileChords, &tileBass, &tileAll }) { t->setBounds (908, y, 130, 50); y += 56; }
}

bool MelodyLabEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;aif;aiff;flac;ogg")) return true;
    return false;
}

void MelodyLabEditor::filesDropped (const juce::StringArray& files, int, int)
{
    overview.dragHover = false;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (file.hasFileExtension ("wav;mp3;aif;aiff;flac;ogg")) { proc.loadFile (file); break; }
    }
    repaint();
}

void MelodyLabEditor::timerCallback()
{
    const int v = proc.getDataVersion();
    const auto st = proc.getStatus();
    if (v != lastVersion)
    {
        lastVersion = v;
        roll.refreshSize();
        roll.repaint();
        overview.repaint();
        repaint (0, 0, getWidth(), 56);
    }
    if (st != lastStatus) { lastStatus = st; repaint (0, 0, getWidth(), 56); overview.repaint(); }

    const bool playing = proc.isPreviewPlaying();
    playBtn.setButtonText (proc.isHostPlaying() ? "HOST" : (playing ? "PAUSE" : "PLAY"));
    playBtn.setToggleState (playing, juce::dontSendNotification);

    const bool manual = proc.apvts.getRawParameterValue ("bpmSource")->load() > 0.5f;
    byId["bpm"]->setEnabled (manual);

    if (playing || proc.isLoading())
    {
        overview.repaint();
        const int px = (int) (10.0 + proc.getPlayheadBeat() * roll.pxPerBeat);
        const auto view = viewport.getViewArea();
        if (playing && (px < view.getX() || px > view.getRight() - 40))
            viewport.setViewPosition (std::max (0, px - 60), 0);
        roll.repaint (viewport.getViewArea());
    }
}

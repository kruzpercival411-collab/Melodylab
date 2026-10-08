// MELODY LAB — editor
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include "PluginProcessor.h"

namespace mlui
{
const juce::Colour bg       { 0xff131518 };
const juce::Colour panel    { 0xff1d2024 };
const juce::Colour panelHi  { 0xff272b30 };
const juce::Colour line     { 0xff33383e };
const juce::Colour text     { 0xffd9d4ca };
const juce::Colour dim      { 0xff8a8a84 };
const juce::Colour amber    { 0xffe8a33d };
const juce::Colour teal     { 0xff3fb8b0 };
const juce::Colour green    { 0xff7fa36b };
const juce::Colour rust     { 0xffc9614a };

class LabLook : public juce::LookAndFeel_V4
{
public:
    LabLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

class PianoRoll : public juce::Component
{
public:
    explicit PianoRoll (MelodyLabProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refreshSize();
    float pxPerBeat = 18.0f;
private:
    MelodyLabProcessor& proc;
};

class Overview : public juce::Component
{
public:
    explicit Overview (MelodyLabProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool dragHover = false;
private:
    MelodyLabProcessor& proc;
};

class DragTile : public juce::Component
{
public:
    DragTile (MelodyLabProcessor& p, const juce::String& name, int mask, juce::Colour c) : proc (p), title (name), partMask (mask), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
private:
    MelodyLabProcessor& proc;
    juce::String title;
    int partMask;
    juce::Colour colour;
    bool dragging = false, hover = false;
};
}

class MelodyLabEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit MelodyLabEditor (MelodyLabProcessor&);
    ~MelodyLabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { overview.dragHover = true; overview.repaint(); }
    void fileDragExit (const juce::StringArray&) override              { overview.dragHover = false; overview.repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    juce::Slider& knob (const juce::String& paramId, const juce::String& label);
    juce::ComboBox& combo (const juce::String& paramId, const juce::String& label);

    MelodyLabProcessor& proc;
    mlui::LabLook look;

    mlui::Overview overview;
    mlui::PianoRoll roll;
    juce::Viewport viewport;
    juce::Slider zoom;

    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::map<juce::String, juce::Component*> byId;

    juce::ToggleButton melodyOn { "Melody" }, chordsOn { "Chords" }, bassOn { "Bass" };
    juce::TextButton loadBtn { "LOAD ACAPELLA" }, playBtn { "PLAY" }, stopBtn { "STOP" },
                     newChordsBtn { "NEW CHORDS" }, newMelodyBtn { "NEW MELODY" };
    mlui::DragTile tileMelody, tileChords, tileBass, tileAll;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastVersion = -1;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyLabEditor)
};

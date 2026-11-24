#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace ui {
// Palette: dark gray panel, pastel purple as the only strong colour, neutral grays elsewhere.
inline juce::Colour bg()      { return juce::Colour(0xff26262b); }
inline juce::Colour panel()   { return juce::Colour(0xff303036); }
inline juce::Colour edge()    { return juce::Colour(0xff45454d); }
inline juce::Colour purple()  { return juce::Colour(0xffc9b6f2); }
inline juce::Colour purpleDim() { return juce::Colour(0xff7d6fa3); }
inline juce::Colour text()    { return juce::Colour(0xfff0f0f4); }
inline juce::Colour muted()   { return juce::Colour(0xffb4b4be); }
inline juce::Colour knobArc() { return juce::Colour(0xffdcdce3); }
inline juce::Colour track()   { return juce::Colour(0xff4c4c55); }

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
};

// A labelled rotary knob with its value under it. `accent` = pastel purple (main control).
class Knob : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label, bool accent = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider slider;
    juce::String label; bool accent; float labelSize = 13.0f;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// What the voice is doing to the instrument right now: 28 vowel bands, the note, tone and consonants.
class VowelView : public juce::Component {
public:
    explicit VowelView(HuskProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    HuskProcessor& proc;
};

class ModelBar : public juce::Component {   // the nine instrument models
public:
    explicit ModelBar(HuskProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    HuskProcessor& proc;
};

class Segmented : public juce::Component {   // Octave: -1 / 0 / +1
public:
    Segmented(HuskProcessor& p, int paramIndex, juce::StringArray items) : proc(p), idx(paramIndex), items(std::move(items)) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    HuskProcessor& proc; int idx; juce::StringArray items;
};
} // namespace ui

class HuskEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit HuskEditor(HuskProcessor&);
    ~HuskEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr int kW = 1200, kH = 760;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    struct Content : public juce::Component { HuskEditor* ed = nullptr; void paint(juce::Graphics&) override; };
    HuskProcessor& proc;
    ui::LookAndFeel lnf;
    Content content;
    struct Led : public juce::Component { HuskEditor* ed = nullptr; void paint(juce::Graphics&) override; } midiLed;
    std::unique_ptr<ui::VowelView> vowels;
    std::unique_ptr<ui::ModelBar> models;
    std::unique_ptr<ui::Segmented> octave;
    std::unique_ptr<ui::Knob> kPartials, kTalk, kPitch, kFormant, kPhase, kPhaseRate, kAir, kShimmer, kSpace, kCrunch, kMix, kOut,
        kUnison, kDetune, kWidth, kDrift, kGlide;
    juce::ComboBox presetBox, stepsBox, divBox, keyBox, scaleBox;
    juce::TextButton abBtn { "A/B" }, bypassBtn { "BYPASS" }, freezeBtn { "FREEZE" }, chordBtn { "CHORD (MIDI)" }, syncBtn { "SYNC" }, snapBtn { "NOTE SNAP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> abAtt, byAtt, frAtt, chAtt, syAtt, snAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stAtt, dvAtt, kyAtt, scAtt;
    long long lastMidi = 0; int midiGlow = 0; int lastProgramShown = -1;
    friend struct Content; friend struct Led;
};

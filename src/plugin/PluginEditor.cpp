#include "PluginEditor.h"
using namespace ui;

static juce::Font font(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
static void caption(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c, float size = 13.0f,
                    juce::Justification j = juce::Justification::centred, bool bold = true) {
    g.setColour(c); g.setFont(font(size, bold).withExtraKerningFactor(0.06f)); g.drawText(t, r, j, false);
}

// ------------------------------------------------------------------ look and feel
LookAndFeel::LookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, panel());
    setColour(juce::PopupMenu::textColourId, text());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, purple());
    setColour(juce::PopupMenu::highlightedTextColourId, bg());
    setColour(juce::ComboBox::textColourId, text());
    setColour(juce::ComboBox::arrowColourId, muted());
    setColour(juce::ComboBox::backgroundColourId, panel());
    setColour(juce::ComboBox::outlineColourId, edge());
    setColour(juce::TextButton::textColourOffId, text());
    setColour(juce::TextButton::textColourOnId, bg());
}
void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    const bool accent = s.getProperties()["accent"];
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4);
    float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f; auto c = r.getCentre();
    float thick = accent ? juce::jmax(5.0f, rad * 0.13f) : juce::jmax(3.5f, rad * 0.11f);
    float ang = a0 + pos * (a1 - a0);
    juce::Path trackP; trackP.addCentredArc(c.x, c.y, rad - thick, rad - thick, 0, a0, a1, true);
    g.setColour(track()); g.strokePath(trackP, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // bipolar controls fill from the centre
    double mn = s.getMinimum(), mx = s.getMaximum(); bool bip = mn < 0 && mx > 0;
    float from = bip ? a0 + (float)((0 - mn) / (mx - mn)) * (a1 - a0) : a0;
    if (std::abs(ang - from) > 0.001f) {
        juce::Path v; v.addCentredArc(c.x, c.y, rad - thick, rad - thick, 0, juce::jmin(from, ang), juce::jmax(from, ang), true);
        g.setColour(accent ? purple() : knobArc()); g.strokePath(v, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    float inner = rad - thick * 2.2f;
    g.setColour(panel().brighter(0.08f)); g.fillEllipse(c.x - inner, c.y - inner, inner * 2, inner * 2);
    g.setColour(edge()); g.drawEllipse(c.x - inner, c.y - inner, inner * 2, inner * 2, 1.0f);
    juce::Path tick; tick.addRoundedRectangle(-1.6f, -inner + 3, 3.2f, inner * 0.45f, 1.5f);
    g.setColour(accent ? purple() : text()); g.fillPath(tick, juce::AffineTransform::rotation(ang).translated(c.x, c.y));
}
void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    const bool on = b.getToggleState(); const bool accent = b.getProperties()["accent"];
    juce::Colour fill = on ? (accent ? purple() : knobArc()) : panel();
    if (over && !on) fill = fill.brighter(0.07f);
    if (down) fill = fill.darker(0.1f);
    g.setColour(fill); g.fillRoundedRectangle(r, 7.0f);
    g.setColour(on ? fill : (accent ? purple() : edge())); g.drawRoundedRectangle(r, 7.0f, accent ? 2.0f : 1.2f);
}
void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const bool on = b.getToggleState(); const bool accent = b.getProperties()["accent"];
    auto t = b.getButtonText(); if (on && b.getProperties().contains("onText")) t = b.getProperties()["onText"].toString();
    float size = b.getProperties().contains("size") ? (float)b.getProperties()["size"] : 14.0f;
    caption(g, b.getLocalBounds().toFloat(), t, on ? bg() : (accent ? purple() : text()), size);
}
void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) {
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(panel()); g.fillRoundedRectangle(r, 6.0f);
    g.setColour(edge()); g.drawRoundedRectangle(r, 6.0f, 1.2f);
    juce::Path ar; float ax = (float)w - 16, ay = h * 0.5f;
    ar.addTriangle(ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(cb.isEnabled() ? muted() : track()); g.fillPath(ar);
}
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return font(14.0f, true); }
juce::Font LookAndFeel::getPopupMenuFont() { return font(15.0f); }
juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int) { return font(14.0f, true); }
void LookAndFeel::positionComboBoxText(juce::ComboBox& cb, juce::Label& l) {
    l.setBounds(8, 1, cb.getWidth() - 30, cb.getHeight() - 2); l.setFont(getComboBoxFont(cb)); l.setJustificationType(juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ knob
Knob::Knob(juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& lab, bool acc) : label(lab), accent(acc) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set("accent", acc);
    addAndMakeVisible(slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, id, slider);
    if (auto* p = s.getParameter(id)) slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}
void Knob::resized() { auto r = getLocalBounds(); r.removeFromTop(22); r.removeFromBottom(22); slider.setBounds(r); }
void Knob::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    caption(g, r.removeFromTop(20), label, accent ? purple() : text(), labelSize);
    auto txt = slider.getTextFromValue(slider.getValue());
    caption(g, r.removeFromBottom(20), txt, muted(), 13.0f, juce::Justification::centred, false);
}

// ------------------------------------------------------------------ vowel view
void VowelView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(panel()); g.fillRoundedRectangle(r, 12.0f);
    g.setColour(edge()); g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
    auto in = r.reduced(18, 14);
    auto top = in.removeFromTop(22);
    caption(g, top, "YOUR VOWELS SHAPING THE INSTRUMENT", muted(), 13.0f, juce::Justification::centredLeft);
    const bool v = proc.voiced.load(); const int nn = proc.noteNow.load(); const float f = proc.f0.load();
    juce::String note = "No clear note";
    if (nn >= 0 && f > 0) {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int m = juce::jlimit(0, 127, nn);
        note = "Playing " + juce::String(names[m % 12]) + juce::String(m / 12 - 1) + "   " + juce::String(f, 1) + " Hz";
    }
    caption(g, top, note, nn >= 0 ? text() : muted(), 15.0f, juce::Justification::centredRight);
    // bottom: tone and consonant meters
    auto bottom = in.removeFromBottom(22);
    auto meter = [&](juce::Rectangle<float> m, const juce::String& lab, float val, juce::Colour c) {
        caption(g, m.removeFromLeft(96), lab, text(), 12.5f, juce::Justification::centredLeft);
        auto bar = m.reduced(0, 6); g.setColour(track()); g.fillRoundedRectangle(bar, 3.0f);
        g.setColour(c); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, val)), 3.0f);
    };
    float half = bottom.getWidth() * 0.5f;
    meter(bottom.removeFromLeft(half - 12), "NOTE ON", proc.toneLevel.load(), purple());
    bottom.removeFromLeft(24);
    meter(bottom, "CONSONANT", proc.consLevel.load(), knobArc());
    in.removeFromBottom(22);   // room for the Hz labels
    // 28 vowel bands
    const float base = in.getBottom(), hMax = in.getHeight() - 22;
    g.setColour(edge()); g.drawHorizontalLine((int)base, in.getX(), in.getRight());
    bool live = proc.toneLevel.load() > 0.02f || proc.holding.load();
    const int nb = husk::kVB; float bw = in.getWidth() / nb;
    for (int b = 0; b < nb; ++b) {
        float val = proc.dispBands[(size_t)b].load();
        auto col = juce::Rectangle<float>(in.getX() + b * bw + bw * 0.18f, base - 4, bw * 0.64f, 4);
        if (live) {
            float h = juce::jmax(4.0f, val * hMax);
            auto bar = col.withTop(base - h);
            g.setColour(purple().withAlpha(0.18f)); g.fillRoundedRectangle(bar.expanded(2.5f, 0), 3.0f);
            g.setColour(purple()); g.fillRoundedRectangle(bar, 2.5f);
        } else { g.setColour(track()); g.fillRoundedRectangle(col, 2.0f); }
    }
    for (auto [hz, lab] : { std::pair<float, const char*>{ 200.0f, "200" }, { 1000.0f, "1k" }, { 4000.0f, "4k" } }) {
        float x = -1; for (int b = 0; b + 1 < nb; ++b) if (proc.engine().bandFc(b) <= hz && proc.engine().bandFc(b + 1) > hz) x = in.getX() + (b + 0.5f + std::log(hz / proc.engine().bandFc(b)) / std::log(proc.engine().bandFc(b + 1) / proc.engine().bandFc(b))) * bw;
        if (x > 0) caption(g, { x - 20, base + 1, 40, 14 }, juce::String(lab) + " Hz", muted(), 10.5f, juce::Justification::centred, false);
    }
    if (!live) caption(g, in.withTrimmedBottom(in.getHeight() * 0.3f), v ? "Listening..." : "Sing or hum into the track", muted(), 17.0f, juce::Justification::centred, false);
    if (proc.holding.load()) caption(g, in.removeFromTop(24), "FROZEN", purple(), 14.0f, juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ model bar / segmented
void ModelBar::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat(); int sel = (int)std::lround(proc.value(P_MODEL));
    const int n = husk::kNumModels; float w = (r.getWidth() - (n - 1) * 8) / n;
    for (int i = 0; i < n; ++i) {
        auto c = juce::Rectangle<float>(r.getX() + i * (w + 8), r.getY(), w, r.getHeight()).reduced(0.5f);
        bool on = i == sel;
        g.setColour(on ? purple() : panel()); g.fillRoundedRectangle(c, 8.0f);
        g.setColour(on ? purple() : edge()); g.drawRoundedRectangle(c, 8.0f, 1.2f);
        caption(g, c, juce::String(husk::modelName(i)).toUpperCase(), on ? bg() : text(), 13.5f);
    }
}
void ModelBar::mouseDown(const juce::MouseEvent& e) {
    const int n = husk::kNumModels; float w = (getWidth() - (n - 1) * 8) / (float)n; int i = juce::jlimit(0, n - 1, (int)(e.position.x / (w + 8)));
    proc.setValue(P_MODEL, (float)i); repaint();
}
void Segmented::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat(); int sel = (int)std::lround(proc.value(idx)); int n = items.size();
    float w = (r.getWidth() - (n - 1) * 6) / n;
    for (int i = 0; i < n; ++i) {
        auto c = juce::Rectangle<float>(r.getX() + i * (w + 6), r.getY(), w, r.getHeight()).reduced(0.5f);
        bool on = i == sel;
        g.setColour(on ? knobArc() : panel()); g.fillRoundedRectangle(c, 7.0f);
        g.setColour(on ? knobArc() : edge()); g.drawRoundedRectangle(c, 7.0f, 1.2f);
        caption(g, c, items[i], on ? bg() : text(), 15.0f);
    }
}
void Segmented::mouseDown(const juce::MouseEvent& e) {
    int n = items.size(); float w = (getWidth() - (n - 1) * 6) / (float)n; int i = juce::jlimit(0, n - 1, (int)(e.position.x / (w + 6)));
    proc.setValue(idx, (float)i); repaint();
}

// ------------------------------------------------------------------ editor
HuskEditor::HuskEditor(HuskProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&lnf);
    content.ed = this;
    addAndMakeVisible(content);
    vowels = std::make_unique<ui::VowelView>(p); content.addAndMakeVisible(*vowels);
    models = std::make_unique<ui::ModelBar>(p); content.addAndMakeVisible(*models);
    octave = std::make_unique<ui::Segmented>(p, P_OCTAVE, juce::StringArray { "-1", "0", "+1" }); content.addAndMakeVisible(*octave);
    auto& s = p.apvts;
    kPartials = std::make_unique<ui::Knob>(s, "partials", "PARTIALS", true); kPartials->labelSize = 15.0f;
    kTalk = std::make_unique<ui::Knob>(s, "talk", "TALK", true); kTalk->labelSize = 15.0f;
    kPitch = std::make_unique<ui::Knob>(s, "pitch", "PITCH");
    kFormant = std::make_unique<ui::Knob>(s, "formant", "FORMANT");
    kPhase = std::make_unique<ui::Knob>(s, "phase", "PHASE");
    kPhaseRate = std::make_unique<ui::Knob>(s, "phase_rate", "PHASE RATE");
    kAir = std::make_unique<ui::Knob>(s, "air", "AIR");
    kShimmer = std::make_unique<ui::Knob>(s, "shimmer", "SHIMMER");
    kSpace = std::make_unique<ui::Knob>(s, "space", "SPACE");
    kCrunch = std::make_unique<ui::Knob>(s, "crunch", "CRUNCH");
    kMix = std::make_unique<ui::Knob>(s, "mix", "MIX");
    kOut = std::make_unique<ui::Knob>(s, "out", "OUT");
    kUnison = std::make_unique<ui::Knob>(s, "unison", "UNISON");
    kDetune = std::make_unique<ui::Knob>(s, "detune", "DETUNE");
    kWidth = std::make_unique<ui::Knob>(s, "width", "WIDTH");
    kDrift = std::make_unique<ui::Knob>(s, "drift", "DRIFT");
    kGlide = std::make_unique<ui::Knob>(s, "glide", "GLIDE");
    for (auto* k : { kPartials.get(), kTalk.get(), kPitch.get(), kFormant.get(), kPhase.get(), kPhaseRate.get(), kAir.get(), kShimmer.get(), kSpace.get(), kCrunch.get(), kMix.get(), kOut.get(),
                     kUnison.get(), kDetune.get(), kWidth.get(), kDrift.get(), kGlide.get() })
        content.addAndMakeVisible(*k);
    int c = 0; auto* pr = husk::presets(c);
    for (int i = 0; i < c; ++i) presetBox.addItem(pr[i].name, i + 1);
    presetBox.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { int i = presetBox.getSelectedId() - 1; if (i >= 0) { proc.setCurrentProgram(i); lastProgramShown = i; } };
    stepsBox.addItemList({ "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    divBox.addItemList({ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    keyBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList({ "Chromatic", "Major", "Minor", "Pentatonic", "Minor Pent." }, 1);
    for (auto* cb : { &presetBox, &stepsBox, &divBox, &keyBox, &scaleBox }) content.addAndMakeVisible(*cb);
    stAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "steps", stepsBox);
    dvAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "phase_div", divBox);
    kyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "key", keyBox);
    scAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "scale", scaleBox);
    for (auto* b : { &abBtn, &bypassBtn, &freezeBtn, &chordBtn, &syncBtn, &snapBtn }) { b->setClickingTogglesState(true); content.addAndMakeVisible(*b); }
    midiLed.ed = this; midiLed.setInterceptsMouseClicks(false, false); content.addAndMakeVisible(midiLed);
    freezeBtn.getProperties().set("accent", true); freezeBtn.getProperties().set("onText", "FROZEN"); freezeBtn.getProperties().set("size", 18.0f);
    chordBtn.getProperties().set("size", 15.0f); syncBtn.getProperties().set("size", 12.0f); snapBtn.getProperties().set("size", 13.0f); snapBtn.getProperties().set("accent", true);
    abBtn.getProperties().set("onText", "A/B: DRY"); bypassBtn.getProperties().set("onText", "BYPASSED");
    abAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "ab", abBtn);
    byAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "bypass", bypassBtn);
    frAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "freeze", freezeBtn);
    chAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "chord", chordBtn);
    syAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "phase_sync", syncBtn);
    snAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "snap", snapBtn);
    abBtn.setTooltip("Hear the untouched voice (time-aligned) to compare");

    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(kW * 7 / 10, kH * 7 / 10, kW * 2, kH * 2);
    if (auto* cons = getConstrainer()) cons->setFixedAspectRatio((double)kW / kH);
    setSize(kW, kH);
    startTimerHz(30);
}
HuskEditor::~HuskEditor() { stopTimer(); setLookAndFeel(nullptr); }

void HuskEditor::paint(juce::Graphics& g) { g.fillAll(bg()); }

void HuskEditor::resized() {
    content.setBounds(0, 0, kW, kH);
    float sc = juce::jmin(getWidth() / (float)kW, getHeight() / (float)kH);
    content.setTransform(juce::AffineTransform::scale(sc));
    // header
    presetBox.setBounds(392, 20, 250, 32);
    abBtn.setBounds(966, 20, 98, 32); bypassBtn.setBounds(1074, 20, 102, 32);
    models->setBounds(24, 72, 1152, 44);
    // vowel view + ensemble / note snap panel
    vowels->setBounds(24, 130, 790, 260);
    int x = 836, y = 160;
    for (auto* k : { kUnison.get(), kDetune.get(), kWidth.get(), kDrift.get() }) { k->setBounds(x, y, 82, 112); x += 84; }
    snapBtn.setBounds(840, 312, 116, 32); keyBox.setBounds(962, 312, 64, 32); scaleBox.setBounds(1032, 312, 130, 32);
    // row 1
    y = 404; int h = 144; x = 24;
    kPartials->setBounds(x, y - 4, 148, h + 8); x += 152;
    kTalk->setBounds(x, y - 4, 148, h + 8); x += 156;
    kPitch->setBounds(x, y, 104, h); x += 108;
    octave->setBounds(x + 4, y + 52, 124, 40); x += 140;
    kFormant->setBounds(x, y, 104, h); x += 108;
    kGlide->setBounds(x, y, 104, h); x += 108;
    kPhase->setBounds(x, y, 104, h); x += 108;
    kPhaseRate->setBounds(x, y, 150, h - 30);
    syncBtn.setBounds(x + 4, y + h - 28, 60, 26); divBox.setBounds(x + 70, y + h - 28, 84, 26); x += 166;
    stepsBox.setBounds(x + 6, y + 52, 120, 34);
    // row 2
    y = 576; h = 130; x = 24;
    freezeBtn.setBounds(x, y + 26, 160, 72); x += 172;
    chordBtn.setBounds(x, y + 26, 178, 72); midiLed.setBounds(x + 178 - 24, y + 26 + 10, 14, 14); x += 192;
    for (auto* k : { kAir.get(), kShimmer.get(), kSpace.get(), kCrunch.get(), kMix.get(), kOut.get() }) { k->setBounds(x, y, 120, h); x += 125; }
}

void HuskEditor::Content::paint(juce::Graphics& g) {
    auto& e = *ed;
    g.fillAll(bg());
    caption(g, { 24, 14, 120, 44 }, "HUSK", purple(), 36.0f, juce::Justification::centredLeft);
    caption(g, { 146, 22, 240, 16 }, "VOICE INTO INSTRUMENT", muted(), 12.0f, juce::Justification::centredLeft, false);
    caption(g, { 146, 38, 240, 16 }, "v2.0", muted(), 11.0f, juce::Justification::centredLeft, false);
    caption(g, { 324, 20, 64, 32 }, "PRESET", muted(), 12.0f, juce::Justification::centredRight);
    double ms = e.proc.latencyNow() / juce::jmax(1.0, e.proc.sampleRateNow()) * 1000.0;
    caption(g, { 664, 20, 220, 32 }, "LATENCY " + juce::String(ms, 1) + " ms", muted(), 12.0f, juce::Justification::centredLeft, false);
    // ensemble panel
    juce::Rectangle<float> pr { 826, 130, 350, 260 };
    g.setColour(panel()); g.fillRoundedRectangle(pr, 12.0f); g.setColour(edge()); g.drawRoundedRectangle(pr.reduced(0.5f), 12.0f, 1.0f);
    caption(g, { 844, 138, 300, 22 }, "ENSEMBLE", muted(), 13.0f, juce::Justification::centredLeft);
    g.setColour(edge()); g.drawHorizontalLine(284, 844, 1158);
    caption(g, { 844, 288, 300, 20 }, "NOTE SNAP", muted(), 13.0f, juce::Justification::centredLeft);
    juce::String snapTxt = e.proc.value(P_SNAP) > 0.5f ? "Notes lock to the key and scale" : "Off: follows your exact pitch";
    caption(g, { 844, 350, 320, 18 }, snapTxt, muted(), 11.5f, juce::Justification::centredLeft, false);
    // row captions for non-knob controls
    caption(g, { 24 + 152 + 156 + 108, 404, 132, 20 }, "OCTAVE", text(), 13.0f);
    const float stepsX = 24 + 152 + 156 + 108 + 140 + 108 + 108 + 108 + 166 + 6;
    caption(g, { stepsX, 404, 120, 20 }, "STEPS", text(), 13.0f);
    juce::String stepTxt = e.proc.value(P_STEPS) < 0.5f ? "Off: every moment" : "On host tempo";
    caption(g, { stepsX - 10, 500, 140, 20 }, stepTxt, muted(), 11.5f, juce::Justification::centred, false);
    caption(g, { 24 + 172, 576 + 102, 178, 18 }, "Dot lights when MIDI arrives", muted(), 11.5f, juce::Justification::centred, false);
    caption(g, { 24, 576 + 102, 160, 18 }, "Holds the last sung note", muted(), 11.5f, juce::Justification::centred, false);
    g.setColour(edge()); g.drawHorizontalLine(566, 24, 1176);
    caption(g, { 24, 724, 1152, 20 }, "PARTIALS = brightness   |   TALK = how much your vowels shape the sound   |   AIR = consonants in the instrument's colour",
            muted(), 12.0f, juce::Justification::centred, false);
}

void HuskEditor::Led::paint(juce::Graphics& g) {
    bool lit = ed && ed->midiGlow > 0;
    auto r = getLocalBounds().toFloat().reduced(1);
    g.setColour(lit ? purple() : bg()); g.fillEllipse(r);
    g.setColour(lit ? purple() : edge().brighter(0.3f)); g.drawEllipse(r, 1.2f);
}

void HuskEditor::timerCallback() {
    long long m = proc.midiCount.load();
    if (m != lastMidi) { lastMidi = m; midiGlow = 6; } else if (midiGlow > 0) --midiGlow;
    int prog = proc.getCurrentProgram();
    if (prog != lastProgramShown) { presetBox.setSelectedId(prog + 1, juce::dontSendNotification); lastProgramShown = prog; }
    bool sync = proc.value(P_PHASE_SYNC) > 0.5f;
    kPhaseRate->setAlpha(sync ? 0.45f : 1.0f); divBox.setEnabled(sync); divBox.setAlpha(sync ? 1.0f : 0.45f);
    bool snap = proc.value(P_SNAP) > 0.5f; keyBox.setAlpha(snap ? 1.0f : 0.5f); scaleBox.setAlpha(snap ? 1.0f : 0.5f);
    midiLed.repaint(); vowels->repaint(); models->repaint(); octave->repaint();
    content.repaint(0, 560, kW, 200); content.repaint(0, 0, kW, 60); content.repaint(826, 280, 350, 110); content.repaint(0, 390, kW, 170);
}

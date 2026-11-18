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

// ------------------------------------------------------------------ partials display
void PartialsView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(panel()); g.fillRoundedRectangle(r, 12.0f);
    g.setColour(edge()); g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
    auto in = r.reduced(18, 14);
    auto top = in.removeFromTop(22);
    caption(g, top, "PARTIALS", muted(), 13.0f, juce::Justification::centredLeft);
    const bool v = proc.voiced.load();
    float f = proc.f0.load();
    juce::String note = "No clear note";
    if (v && f > 0) {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        int midi = (int)std::lround(69 + 12 * std::log2(f / 440.0)); midi = juce::jlimit(0, 127, midi);
        note = juce::String(names[midi % 12]) + juce::String(midi / 12 - 1) + "   " + juce::String(f, 1) + " Hz";
    }
    caption(g, top, note, v ? text() : muted(), 14.0f, juce::Justification::centredRight);
    auto bottom = in.removeFromBottom(20);
    const int keepK = juce::jlimit(1, 64, (int)std::lround(proc.value(P_PARTIALS)));
    juce::String foot = "Keeping " + juce::String(keepK) + (keepK == 1 ? " tone" : " tones");
    if (proc.value(P_FREEZE) > 0.5f) foot += "   |   FROZEN";
    if (proc.value(P_CHORD) > 0.5f) { int h = proc.heldNotes.load(); foot += "   |   CHORD: " + (h > 0 ? juce::String(h) + (h == 1 ? " note" : " notes") : juce::String("waiting for MIDI")); }
    caption(g, bottom, foot, muted(), 13.0f, juce::Justification::centredLeft, false);
    in.removeFromBottom(6);
    // strands
    const float base = in.getBottom(), hMax = in.getHeight() - 4;
    g.setColour(edge()); g.drawHorizontalLine((int)base, in.getX(), in.getRight());
    bool any = false; for (int k = 0; k < 64; ++k) any |= proc.dispAmp[(size_t)k].load() > 1e-6f;
    for (int k = 0; k < 64; ++k) {
        float x = in.getX() + (k + 0.5f) * in.getWidth() / 64.0f;
        bool kept = k < keepK;
        float a = proc.dispAmp[(size_t)k].load();
        if (any && kept && a > 1e-6f) {
            float dbv = juce::jlimit(-60.0f, 0.0f, 20.0f * std::log10(a));
            float h = juce::jmax(4.0f, (dbv + 60.0f) / 60.0f * hMax);
            for (auto [w, al] : { std::pair<float, float>{ 9.0f, 0.08f }, { 5.0f, 0.18f }, { 2.2f, 1.0f } }) {
                g.setColour(purple().withAlpha(al)); g.drawLine(x, base, x, base - h, w);
            }
        } else if (kept) {
            g.setColour(purpleDim().withAlpha(0.55f)); g.drawLine(x, base, x, base - 10.0f, 2.0f);
        } else {
            g.setColour(track()); g.fillEllipse(x - 1.6f, base - 3.2f, 3.2f, 3.2f);
        }
    }
    if (!any) caption(g, in.withTrimmedBottom(in.getHeight() * 0.35f), "Waiting for a voice", muted(), 16.0f, juce::Justification::centred, false);
}

// ------------------------------------------------------------------ X-Y pad
juce::Rectangle<float> XYPad::padArea() const { return getLocalBounds().toFloat().reduced(16).withTrimmedTop(26).withTrimmedBottom(26); }
void XYPad::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(panel()); g.fillRoundedRectangle(r, 12.0f);
    g.setColour(edge()); g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, 1.0f);
    auto p = padArea();
    g.setColour(bg().brighter(0.03f)); g.fillRoundedRectangle(p, 14.0f);
    g.setColour(edge()); g.drawRoundedRectangle(p, 14.0f, 1.0f);
    g.setColour(track()); g.drawLine(p.getCentreX(), p.getY() + 8, p.getCentreX(), p.getBottom() - 8, 1.0f);
    g.drawLine(p.getX() + 8, p.getCentreY(), p.getRight() - 8, p.getCentreY(), 1.0f);
    auto tri = [&](float cx, float cy, int dir, juce::Colour c) {   // 0 up, 1 down, 2 left, 3 right
        juce::Path t; const float s = 4.5f;
        if (dir == 0) t.addTriangle(cx - s, cy + s * 0.6f, cx + s, cy + s * 0.6f, cx, cy - s * 0.8f);
        else if (dir == 1) t.addTriangle(cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, cx, cy + s * 0.8f);
        else if (dir == 2) t.addTriangle(cx + s * 0.6f, cy - s, cx + s * 0.6f, cy + s, cx - s * 0.8f, cy);
        else t.addTriangle(cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, cx + s * 0.8f, cy);
        g.setColour(c); g.fillPath(t);
    };
    auto head = juce::Rectangle<float>(r.getX() + 18, r.getY() + 8, r.getWidth() - 36, 18);
    caption(g, head, "GLIDE", muted(), 12.0f, juce::Justification::centredLeft); tri(head.getX() + 48, head.getCentreY(), 2, muted());
    caption(g, head, "GRAIN", text(), 13.0f); tri(head.getCentreX() + 30, head.getCentreY(), 0, text());
    caption(g, head, "WATER", text(), 13.0f, juce::Justification::centredRight); tri(head.getRight() - 56, head.getCentreY(), 3, text());
    auto foot = juce::Rectangle<float>(r.getX() + 18, r.getBottom() - 26, r.getWidth() - 36, 18);
    caption(g, foot, "SOFT", muted(), 12.0f); tri(foot.getCentreX() + 26, foot.getCentreY(), 1, muted());
    float wv = proc.value(P_WATER), gv = proc.value(P_GRAIN);
    float cx = p.getCentreX() + wv * (p.getWidth() * 0.5f - 12), cy = p.getCentreY() - gv * (p.getHeight() * 0.5f - 12);
    g.setColour(purple().withAlpha(0.15f)); g.fillEllipse(cx - 18, cy - 18, 36, 36);
    g.setColour(purple()); g.fillEllipse(cx - 9, cy - 9, 18, 18);
    g.setColour(bg()); g.fillEllipse(cx - 3, cy - 3, 6, 6);
}
void XYPad::setFrom(juce::Point<float> pt) {
    auto p = padArea();
    float x = juce::jlimit(-1.0f, 1.0f, (pt.x - p.getCentreX()) / (p.getWidth() * 0.5f - 12));
    float y = juce::jlimit(-1.0f, 1.0f, -(pt.y - p.getCentreY()) / (p.getHeight() * 0.5f - 12));
    for (auto [i, v] : { std::pair<int, float>{ P_WATER, x }, { P_GRAIN, y } }) { auto* prm = proc.param(i); prm->setValueNotifyingHost(prm->convertTo0to1(v)); }
    repaint();
}
void XYPad::mouseDown(const juce::MouseEvent& e) { proc.param(P_WATER)->beginChangeGesture(); proc.param(P_GRAIN)->beginChangeGesture(); dragging = true; setFrom(e.position); }
void XYPad::mouseDrag(const juce::MouseEvent& e) { if (dragging) setFrom(e.position); }
void XYPad::mouseUp(const juce::MouseEvent&) { if (dragging) { proc.param(P_WATER)->endChangeGesture(); proc.param(P_GRAIN)->endChangeGesture(); } dragging = false; }
void XYPad::mouseDoubleClick(const juce::MouseEvent&) { resetToCentre(); }
void XYPad::resetToCentre() { proc.setValue(P_WATER, 0.0f); proc.setValue(P_GRAIN, 0.0f); repaint(); }

// ------------------------------------------------------------------ model bar / segmented
void ModelBar::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat(); int sel = (int)std::lround(proc.value(P_MODEL));
    float w = (r.getWidth() - 6 * 8) / 7.0f;
    for (int i = 0; i < 7; ++i) {
        auto c = juce::Rectangle<float>(r.getX() + i * (w + 8), r.getY(), w, r.getHeight()).reduced(0.5f);
        bool on = i == sel;
        g.setColour(on ? purple() : panel()); g.fillRoundedRectangle(c, 8.0f);
        g.setColour(on ? purple() : edge()); g.drawRoundedRectangle(c, 8.0f, 1.2f);
        caption(g, c, juce::String(husk::modelName(i)).toUpperCase(), on ? bg() : text(), 15.0f);
    }
}
void ModelBar::mouseDown(const juce::MouseEvent& e) {
    float w = (getWidth() - 6 * 8) / 7.0f; int i = juce::jlimit(0, 6, (int)(e.position.x / (w + 8)));
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
    partials = std::make_unique<ui::PartialsView>(p); content.addAndMakeVisible(*partials);
    xy = std::make_unique<ui::XYPad>(p); content.addAndMakeVisible(*xy);
    models = std::make_unique<ui::ModelBar>(p); content.addAndMakeVisible(*models);
    octave = std::make_unique<ui::Segmented>(p, P_OCTAVE, juce::StringArray { "-1", "0", "+1" }); content.addAndMakeVisible(*octave);
    auto& s = p.apvts;
    kPartials = std::make_unique<ui::Knob>(s, "partials", "PARTIALS", true); kPartials->labelSize = 15.0f;
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
    for (auto* k : { kPartials.get(), kPitch.get(), kFormant.get(), kPhase.get(), kPhaseRate.get(), kAir.get(), kShimmer.get(), kSpace.get(), kCrunch.get(), kMix.get(), kOut.get() })
        content.addAndMakeVisible(*k);
    int c = 0; auto* pr = husk::presets(c);
    for (int i = 0; i < c; ++i) presetBox.addItem(pr[i].name, i + 1);
    presetBox.setSelectedId(p.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { int i = presetBox.getSelectedId() - 1; if (i >= 0) { proc.setCurrentProgram(i); lastProgramShown = i; } };
    stepsBox.addItemList({ "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    divBox.addItemList({ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    for (auto* cb : { &presetBox, &stepsBox, &divBox }) content.addAndMakeVisible(*cb);
    stAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "steps", stepsBox);
    dvAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(s, "phase_div", divBox);
    for (auto* b : { &abBtn, &bypassBtn, &freezeBtn, &chordBtn, &syncBtn }) { b->setClickingTogglesState(true); content.addAndMakeVisible(*b); }
    midiLed.ed = this; midiLed.setInterceptsMouseClicks(false, false); content.addAndMakeVisible(midiLed);
    freezeBtn.getProperties().set("accent", true); freezeBtn.getProperties().set("onText", "FROZEN"); freezeBtn.getProperties().set("size", 18.0f);
    chordBtn.getProperties().set("size", 15.0f); syncBtn.getProperties().set("size", 12.0f);
    abBtn.getProperties().set("onText", "A/B: DRY"); bypassBtn.getProperties().set("onText", "BYPASSED");
    abAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "ab", abBtn);
    byAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "bypass", bypassBtn);
    frAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "freeze", freezeBtn);
    chAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "chord", chordBtn);
    syAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(s, "phase_sync", syncBtn);
    abBtn.setTooltip("Hear the untouched voice (time-aligned) to compare");

    setOpaque(true);
    setResizable(true, true);
    setResizeLimits(770, 490, 2200, 1400);
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
    presetBox.setBounds(392, 20, 230, 32);
    abBtn.setBounds(866, 20, 98, 32); bypassBtn.setBounds(974, 20, 102, 32);
    models->setBounds(24, 72, 1052, 44);
    partials->setBounds(24, 130, 736, 250);
    xy->setBounds(772, 130, 304, 250);
    // row 1
    int y = 396, h = 144, x = 24;
    kPartials->setBounds(x, y - 4, 170, h + 8); x += 180;
    kPitch->setBounds(x, y, 118, h); x += 128;
    octave->setBounds(x + 4, y + 52, 142, 40); x += 160;
    kFormant->setBounds(x, y, 118, h); x += 128;
    kPhase->setBounds(x, y, 118, h); x += 128;
    kPhaseRate->setBounds(x, y, 150, h - 30);
    syncBtn.setBounds(x + 4, y + h - 28, 60, 26); divBox.setBounds(x + 70, y + h - 28, 84, 26); x += 172;
    stepsBox.setBounds(x + 6, y + 52, 134, 34);
    // row 2
    y = 556; h = 130; x = 24;
    freezeBtn.setBounds(x, y + 26, 150, 72); x += 162;
    chordBtn.setBounds(x, y + 26, 168, 72); midiLed.setBounds(x + 168 - 24, y + 26 + 10, 14, 14); x += 182;
    for (auto* k : { kAir.get(), kShimmer.get(), kSpace.get(), kCrunch.get(), kMix.get(), kOut.get() }) { k->setBounds(x, y, 112, h); x += 117; }
}

void HuskEditor::Content::paint(juce::Graphics& g) {
    auto& e = *ed;
    g.fillAll(bg());
    caption(g, { 24, 14, 120, 44 }, "HUSK", purple(), 36.0f, juce::Justification::centredLeft);
    caption(g, { 146, 28, 220, 20 }, "VOICE INTO INSTRUMENT", muted(), 12.0f, juce::Justification::centredLeft, false);
    caption(g, { 324, 20, 64, 32 }, "PRESET", muted(), 12.0f, juce::Justification::centredRight);
    double ms = e.proc.latencyNow() / juce::jmax(1.0, e.proc.sampleRateNow()) * 1000.0;
    caption(g, { 636, 20, 210, 32 }, "LATENCY " + juce::String(ms, 1) + " ms", muted(), 12.0f, juce::Justification::centredLeft, false);
    // row captions for non-knob controls
    caption(g, { 24 + 180 + 128, 396, 160, 20 }, "OCTAVE", text(), 13.0f);
    caption(g, { 24 + 180 + 128 + 160 + 128 + 128 + 172, 396, 146, 20 }, "STEPS", text(), 13.0f);
    juce::String stepTxt = e.proc.value(P_STEPS) < 0.5f ? "Off: follows every moment" : "Locked to host tempo";
    caption(g, { 24 + 180 + 128 + 160 + 128 + 128 + 172, 492, 146, 20 }, stepTxt, muted(), 11.5f, juce::Justification::centred, false);
    caption(g, { 24 + 162, 556 + 102, 168, 18 }, "Dot lights when MIDI arrives", muted(), 11.5f, juce::Justification::centred, false);
    caption(g, { 24, 556 + 102, 150, 18 }, "Hold as an endless pad", muted(), 11.5f, juce::Justification::centred, false);
    g.setColour(edge()); g.drawHorizontalLine(548, 24, 1076);
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
    midiLed.repaint(); partials->repaint(); xy->repaint(); models->repaint(); octave->repaint();
    content.repaint(0, 540, kW, 160); content.repaint(0, 0, kW, 60); content.repaint(600, 390, 500, 130);
}

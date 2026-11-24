#include "PluginProcessor.h"
#include "PluginEditor.h"

const char* const kParamIds[kNumParams] = {
    "model", "partials", "talk", "air", "pitch", "octave", "formant", "phase", "phase_rate", "phase_sync", "phase_div",
    "steps", "freeze", "chord", "snap", "key", "scale", "glide", "unison", "detune", "width", "drift",
    "shimmer", "space", "crunch", "mix", "out", "ab", "bypass"
};

int mapV1Model(int v1) {
    // v1: Sine, Saw, Square, Glass, Flute, Choir, String
    static const int m[7] = { husk::Ghost, husk::SawLead, husk::Reed, husk::Glass, husk::Organ, husk::Choir, husk::Bowed };
    return m[juce::jlimit(0, 6, v1)];
}

namespace {
using APF = juce::AudioParameterFloat; using APB = juce::AudioParameterBool; using APC = juce::AudioParameterChoice;
// Switches are two-step choices so the host value is always exactly 0 or 1 (state restores bit-exact).
static std::unique_ptr<APC> onOff(const char* id, const char* name) {
    return std::make_unique<APC>(juce::ParameterID { id, 1 }, name, juce::StringArray { "Off", "On" }, 0);
}

juce::String pct(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }
juce::String signedSt(float v, int) { auto r = std::round(v * 10.0f) / 10.0f; return (r > 0 ? "+" : "") + juce::String(r, std::abs(r - std::round(r)) < 0.05f ? 0 : 1) + " st"; }
std::unique_ptr<APF> fl(const char* id, const char* name, juce::NormalisableRange<float> r, float def, std::function<juce::String(float, int)> txt) {
    return std::make_unique<APF>(juce::ParameterID { id, 1 }, name, r, def, juce::AudioParameterFloatAttributes().withStringFromValueFunction(std::move(txt)));
}
}

juce::AudioProcessorValueTreeState::ParameterLayout HuskProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<APC>(juce::ParameterID { "model", 1 }, "Model", juce::StringArray { "Saw Lead", "Reed", "Glass", "Bowed String", "Choir", "Organ", "Pluck", "Sub", "Ghost" }, 0));
    { juce::NormalisableRange<float> r(1.0f, 64.0f, 0.01f); r.setSkewForCentre(12.0f);
      l.add(fl("partials", "Partials (brightness)", r, 64.0f, [](float v, int) { return v >= 63.5f ? juce::String("All") : juce::String(juce::roundToInt(v)); })); }
    l.add(fl("talk", "Talk", { 0.0f, 1.0f, 0.001f }, 0.6f, pct));
    l.add(fl("air", "Air", { 0.0f, 1.0f, 0.001f }, 0.15f, pct));
    l.add(fl("pitch", "Pitch", { -12.0f, 12.0f, 1.0f }, 0.0f, signedSt));
    l.add(std::make_unique<APC>(juce::ParameterID { "octave", 1 }, "Octave", juce::StringArray { "-1", "0", "+1" }, 1));
    l.add(fl("formant", "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, signedSt));
    l.add(fl("phase", "Phase", { 0.0f, 1.0f, 0.001f }, 0.0f, pct));
    { juce::NormalisableRange<float> r(0.05f, 20.0f, 0.001f); r.setSkewForCentre(1.0f);
      l.add(fl("phase_rate", "Phase Rate", r, 1.0f, [](float v, int) { return juce::String(v, v < 1 ? 2 : 1) + " Hz"; })); }
    l.add(onOff("phase_sync", "Phase Rate Sync"));
    l.add(std::make_unique<APC>(juce::ParameterID { "phase_div", 1 }, "Phase Rate Note", juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    l.add(std::make_unique<APC>(juce::ParameterID { "steps", 1 }, "Steps", juce::StringArray { "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 0));
    l.add(onOff("freeze", "Freeze"));
    l.add(onOff("chord", "Chord (MIDI)"));
    l.add(onOff("snap", "Note Snap"));
    l.add(std::make_unique<APC>(juce::ParameterID { "key", 1 }, "Key", juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add(std::make_unique<APC>(juce::ParameterID { "scale", 1 }, "Scale", juce::StringArray { "Chromatic", "Major", "Minor", "Pentatonic", "Minor Pent." }, 0));
    { juce::NormalisableRange<float> r(0.0f, 1000.0f, 1.0f); r.setSkewForCentre(120.0f);
      l.add(fl("glide", "Glide", r, 0.0f, [](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; })); }
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "unison", 1 }, "Unison", 1, 7, 3));
    l.add(fl("detune", "Detune", { 0.0f, 1.0f, 0.001f }, 0.35f, pct));
    l.add(fl("width", "Width", { 0.0f, 1.0f, 0.001f }, 0.6f, pct));
    l.add(fl("drift", "Drift", { 0.0f, 1.0f, 0.001f }, 0.4f, pct));
    l.add(fl("shimmer", "Shimmer", { 0.0f, 1.0f, 0.001f }, 0.0f, pct));
    l.add(fl("space", "Space", { 0.0f, 1.0f, 0.001f }, 0.0f, pct));
    l.add(fl("crunch", "Crunch", { 0.0f, 1.0f, 0.001f }, 0.0f, pct));
    l.add(fl("mix", "Mix", { 0.0f, 1.0f, 0.001f }, 1.0f, pct));
    l.add(fl("out", "Out", { -24.0f, 12.0f, 0.1f }, 0.0f, [](float v, int) { return juce::String(v, 1) + " dB"; }));
    l.add(onOff("ab", "A/B (dry voice)"));
    l.add(std::make_unique<APB>(juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

HuskProcessor::HuskProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HUSK", layout()) {
    for (int i = 0; i < kNumParams; ++i) {
        params[(size_t)i] = apvts.getParameter(kParamIds[i]);
        raw[(size_t)i] = apvts.getRawParameterValue(kParamIds[i]);
        jassert(params[(size_t)i] != nullptr);
    }
    for (auto& a : dispBands) a.store(0);
    eng.prepare(44100.0, 512);
    setLatencySamples(eng.latencySamples());
}

void HuskProcessor::setValue(int i, float v) {
    auto* p = params[(size_t)i];
    p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
}

husk::Params HuskProcessor::readParams() const {
    husk::Params p;
    p.model = juce::jlimit(0, husk::kNumModels - 1, (int)std::lround(value(P_MODEL)));
    p.partials = value(P_PARTIALS); p.talk = value(P_TALK); p.air = value(P_AIR); p.pitch = value(P_PITCH);
    p.octave = (int)std::lround(value(P_OCTAVE)) - 1; p.formant = value(P_FORMANT);
    p.phase = value(P_PHASE); p.phaseRate = value(P_PHASE_RATE); p.phaseSync = value(P_PHASE_SYNC) > 0.5f;
    p.phaseDiv = (int)std::lround(value(P_PHASE_DIV)); p.steps = (int)std::lround(value(P_STEPS));
    p.freeze = value(P_FREEZE) > 0.5f; p.chord = value(P_CHORD) > 0.5f;
    p.snap = value(P_SNAP) > 0.5f; p.key = (int)std::lround(value(P_KEY)); p.scale = (int)std::lround(value(P_SCALE)); p.glideMs = value(P_GLIDE);
    p.unison = (int)std::lround(value(P_UNISON)); p.detune = value(P_DETUNE); p.width = value(P_WIDTH); p.drift = value(P_DRIFT);
    p.shimmer = value(P_SHIMMER); p.space = value(P_SPACE); p.crunch = value(P_CRUNCH);
    p.mix = value(P_MIX); p.outDb = value(P_OUT); p.ab = value(P_AB) > 0.5f; p.bypass = value(P_BYPASS) > 0.5f;
    return p;
}

void HuskProcessor::prepareToPlay(double fs, int block) {
    sr = fs > 0 ? fs : 44100.0;
    eng.setParams(readParams());
    eng.prepare(sr, block);
    monoR.assign((size_t)juce::jmax(block, 4096) , 0.0f);
    setLatencySamples(eng.latencySamples());
}

void HuskProcessor::reset() { eng.reset(); }

bool HuskProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in.isDisabled() || out.isDisabled() || in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void HuskProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { run(b, m, false); }
void HuskProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { run(b, m, true); }

void HuskProcessor::run(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi, bool forceBypass) {
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples(), nCh = buf.getNumChannels();
    for (int c = getTotalNumInputChannels(); c < nCh; ++c) buf.clear(c, 0, n);
    if (n <= 0 || nCh <= 0) { midi.clear(); return; }
    auto p = readParams();
    if (forceBypass) p.bypass = true;
    eng.setParams(p);

    husk::HostTime ht;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto bpm = pos->getBpm()) { ht.bpm = *bpm; ht.valid = true; }
            if (auto q = pos->getPpqPosition()) ht.ppq = *q;
            ht.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }

    float* L = buf.getWritePointer(0);
    if ((int)monoR.size() < n) monoR.resize((size_t)n);   // only if a host breaks its promised block size
    float* R = nCh > 1 ? buf.getWritePointer(1) : monoR.data();
    if (nCh == 1) std::copy(L, L + n, R);
    float inPk = 0; for (int i = 0; i < n; ++i) inPk = std::max(inPk, std::max(std::fabs(L[i]), std::fabs(R[i])));

    int pos = 0;
    auto renderTo = [&](int end) {
        if (end <= pos) return;
        husk::HostTime t = ht; t.ppq += pos / sr * ht.bpm / 60.0;
        eng.setTime(t);
        eng.process(L + pos, R + pos, end - pos);
        pos = end;
    };
    for (const auto meta : midi) {
        auto msg = meta.getMessage();
        int at = juce::jlimit(0, n, meta.samplePosition);
        renderTo(at);
        if (msg.isNoteOn()) eng.noteOn(msg.getNoteNumber());
        else if (msg.isNoteOff()) eng.noteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) eng.allNotesOff();
        if (msg.isNoteOnOrOff()) midiCount.fetch_add(1, std::memory_order_relaxed);
    }
    renderTo(n);
    midi.clear();

    float outPk = 0; for (int i = 0; i < n; ++i) outPk = std::max(outPk, std::max(std::fabs(L[i]), std::fabs(R[i])));
    inLevel.store(inPk, std::memory_order_relaxed); outLevel.store(outPk, std::memory_order_relaxed);
    for (int k = 0; k < husk::kVB; ++k) dispBands[(size_t)k].store(eng.gDisp[k], std::memory_order_relaxed);
    f0.store(eng.dispF0, std::memory_order_relaxed); voiced.store(eng.dispVoiced, std::memory_order_relaxed);
    toneLevel.store(eng.dispTone, std::memory_order_relaxed); consLevel.store(eng.dispCons, std::memory_order_relaxed);
    noteNow.store(eng.dispNote, std::memory_order_relaxed); holding.store(eng.isHolding(), std::memory_order_relaxed);
    heldNotes.store(eng.heldNotes(), std::memory_order_relaxed);
}

int HuskProcessor::getNumPrograms() { int c = 0; husk::presets(c); return c; }
const juce::String HuskProcessor::getProgramName(int i) {
    int c = 0; auto* p = husk::presets(c); return (i >= 0 && i < c) ? juce::String(p[i].name) : juce::String();
}
void HuskProcessor::setCurrentProgram(int i) {
    int c = 0; auto* pr = husk::presets(c);
    if (i < 0 || i >= c) return;
    currentProgram = i;
    for (int k = 0; k < kNumParams; ++k) {
        if (k == P_BYPASS) continue;
        auto* p = params[(size_t)k]; float def = p->getDefaultValue();
        for (int j = 0; j < pr[i].n; ++j) if (juce::String(pr[i].v[j].id) == kParamIds[k]) def = p->convertTo0to1(pr[i].v[j].value + (k == P_OCTAVE ? 1.0f : 0.0f));   // presets store octave as -1/0/+1; the choice is 0..2
        p->beginChangeGesture(); p->setValueNotifyingHost(def); p->endChangeGesture();
    }
}

void HuskProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram, nullptr);
    state.setProperty("husk_version", 2, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}
void HuskProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            auto t = juce::ValueTree::fromXml(*xml);
            if (!t.isValid()) return;
            if ((int)t.getProperty("husk_version", 1) < 2) {
                // v1 session: map the old model list, drop the removed Water/Grain pad, keep everything else
                for (int i = t.getNumChildren(); --i >= 0;) {
                    auto c = t.getChild(i);
                    auto id = c.getProperty("id").toString();
                    if (id == "model") c.setProperty("value", mapV1Model((int)std::lround((double)c.getProperty("value", 0.0))), nullptr);
                    if (id == "water" || id == "grain") t.removeChild(i, nullptr);
                }
                t.setProperty("program", 0, nullptr);
            }
            currentProgram = juce::jlimit(0, juce::jmax(0, getNumPrograms() - 1), (int)t.getProperty("program", 0));
            apvts.replaceState(t);
        }
}

juce::AudioProcessorEditor* HuskProcessor::createEditor() { return new HuskEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HuskProcessor(); }

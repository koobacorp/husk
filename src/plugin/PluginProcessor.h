#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "HuskEngine.h"
#include "HuskPresets.h"

enum ParamIndex : int {
    P_MODEL = 0, P_PARTIALS, P_TALK, P_AIR, P_PITCH, P_OCTAVE, P_FORMANT, P_PHASE, P_PHASE_RATE, P_PHASE_SYNC, P_PHASE_DIV,
    P_STEPS, P_FREEZE, P_CHORD, P_SNAP, P_KEY, P_SCALE, P_GLIDE, P_UNISON, P_DETUNE, P_WIDTH, P_DRIFT,
    P_SHIMMER, P_SPACE, P_CRUNCH, P_MIX, P_OUT, P_AB, P_BYPASS,
    kNumParams
};
// v1 sessions stored the old 7-model list; this maps them onto the closest v2 model.
int mapV1Model(int v1);
extern const char* const kParamIds[kNumParams];

class HuskProcessor : public juce::AudioProcessor {
public:
    HuskProcessor();
    ~HuskProcessor() override = default;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return params[P_BYPASS]; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "HUSK"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* param(int i) const { return params[(size_t)i]; }
    float value(int i) const { return raw[(size_t)i]->load(std::memory_order_relaxed); }
    void setValue(int i, float realValue);
    husk::Params readParams() const;

    // telemetry for the editor
    std::array<std::atomic<float>, husk::kVB> dispBands {};
    std::atomic<float> f0 { 0 }, inLevel { 0 }, outLevel { 0 }, toneLevel { 0 }, consLevel { 0 };
    std::atomic<bool> voiced { false }, holding { false };
    std::atomic<int> heldNotes { 0 }, noteNow { -1 };
    std::atomic<long long> midiCount { 0 };
    int latencyNow() const { return getLatencySamples(); }
    double sampleRateNow() const { return sr; }
    const husk::Engine& engine() const { return eng; }

private:
    void run(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool forceBypass);
    husk::Engine eng;
    std::array<juce::RangedAudioParameter*, kNumParams> params {};
    std::array<std::atomic<float>*, kNumParams> raw {};
    double sr = 44100; int currentProgram = 0;
    std::vector<float> monoR;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HuskProcessor)
};

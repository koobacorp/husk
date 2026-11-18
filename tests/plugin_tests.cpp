// HUSK plugin tests: JUCE processor (parameters, buses, latency, MIDI, state, presets, every control,
// fuzz) and the editor (resizing, screenshots).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestUtil.h"
#include <chrono>
#include <cstdio>
using tu::db; using tu::cents;

struct PlayHead : juce::AudioPlayHead {
    double bpm = 120, ppq = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setBpm(bpm); p.setPpqPosition(ppq); p.setIsPlaying(playing); return p;
    }
};

static std::vector<float> vowel(double f0, double secs, double sr, double gain = 1.0) {
    std::vector<float> x((size_t)(secs * sr));
    auto res = [](double f, double F, double bw) { double r = f / F; return 1.0 / std::sqrt((1 - r * r) * (1 - r * r) + (f * bw / (F * F)) * (f * bw / (F * F))); };
    std::vector<double> a; for (int k = 1; k * f0 < 0.42 * sr && k <= 64; ++k) { double f = k * f0; a.push_back(0.02 / k * res(f, 700, 90) * res(f, 1220, 120) * res(f, 2600, 170)); }
    double s = 0; for (double v : a) s += v * v; double sc = 0.25 / std::sqrt(s / 2); for (double& v : a) v *= sc * gain;
    for (size_t i = 0; i < x.size(); ++i) { double t = i / sr, v = 0; for (size_t k = 0; k < a.size(); ++k) v += a[k] * std::sin(husk::kTwoPi * f0 * (k + 1) * t + 0.3 * k); x[i] = (float)v; }
    return x;
}

struct Rig {
    HuskProcessor p; PlayHead head; double sr; int block;
    std::vector<float> L, R;
    Rig(double rate = 44100, int bs = 512, bool mono = false) : sr(rate), block(bs) {
        juce::AudioProcessor::BusesLayout lay;
        lay.inputBuses.add(mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        lay.outputBuses.add(mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        p.setBusesLayout(lay);
        p.setPlayHead(&head);
        p.setRateAndBufferSizeDetails(sr, block);
        p.prepareToPlay(sr, block);
    }
    ~Rig() { p.setPlayHead(nullptr); }
    // events: (sample, midi message)
    double run(const std::vector<float>& in, std::vector<std::pair<int, juce::MidiMessage>> ev = {}, std::function<void(Rig&, int)> perBlock = {}) {
        L.clear(); R.clear(); int n = (int)in.size(), pos = 0; double t = 0;
        int nCh = p.getTotalNumOutputChannels();
        juce::AudioBuffer<float> buf(nCh, block);
        while (pos < n) {
            if (perBlock) perBlock(*this, pos);
            int m = std::min(block, n - pos);
            buf.setSize(nCh, m, false, false, true);
            for (int c = 0; c < nCh; ++c) for (int i = 0; i < m; ++i) buf.setSample(c, i, in[(size_t)(pos + i)]);
            juce::MidiBuffer midi;
            for (auto& e : ev) if (e.first >= pos && e.first < pos + m) midi.addEvent(e.second, e.first - pos);
            auto t0 = std::chrono::steady_clock::now();
            p.processBlock(buf, midi);
            t += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            L.insert(L.end(), buf.getReadPointer(0), buf.getReadPointer(0) + m);
            R.insert(R.end(), buf.getReadPointer(nCh > 1 ? 1 : 0), buf.getReadPointer(nCh > 1 ? 1 : 0) + m);
            head.ppq += m / sr * head.bpm / 60.0; pos += m;
        }
        return t;
    }
    std::vector<float> mono() const { std::vector<float> m(L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5f * (L[i] + R[i]); return m; }
    void set(int i, float v) { p.param(i)->setValueNotifyingHost(p.param(i)->convertTo0to1(v)); }
};

static void savePng(juce::Component& c, const juce::String& name) {
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile(name);
    f.deleteFile();
    juce::FileOutputStream s(f);
    bool ok = s.openedOk() && juce::PNGImageFormat().writeImageToStream(img, s);
    CHECK(ok);
    std::printf("  screenshot %s %dx%d\n", name.toRawUTF8(), img.getWidth(), img.getHeight());
}

int main(int, char**) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    std::puts("HUSK plugin tests");

    { std::puts("- identity, buses, parameters");
      Rig r;
      CHECK(r.p.getName() == "HUSK");
      CHECK(r.p.acceptsMidi() && !r.p.producesMidi() && !r.p.isMidiEffect());
      CHECK((int)r.p.getParameters().size() == kNumParams);
      CHECK(r.p.getBypassParameter() == r.p.param(P_BYPASS));
      int automatable = 0, texts = 0, okRound = 0;
      for (auto* prm : r.p.getParameters()) { automatable += prm->isAutomatable(); texts += prm->getCurrentValueAsText().isNotEmpty(); }
      CHECK(automatable == kNumParams); CHECK(texts == kNumParams);
      juce::Random rnd(3);
      for (int i = 0; i < kNumParams; ++i) { auto* prm = r.p.param(i); float n = rnd.nextFloat(); prm->setValueNotifyingHost(n); float back = prm->getValue(); okRound += std::abs(prm->convertFrom0to1(back) - prm->convertFrom0to1(prm->convertTo0to1(prm->convertFrom0to1(n)))) < 1e-3f; }
      CHECK(okRound == kNumParams);
      auto lay = [](juce::AudioChannelSet i, juce::AudioChannelSet o) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add(i); l.outputBuses.add(o); return l; };
      CHECK(r.p.checkBusesLayoutSupported(lay(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
      CHECK(r.p.checkBusesLayoutSupported(lay(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
      CHECK(!r.p.checkBusesLayoutSupported(lay(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono())));
      CHECK(!r.p.checkBusesLayoutSupported(lay(juce::AudioChannelSet::disabled(), juce::AudioChannelSet::stereo())));
      std::printf("  %d parameters, all automatable with text; mono and stereo insert layouts\n", kNumParams);
    }

    { std::puts("- latency reported to the host");
      for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
          Rig r(sr); int L = r.p.getLatencySamples();
          CHECK(L == r.p.engine().latencySamples()); CHECK(L / sr * 1000.0 <= 25.0);
          // measured with an impulse at Mix 0 through the real processBlock
          r.set(P_MIX, 0); std::vector<float> x((size_t)(0.5 * sr), 0.0f); x[1234] = 0.5f; r.run(x);
          int at = tu::argmaxAbs(r.L);
          std::printf("  %6.0f Hz: reported %d samples (%.2f ms), dry impulse arrives at +%d\n", sr, L, L / sr * 1000, at - 1234);
          CHECK(at - 1234 == L);
      } }

    { std::puts("- Mix 0 null and Bypass through processBlock (stereo and mono)");
      for (bool mono : { false, true }) {
          Rig r(48000, 480, mono); r.set(P_MIX, 0); r.set(P_CRUNCH, 1); r.set(P_SPACE, 1); r.set(P_SHIMMER, 1); r.set(P_MODEL, 5);
          husk::Rng g(4); std::vector<float> x(48000); for (auto& v : x) v = 0.4f * g.bi();
          r.run(x); int L = r.p.getLatencySamples(); double md = 0;
          for (size_t i = (size_t)L; i < x.size(); ++i) md = std::max(md, (double)std::fabs(r.L[i] - x[i - (size_t)L]));
          CHECKM(md == 0.0, "%g", md);
          Rig b(48000, 480, mono); b.set(P_BYPASS, 1); b.run(x); double mb = 0;
          for (size_t i = (size_t)L; i < x.size(); ++i) mb = std::max(mb, (double)std::fabs(b.L[i] - x[i - (size_t)L]));
          CHECKM(mb == 0.0, "%g", mb);
          juce::AudioBuffer<float> buf(mono ? 1 : 2, 480); juce::MidiBuffer mm; Rig c(48000, 480, mono);
          std::vector<float> outB; for (int pos = 0; pos + 480 <= (int)x.size(); pos += 480) { for (int ch = 0; ch < buf.getNumChannels(); ++ch) for (int i = 0; i < 480; ++i) buf.setSample(ch, i, x[(size_t)(pos + i)]); c.p.processBlockBypassed(buf, mm); outB.insert(outB.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 480); }
          double mc = 0; for (size_t i = (size_t)L; i < outB.size(); ++i) mc = std::max(mc, (double)std::fabs(outB[i] - x[i - (size_t)L]));
          CHECKM(mc == 0.0, "%g", mc);
          std::printf("  %s: mix-0 diff %g, bypass param diff %g, host bypass diff %g (all time-aligned to latency)\n", mono ? "mono" : "stereo", md, mb, mc);
      } }

    { std::puts("- MIDI Chord mode through processBlock (notes arrive mid-block)");
      Rig r; r.set(P_CHORD, 1);
      auto x = vowel(220, 2.5, 44100);
      std::vector<std::pair<int, juce::MidiMessage>> ev = { { 300, juce::MidiMessage::noteOn(1, 48, 0.8f) }, { 301, juce::MidiMessage::noteOn(1, 52, 0.8f) }, { 777, juce::MidiMessage::noteOn(1, 55, 0.8f) },
                                                            { 60000, juce::MidiMessage::noteOff(1, 52) } };
      r.run(x, ev); auto m = r.mono();
      tu::Spec s; s.run(m, 8000, 16384, 44100); double tot = std::sqrt(s.bandPower(60, 10000)), wc = 0;
      for (int n : { 48, 52, 55 }) { double f = 440 * std::pow(2.0, (n - 69) / 12.0), fr; double a = s.peak(f, 8, fr); wc = std::max(wc, std::fabs(cents(fr, f))); CHECK(db(a / tot) > -25); }
      tu::Spec s2; s2.run(m, 75000, 16384, 44100);
      double e3 = db(s2.amp(164.81, 4) / std::sqrt(s2.bandPower(60, 10000)));
      std::printf("  chord tones within %.3f cents; after E3 note-off its level is %.1f dB; MIDI events seen %lld, held now %d\n", wc, e3, r.p.midiCount.load(), r.p.heldNotes.load());
      CHECK(wc < 2); CHECK(e3 < -40); CHECK(r.p.midiCount.load() == 4); CHECK(r.p.heldNotes.load() == 2);
    }

    { std::puts("- Steps follow the host tempo through the play head");
      Rig r(44100, 256); r.set(P_STEPS, 4); r.head.bpm = 120;
      std::vector<double> times; long long last = -1;
      auto x = vowel(200, 2.0, 44100);
      r.run(x, {}, [&](Rig& rr, int pos) { long long u = rr.p.engine().recipeUpdates; if (u != last) { times.push_back(pos / 44100.0); last = u; } });
      double worst = 0; for (size_t i = 3; i < times.size(); ++i) worst = std::max(worst, std::fabs(times[i] - times[i - 1] - 0.125));  // [0] initial read, [1] start
      std::printf("  1/16 at 120 BPM: %zu updates, worst interval error %.2f ms\n", times.size(), worst * 1000);
      CHECK(times.size() >= 14); CHECK(worst <= 256 / 44100.0 + 1e-6);
    }

    { std::puts("- every control changes the sound (each against a neutral baseline)");
      auto x = vowel(196, 1.6, 44100); { auto y = vowel(262, 1.6, 44100); for (size_t i = (size_t)(0.8 * 44100); i < x.size(); ++i) x[i] = y[i]; }
      struct C { int idx; float v; std::function<void(Rig&)> ctx; };
      auto none = [](Rig&) {};
      std::vector<C> cs = {
          { P_MODEL, 1, none }, { P_PARTIALS, 3, none }, { P_AIR, 1, none }, { P_PITCH, 5, none }, { P_OCTAVE, 2, none }, { P_FORMANT, 4, none },
          { P_PHASE, 0.5f, none }, { P_PHASE_RATE, 9, [](Rig& r) { r.set(P_PHASE, 0.5f); } },
          { P_PHASE_SYNC, 1, [](Rig& r) { r.set(P_PHASE, 0.5f); } }, { P_PHASE_DIV, 0, [](Rig& r) { r.set(P_PHASE, 0.5f); r.set(P_PHASE_SYNC, 1); } },
          { P_STEPS, 2, none }, { P_WATER, 0.8f, none }, { P_GRAIN, 0.8f, none }, { P_FREEZE, 1, none }, { P_CHORD, 1, none },
          { P_SHIMMER, 0.6f, none }, { P_SPACE, 0.6f, none }, { P_CRUNCH, 0.6f, none }, { P_MIX, 0.3f, none }, { P_OUT, -6, none },
          { P_AB, 1, none }, { P_BYPASS, 1, none } };
      CHECK((int)cs.size() == kNumParams);
      std::vector<std::pair<int, juce::MidiMessage>> ev = { { 1000, juce::MidiMessage::noteOn(1, 57, 0.8f) }, { 1000, juce::MidiMessage::noteOn(1, 61, 0.8f) } };
      int changed = 0;
      for (auto& c : cs) {
          Rig base; base.p.setPlayHead(&base.head); c.ctx(base); base.run(x, ev);
          Rig alt; c.ctx(alt); alt.set(c.idx, c.v); alt.run(x, ev);
          double d = tu::rmsDiff(base.L, alt.L), ref = tu::rms(base.L) + 1e-9;
          bool fin = tu::finite(alt.L) && tu::finite(alt.R); float pk = tu::peak2(alt.L, alt.R);
          bool ok = d / ref > 1e-3 && fin && pk <= 0.97f;
          changed += ok;
          std::printf("  %-11s -> %-10s change %6.1f dB re signal, peak %.1f dBFS%s\n", kParamIds[c.idx], alt.p.param(c.idx)->getCurrentValueAsText().toRawUTF8(), db(d / ref), db(pk), ok ? "" : "   <-- FAIL");
          CHECK(ok);
      }
      std::printf("  %d of %d controls verified\n", changed, (int)cs.size());
    }

    { std::puts("- state save / restore and presets");
      Rig a; juce::Random rnd(17); std::vector<juce::String> want;
      for (int i = 0; i < kNumParams; ++i) { a.p.param(i)->setValueNotifyingHost(rnd.nextFloat()); want.push_back(a.p.param(i)->getCurrentValueAsText()); }
      juce::MemoryBlock mb; a.p.getStateInformation(mb);
      Rig b; b.p.setStateInformation(mb.getData(), (int)mb.getSize()); int same = 0;
      for (int i = 0; i < kNumParams; ++i) { bool ok = b.p.param(i)->getCurrentValueAsText() == want[(size_t)i]; same += ok; if (!ok) std::printf("  mismatch %s: %s vs %s\n", kParamIds[i], b.p.param(i)->getCurrentValueAsText().toRawUTF8(), want[(size_t)i].toRawUTF8()); }
      CHECK(same == kNumParams);
      int n = a.p.getNumPrograms(); CHECK(n == 11);
      std::printf("  state round trip %d/%d; %d presets:", same, kNumParams, n);
      for (int i = 0; i < n; ++i) {
          Rig r; r.p.setCurrentProgram(i); CHECK(r.p.getCurrentProgram() == i);
          std::printf(" %s%s", r.p.getProgramName(i).toRawUTF8(), i + 1 < n ? "," : "\n");
          r.run(vowel(220, 0.8, 44100)); CHECK(tu::finite(r.L)); CHECK(tu::peak2(r.L, r.R) <= 0.97f);
      }
      Rig r; r.p.setCurrentProgram(10); CHECK(std::lround(r.p.value(P_PARTIALS)) == 1); CHECK(std::lround(r.p.value(P_MODEL)) == 0);
      r.p.setCurrentProgram(2); CHECK(std::lround(r.p.value(P_MODEL)) == 3); CHECK(std::lround(r.p.value(P_PARTIALS)) == 40);
      r.p.setCurrentProgram(0); CHECK(std::lround(r.p.value(P_PARTIALS)) == 64); CHECK(r.p.value(P_SPACE) == 0.0f); CHECK(r.p.value(P_MIX) == 1.0f);
    }

    { std::puts("- random automation + MIDI fuzz through processBlock (20 s, random block sizes)");
      Rig r(48000, 2048); juce::Random rnd(5);
      auto x = vowel(180, 20, 48000, 2.5); for (size_t i = 0; i < x.size(); i += 48000 * 3) for (size_t j = i; j < std::min(x.size(), i + 24000); ++j) x[j] = 0;
      std::vector<std::pair<int, juce::MidiMessage>> ev;
      for (int i = 0; i < 400; ++i) ev.push_back({ rnd.nextInt((int)x.size()), rnd.nextBool() ? juce::MidiMessage::noteOn(1, 36 + rnd.nextInt(48), 0.7f) : juce::MidiMessage::noteOff(1, 36 + rnd.nextInt(48)) });
      std::sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.first < b.first; });
      double t = r.run(x, ev, [&](Rig& rr, int) { rr.block = 1 + rnd.nextInt(2048); if (rnd.nextFloat() < 0.3f) for (int i = 0; i < kNumParams; ++i) if (i != P_BYPASS && i != P_AB && rnd.nextFloat() < 0.3f) rr.p.param(i)->setValueNotifyingHost(rnd.nextFloat()); rr.head.bpm = 60 + rnd.nextInt(140); rr.head.playing = rnd.nextBool(); });
      auto tail = std::vector<float>(48000, 0.0f); Rig& rr = r; (void)rr;
      float tp = std::max(tu::truePeak(r.L), tu::truePeak(r.R));
      std::printf("  finite %d, subnormal %d, true peak %.2f dBTP, %.1f%% of real time\n", tu::finite(r.L) && tu::finite(r.R), tu::hasSubnormal(r.L) || tu::hasSubnormal(r.R), db(tp), t / 20.0 * 100);
      CHECK(tu::finite(r.L) && tu::finite(r.R)); CHECK(!tu::hasSubnormal(r.L)); CHECK(tp <= 1.0f);
    }

    { std::puts("- CPU through processBlock (default preset, 512-sample blocks)");
      Rig r; auto x = vowel(165, 10, 44100); double t = r.run(x);
      std::printf("  default: %.2f%% of one core\n", t / 10 * 100); CHECK(t / 10 < 0.25);
    }

    { std::puts("- editor: resizable, every state renders, screenshots");
      Rig r; auto* ed = dynamic_cast<HuskEditor*>(r.p.createEditor()); CHECK(ed != nullptr);
      if (ed) {
          CHECK(ed->isResizable());
          ed->setSize(1100, 700); savePng(*ed, "HUSK-silent.png");
          r.run(vowel(220, 0.6, 44100)); ed->setSize(1100, 700); savePng(*ed, "HUSK-vowel-full.png");
          r.set(P_PARTIALS, 1); r.run(vowel(220, 0.4, 44100)); savePng(*ed, "HUSK-partials-1.png");
          r.set(P_PARTIALS, 24); r.set(P_FREEZE, 1); r.set(P_MODEL, 3); r.set(P_WATER, 0.55f); r.set(P_GRAIN, 0.35f); r.set(P_SHIMMER, 0.4f); r.set(P_SPACE, 0.5f);
          r.run(vowel(196, 0.4, 44100)); r.run(std::vector<float>(22050, 0.0f)); savePng(*ed, "HUSK-freeze.png");
          r.set(P_FREEZE, 0); r.set(P_CHORD, 1); r.set(P_MODEL, 5); r.set(P_PARTIALS, 48); r.set(P_PHASE_SYNC, 1); r.set(P_STEPS, 4);
          r.run(vowel(220, 0.6, 44100), { { 10, juce::MidiMessage::noteOn(1, 48, 0.8f) }, { 10, juce::MidiMessage::noteOn(1, 52, 0.8f) }, { 10, juce::MidiMessage::noteOn(1, 55, 0.8f) } });
          savePng(*ed, "HUSK-chord-midi.png");
          ed->pad().resetToCentre(); CHECK(r.p.value(P_WATER) == 0.0f && r.p.value(P_GRAIN) == 0.0f);
          ed->setSize(770, 490); savePng(*ed, "HUSK-small.png");
          ed->setSize(1650, 1050); savePng(*ed, "HUSK-large.png");
          delete ed;
      } }

    std::printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

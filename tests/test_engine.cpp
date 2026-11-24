// HUSK v2 engine tests: timing, safety and behaviour of every feature, checked against known inputs.
// No JUCE. (Sound-quality gates on a real vocal are in test_quality.cpp.)
#include "Quality.h"
#include "TestUtil.h"
#include <chrono>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
using namespace husk;
using tu::cents; using tu::db;

static double SR = 44100;
struct Out { std::vector<float> L, R; std::vector<float> mono() const { std::vector<float> m(L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5f * (L[i] + R[i]); return m; } };
struct Run {
    Engine e; Params p; HostTime ht; int block = 256; double sr;
    std::function<void(Run&, size_t)> perBlock;     // called before each block with its start sample
    explicit Run(double fs = SR) : sr(fs) { e.prepare(sr, 4096); e.setParams(p); }
    Out go(const std::vector<float>& inL, const std::vector<float>* inR = nullptr) {
        Out o; o.L = inL; o.R = inR ? *inR : inL;
        for (size_t s = 0; s < inL.size(); s += (size_t)block) {
            if (perBlock) perBlock(*this, s);
            e.setParams(p); e.setTime(ht);
            int n = (int)std::min<size_t>((size_t)block, inL.size() - s);
            e.process(o.L.data() + s, o.R.data() + s, n);
            if (ht.playing) ht.ppq += n / sr * ht.bpm / 60.0;
        }
        return o;
    }
};
static double resonance(double f, double F, double bw) { double r = f / F; return 1.0 / std::sqrt((1 - r * r) * (1 - r * r) + (f * bw / (F * F)) * (f * bw / (F * F))); }
// sung vowel "ah" at f0 (optionally a per-sample f0 curve), about -12 dBFS
static std::vector<float> sung(double secs, std::function<double(double)> f0, double gain = 1.0, double sr = SR) {
    std::vector<float> x((size_t)(secs * sr)); double ph = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        double f = f0(i / sr); ph += husk::kTwoPi * f / sr; double s = 0, nrm = 0;
        for (int k = 1; k * f < 0.42 * sr && k <= 60; ++k) { double a = resonance(k * f, 700, 90) * resonance(k * f, 1220, 120) * resonance(k * f, 2600, 170) / k; s += a * std::sin(k * ph); nrm += a * a; }
        x[i] = (float)(gain * 0.35 * s / std::sqrt(nrm / 2 + 1e-12));
    }
    double e = 0; for (float v : x) e += (double)v * v; double r = std::sqrt(e / x.size()); for (auto& v : x) v *= (float)(0.25 * gain / r);
    return x;
}
static std::vector<float> sungAt(double secs, double f, double gain = 1.0, double sr = SR) { return sung(secs, [f](double) { return f; }, gain, sr); }
static std::vector<float> cat(std::vector<float> a, const std::vector<float>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static std::vector<float> sil(double secs, double sr = SR) { return std::vector<float>((size_t)(secs * sr), 0.0f); }
// strongest output frequency near f (Hz) over [t0, t1) of the latency-compensated output
static double peakNear(const std::vector<float>& y, double t0, double t1, double f, double hw, double sr = SR) {
    tu::Spec sp; sp.run(y, (size_t)(t0 * sr), (int)((t1 - t0) * sr), sr); double fr = 0; sp.peak(f, hw, fr); return fr;
}

int main() {
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("HUSK v2 engine tests");

    // ------------------------------------------------------------------ latency
    { std::puts("- latency: reported value, 19 ms budget, and measured on the wet path");
      for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
          Engine e; e.prepare(sr, 512); int L = e.latencySamples();
          CHECKM(L == e.frameSize() / 2 + e.hopSize() + e.limiter().la, "sr %g L %d", sr, L);
          CHECKM(L / sr * 1000.0 <= 19.0, "sr %g latency %.2f ms", sr, L / sr * 1000.0);
          std::printf("  %6.0f Hz: latency %d samples = %.2f ms\n", sr, L, L / sr * 1000.0);
      }
      for (double sr : { 44100.0, 48000.0 }) {
          // a sung note with a sharp loudness step: the output step must land where the report says
          Run r(sr); r.p.air = 0; r.p.unison = 1; r.p.drift = 0;
          auto x = cat(sungAt(0.6, 220, 0.25, sr), sungAt(0.6, 220, 1.0, sr));
          auto o = r.go(x).mono(); int L = r.e.latencySamples();
          // 2 ms RMS envelope in dB; the step time is where it crosses halfway (in dB) between the two levels
          auto envDb = [&](const std::vector<float>& v, size_t i) { double e = 1e-20; size_t w = (size_t)(0.002 * sr); for (size_t j = i - w / 2; j < i + w / 2 && j < v.size(); ++j) e += (double)v[j] * v[j]; return 10 * std::log10(e / w); };
          auto stepAt = [&](const std::vector<float>& v, size_t a, size_t b) { double lo = envDb(v, a), hi = envDb(v, b), mid = 0.5 * (lo + hi); for (size_t i = a; i < b; i += 4) if (envDb(v, i) > mid) return (double)i; return -1.0; };
          size_t c = (size_t)(0.6 * sr); double si = stepAt(x, c - (size_t)(0.1 * sr), c + (size_t)(0.1 * sr)), so = stepAt(o, c + (size_t)L - (size_t)(0.1 * sr), c + (size_t)L + (size_t)(0.1 * sr));
          double err = (so - si - L) / sr * 1000.0;
          std::printf("  %g Hz: loudness step measured %.2f ms after the input, reported %.2f ms (error %+.2f ms; the 8 ms loudness follower adds a little)\n", sr, (so - si) / sr * 1000, L / sr * 1000, err);
          CHECKM(err > -2.0 && err < 6.0, "latency error %.2f ms", err);
      } }

    // ------------------------------------------------------------------ nulls
    { std::puts("- Mix 0, Bypass and A/B are the dry signal delayed by the latency, bit for bit (hot input, every colour up)");
      for (double sr : { 44100.0, 96000.0 }) {
          husk::Rng g(5); std::vector<float> x((size_t)(2 * sr)), xr(x.size()); for (size_t i = 0; i < x.size(); ++i) { x[i] = 0.95f * g.bi(); xr[i] = 0.95f * g.bi(); }
          auto setHot = [](Params& p) { p.crunch = 1; p.space = 1; p.shimmer = 1; p.phase = 1; p.model = Choir; p.unison = 7; p.outDb = 12; p.air = 1; };
          for (int mode = 0; mode < 3; ++mode) {
              Run r(sr); setHot(r.p); if (mode == 0) { r.p.mix = 0; r.p.outDb = 0; } if (mode == 1) r.p.bypass = true; if (mode == 2) r.p.ab = true;
              auto o = r.go(x, &xr); int L = r.e.latencySamples(); double md = 0;
              for (size_t i = (size_t)L; i < x.size(); ++i) md = std::max({ md, (double)std::fabs(o.L[i] - x[i - (size_t)L]), (double)std::fabs(o.R[i] - xr[i - (size_t)L]) });
              CHECKM(md == 0.0, "sr %g mode %d max diff %g", sr, mode, md);
              std::printf("  %6.0f Hz %-7s max |out - dry| = %g\n", sr, mode == 0 ? "Mix 0" : mode == 1 ? "Bypass" : "A/B", md);
          }
      }
      Run r; r.p.mix = 0; std::vector<float> x((size_t)SR, 0.0f); x[1000] = 0.5f; auto o = r.go(x);
      CHECKM(tu::argmaxAbs(o.L) == 1000 + r.e.latencySamples(), "impulse at %d", tu::argmaxAbs(o.L)); }

    // ------------------------------------------------------------------ ceiling, NaN, denormals
    { std::puts("- output ceiling -0.1 dBFS true peak: every model, Out +12 dB, all colour up, hot singing");
      auto v = q::phrase(); std::vector<float> hot(v.x); for (auto& s : hot) s = std::clamp(s * 2.0f, -1.0f, 1.0f);
      float worst = 0;
      for (int m = 0; m < kNumModels; ++m) for (int chord = 0; chord < 2; ++chord) {
          Run r; r.p.model = m; r.p.outDb = 12; r.p.crunch = 1; r.p.shimmer = 1; r.p.space = 1; r.p.phase = 1; r.p.air = 1; r.p.unison = 7; r.p.detune = 1; r.p.width = 1; r.p.chord = chord;
          if (chord) for (int nn : { 48, 55, 60, 64, 67, 72 }) r.e.noteOn(nn);
          auto o = r.go(hot); float tp = std::max(tu::truePeak(o.L), tu::truePeak(o.R)); worst = std::max(worst, tp);
          CHECKM(tp <= std::pow(10.0f, -0.1f / 20.0f), "model %d chord %d true peak %.2f dBFS", m, chord, db(tp));
          CHECK(tu::finite(o.L) && tu::finite(o.R));
      }
      std::printf("  worst true peak %.2f dBFS\n", db(worst)); }
    { std::puts("- 30 s random fuzz (random settings every block, wild input, MIDI): finite, no subnormals, under the ceiling");
      Run r; husk::Rng g(77); size_t n = (size_t)(30 * SR); std::vector<float> x(n); double ph = 0; int kind = 0;
      for (size_t i = 0; i < n; ++i) { if (i % 22050 == 0) kind = (int)(g.uni() * 6); double f = 80 + 900 * (0.5 + 0.5 * std::sin(i * 1e-4));
          ph += husk::kTwoPi * f / SR; float s = 0;
          switch (kind) { case 0: s = 0; break; case 1: s = 0.9f * g.bi(); break; case 2: s = (float)(0.9 * std::sin(ph)); break; case 3: s = (float)(std::sin(ph) > 0 ? 1.0 : -1.0); break; case 4: s = 1e-30f * g.bi(); break; default: s = (float)(0.5 * std::sin(ph) + 0.5 * std::sin(3.01 * ph)); }
          x[i] = s; }
      r.block = 97;
      r.perBlock = [&](Run& rr, size_t) {
          if (g.uni() < 0.2) { Params& p = rr.p; p.model = (int)(g.uni() * kNumModels); p.partials = 1 + 63 * (float)g.uni(); p.talk = (float)g.uni(); p.air = (float)g.uni(); p.pitch = (float)(24 * g.uni() - 12); p.octave = (int)(g.uni() * 3) - 1; p.formant = (float)(24 * g.uni() - 12);
              p.phase = (float)g.uni(); p.phaseRate = (float)(0.05 + 20 * g.uni()); p.phaseSync = g.uni() < 0.3; p.phaseDiv = (int)(g.uni() * 6); p.steps = (int)(g.uni() * 7); p.freeze = g.uni() < 0.2; p.chord = g.uni() < 0.3;
              p.snap = g.uni() < 0.5; p.key = (int)(g.uni() * 12); p.scale = (int)(g.uni() * 5); p.glideMs = (float)(1000 * g.uni()); p.unison = 1 + (int)(g.uni() * 7); p.detune = (float)g.uni(); p.width = (float)g.uni(); p.drift = (float)g.uni();
              p.shimmer = (float)g.uni(); p.space = (float)g.uni(); p.crunch = (float)g.uni(); p.mix = (float)g.uni(); p.outDb = (float)(36 * g.uni() - 24); p.ab = g.uni() < 0.05; p.bypass = g.uni() < 0.05; }
          if (g.uni() < 0.05) rr.e.noteOn(36 + (int)(g.uni() * 60)); if (g.uni() < 0.05) rr.e.noteOff(36 + (int)(g.uni() * 60)); if (g.uni() < 0.005) rr.e.allNotesOff();
          rr.ht.valid = g.uni() < 0.8; rr.ht.playing = g.uni() < 0.9; if (g.uni() < 0.01) rr.ht.bpm = 40 + 200 * g.uni();
      };
      auto o = r.go(x);
      bool fin = tu::finite(o.L) && tu::finite(o.R); CHECK(fin); CHECK(!tu::hasSubnormal(o.L) && !tu::hasSubnormal(o.R));
      float pk = tu::peak2(o.L, o.R); CHECKM(pk <= std::pow(10.0f, -0.1f / 20.0f), "fuzz sample peak %.2f dBFS", db(pk));
      std::printf("  finite %s, sample peak %.2f dBFS\n", fin ? "yes" : "NO", db(pk));
      // after the storm: silence must decay to silence without subnormals, and a reset must be clean
      Run s; auto a = s.go(cat(q::phrase().x, sil(10))); std::vector<float> tail(a.L.end() - (long)(SR * 2), a.L.end());
      CHECK(!tu::hasSubnormal(a.L)); CHECKM(tu::peak(tail) < 1e-4f, "tail peak %g", tu::peak(tail));
      s.e.prepare(96000, 512); s.sr = 96000; auto b = s.go(sungAt(1.0, 220, 1.0, 96000)); CHECK(tu::finite(b.L) && tu::rms(b.L) > 1e-3); }

    // ------------------------------------------------------------------ pitch
    { std::puts("- pitch follows sung notes G2..G5 at -37/0/+23 cents (engine reading and output fundamental)");
      double worstE = 0, worstO = 0;
      for (int note = 43; note <= 79; note += 4) for (double ct : { -37.0, 0.0, 23.0 }) {
          double f = 440.0 * std::pow(2.0, (note - 69 + ct / 100.0) / 12.0);
          Run r; r.p.model = Sub; r.p.unison = 1; r.p.drift = 0; r.p.air = 0;
          auto o = r.go(sungAt(0.8, f)).mono(); int L = r.e.latencySamples();
          double e = std::fabs(cents(r.e.currentF0(), f)); worstE = std::max(worstE, e);
          double fo = peakNear(o, 0.35 + (double)L / SR, 0.75 + (double)L / SR, f, f * 0.03); double eo = std::fabs(cents(fo, f)); worstO = std::max(worstO, eo);
          CHECKM(e < 3.0 && eo < 5.0, "note %d %+g c: engine %.2f c, output %.2f c", note, ct, e, eo);
      }
      std::printf("  worst: engine %.2f cents, output %.2f cents\n", worstE, worstO); }
    { std::puts("- octave guard: a 30 ms octave-up blip inside a note is ignored; a real octave leap is followed");
      auto x = cat(cat(sungAt(0.6, 220), sungAt(0.03, 440)), sungAt(0.6, 220));
      Run r; double maxDev = 0; r.perBlock = [&](Run& rr, size_t s) { if (s > 0.3 * SR && s < 1.1 * SR && rr.e.currentF0() > 0) maxDev = std::max(maxDev, std::fabs(cents(rr.e.currentF0(), 220))); };
      r.go(x); std::printf("  blip: worst deviation %.1f cents (folds %lld)\n", maxDev, r.e.octaveFolds); CHECKM(maxDev < 100, "%.1f", maxDev);
      Run l; double at = -1; l.perBlock = [&](Run& rr, size_t s) { if (at < 0 && s > 0.6 * SR && std::fabs(cents(rr.e.currentF0(), 440)) < 30) at = (s - 0.6 * SR) / SR * 1000; };
      l.go(cat(sungAt(0.6, 220), sungAt(0.6, 440))); std::printf("  leap: followed after %.0f ms\n", at); CHECKM(at >= 0 && at < 120, "%.0f ms", at); }
    { std::puts("- gaps: the note rides through a 120 ms gap, and closes in a 400 ms gap");
      for (double gap : { 0.12, 0.4 }) {
          Run r; double minTone = 9, endTone = 9; size_t g0 = (size_t)(0.6 * SR) + (size_t)r.e.latencySamples();
          r.perBlock = [&](Run& rr, size_t s) { if (s > g0 + 0.01 * SR && s < g0 + (gap - 0.01) * SR) minTone = std::min(minTone, (double)rr.e.toneGate()); if (s > g0 + (gap - 0.06) * SR && s < g0 + (gap - 0.01) * SR) endTone = std::min(endTone, (double)rr.e.toneGate()); };
          r.go(cat(cat(sungAt(0.6, 220), sil(gap)), sungAt(0.4, 220)));
          std::printf("  gap %.0f ms: lowest note level inside the gap %.2f, near its end %.2f\n", gap * 1000, minTone, endTone);
          if (gap < 0.15) CHECKM(minTone > 0.5, "%.2f", minTone); else CHECKM(endTone < 0.05, "%.2f", endTone);
      } }
    { std::puts("- Note Snap: chromatic pulls a sharp note onto the nearest note; C major moves F#+45c to G");
      Run r; r.p.snap = true; r.go(sungAt(0.8, 220 * std::pow(2.0, 0.23 / 12))); double e1 = cents(r.e.currentF0(), 220);
      Run m; m.p.snap = true; m.p.scale = Major; m.go(sungAt(0.8, 185.0 * std::pow(2.0, 0.45 / 12))); double e2 = cents(m.e.currentF0(), 196.0);
      std::printf("  chromatic: %+.2f cents from A3; C major: %+.2f cents from G3\n", e1, e2); CHECK(std::fabs(e1) < 1.0); CHECK(std::fabs(e2) < 1.0); }
    { std::puts("- Glide: 0 ms jumps within 20 ms; 300 ms is still on the way after 50 ms and arrives within 1.2 s");
      for (float gms : { 0.0f, 300.0f }) {
          Run r; r.p.glideMs = gms; double at50 = 0, at1200 = 0; double t0 = 0.6 * SR + r.e.hopSize() * 0;
          r.perBlock = [&](Run& rr, size_t s) { if (!at50 && s >= t0 + 0.05 * SR + 1024) at50 = rr.e.currentF0(); if (!at1200 && s >= t0 + 1.2 * SR) at1200 = rr.e.currentF0(); };
          r.go(cat(sungAt(0.6, 220), sungAt(1.4, 330)));
          std::printf("  glide %3.0f ms: %.1f Hz after ~50 ms, %.1f Hz after 1.2 s (4 time constants)\n", gms, at50, at1200);
          if (gms == 0) CHECKM(std::fabs(cents(at50, 330)) < 10, "%.1f", at50); else CHECKM(at50 > 225 && at50 < 320 && std::fabs(cents(at1200, 330)) < 20, "%.1f %.1f", at50, at1200);
      } }
    { std::puts("- Freeze: grabs the last sung moment; engaged before singing it latches the first sung moment");
      Run r; r.p.model = Sub; r.p.unison = 1; r.p.drift = 0; r.perBlock = [](Run& rr, size_t s) { rr.p.freeze = s > 1.0 * SR; };
      auto o = r.go(cat(cat(sungAt(0.6, 220), sungAt(0.3, 330)), sil(1.6))).mono();
      double lev = db(tu::rms(o, (size_t)(1.6 * SR), (size_t)(2.4 * SR))), f = peakNear(o, 1.6, 2.4, 330, 40);
      std::printf("  frozen after the voice stopped: %.1f dBFS at %.1f Hz (last sung 330 Hz)\n", lev, f); CHECK(lev > -40); CHECK(std::fabs(cents(f, 330)) < 15);
      CHECK(r.e.isHolding());
      Run b; b.p.model = Sub; b.p.unison = 1; b.p.drift = 0; b.p.freeze = true;
      auto ob = b.go(cat(cat(sil(0.4), sungAt(0.5, 262)), cat(sungAt(0.5, 392), sil(1.0)))).mono();
      double lb = db(tu::rms(ob, (size_t)(1.7 * SR), (size_t)(2.3 * SR))), fb = peakNear(ob, 1.7, 2.3, 262, 30);
      std::printf("  engaged before singing: still %.1f dBFS at %.1f Hz after the voice stopped (first sung 262 Hz)\n", lb, fb); CHECK(lb > -40); CHECK(std::fabs(cents(fb, 262)) < 15); }
    { std::puts("- Chord: held MIDI notes play while you sing (3 notes), each one at its own pitch");
      Run r; r.p.model = Sub; r.p.unison = 1; r.p.drift = 0; r.p.chord = true; for (int nn : { 60, 64, 67 }) r.e.noteOn(nn);
      auto o = r.go(sungAt(1.2, 220)).mono(); tu::Spec sp; sp.run(o, (size_t)(0.5 * SR), (int)(0.6 * SR), SR);
      double ref = 0; for (int nn : { 60, 64, 67 }) ref = std::max(ref, sp.amp(440.0 * std::pow(2.0, (nn - 69) / 12.0), 4));
      for (int nn : { 60, 64, 67 }) { double f = 440.0 * std::pow(2.0, (nn - 69) / 12.0), fr = 0; double a = sp.peak(f, 4, fr); std::printf("  note %d: %.1f Hz at %.1f dB\n", nn, fr, db(a / ref)); CHECK(db(a / ref) > -12); CHECK(std::fabs(cents(fr, f)) < 5); }
      double a220 = sp.amp(220, 4); CHECKM(db(a220 / ref) < -15, "sung pitch leaks %.1f dB", db(a220 / ref));
      CHECK(r.e.heldNotes() == 3); r.e.noteOff(64); CHECK(r.e.heldNotes() == 2); r.e.allNotesOff(); CHECK(r.e.heldNotes() == 0); }
    { std::puts("- Steps: with the host at 120 bpm, 1/8 steps change pitch only on the 1/8-note grid (250 ms)");
      Run r; r.p.steps = 2; r.ht.valid = r.ht.playing = true; r.ht.bpm = 120; r.ht.ppq = 0; r.block = 64;
      std::vector<double> changes; double last = 0;
      r.perBlock = [&](Run& rr, size_t s) { double f = rr.e.currentF0(); if (last > 0 && std::fabs(cents(f, last)) > 30) changes.push_back(s / SR); last = f; };
      r.go(sung(3.0, [](double t) { return 160.0 * std::pow(2.0, t / 3.0); }));
      double worst = 0; int n = 0; double prev = -1;
      for (double c : changes) { if (prev >= 0 && c - prev < 0.05) { prev = c; continue; } prev = c; ++n; double ph = std::fmod(c - (double)r.e.latencySamples() * 0 / SR, 0.25); worst = std::max(worst, std::min(ph, 0.25 - ph)); }
      std::printf("  %d pitch steps, worst distance from the grid %.1f ms\n", n, worst * 1000); CHECK(n >= 8); CHECKM(worst < 0.03, "%.1f ms", worst * 1000); }
    { std::puts("- Partials: fewer partials means a darker sound (centroid falls steadily)");
      double prev = 1e9; bool mono = true;
      for (float k : { 64.0f, 6.0f, 3.0f, 1.0f }) { Run r; r.p.partials = k; auto o = r.go(sungAt(0.8, 196)).mono(); tu::Spec sp; sp.run(o, (size_t)(0.3 * SR), (int)(0.4 * SR), SR); double c = sp.centroid(10000); std::printf("  partials %2.0f: centroid %.0f Hz\n", k, c); if (c >= prev) mono = false; prev = c; }
      CHECK(mono); }
    { std::puts("- Width: 0 is mono, 100% is wide; Unison 1 with Width 0 is mono");
      Run a; a.p.width = 0; auto oa = a.go(sungAt(0.8, 220)); CHECKM(tu::rmsDiff(oa.L, oa.R) < 1e-6, "%g", tu::rmsDiff(oa.L, oa.R));
      Run b; b.p.width = 1; b.p.unison = 5; auto ob = b.go(sungAt(0.8, 220)); double side = tu::rmsDiff(ob.L, ob.R) / (tu::rms(ob.L) + 1e-9); std::printf("  side/level at 100%%: %.2f\n", side); CHECK(side > 0.2); }
    { std::puts("- Formant shift moves the vowel up and down (brightness of the shaped instrument)");
      double c[3]; int i = 0; for (float fm : { -12.0f, 0.0f, 12.0f }) { Run r; r.p.formant = fm; auto o = r.go(sungAt(0.8, 196)).mono(); tu::Spec sp; sp.run(o, (size_t)(0.3 * SR), (int)(0.4 * SR), SR); c[i++] = sp.centroid(8000); }
      std::printf("  centroid -12/0/+12: %.0f / %.0f / %.0f Hz\n", c[0], c[1], c[2]); CHECK(c[0] < c[1] && c[1] < c[2]); }
    { std::puts("- Pitch and Octave transpose the played note exactly");
      for (auto pr : { std::pair<float, int>{ 7.0f, 0 }, { 0.0f, 1 }, { -5.0f, -1 } }) { Run r; r.p.pitch = pr.first; r.p.octave = pr.second; r.go(sungAt(0.8, 220)); double want = 220 * std::pow(2.0, (pr.first + 12 * pr.second) / 12.0); double e = cents(r.e.currentF0(), want); std::printf("  %+g st, octave %+d: %+.2f cents\n", pr.first, pr.second, e); CHECK(std::fabs(e) < 2); } }

    // ------------------------------------------------------------------ CPU
    { std::puts("- CPU: worst case (6 chord notes x 7 unison, every effect up) per model, 48 kHz");
      double worst = 0; int wm = 0;
      for (int m = 0; m < kNumModels; ++m) {
          Run r(48000); r.p.model = m; r.p.unison = 7; r.p.chord = true; r.p.shimmer = 1; r.p.space = 1; r.p.phase = 1; r.p.crunch = 1; r.p.air = 1; r.p.width = 1;
          for (int nn : { 48, 52, 55, 60, 64, 67 }) r.e.noteOn(nn);
          auto x = sungAt(4.0, 220, 1.0, 48000);
          auto t0 = std::chrono::steady_clock::now(); r.go(x); double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          double frac = el / 4.0; if (frac > worst) { worst = frac; wm = m; }
      }
      std::printf("  worst: %s at %.1f%% of one core in real time\n", modelName(wm), worst * 100); CHECKM(worst < 0.5, "%.2f", worst); }

    std::printf("\nengine checks: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

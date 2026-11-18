// HUSK engine tests — checked against textbook maths (no JUCE).
#include "TestUtil.h"
#include "HuskEngine.h"
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
using namespace husk;
using tu::Spec; using tu::cents; using tu::db;

static double SR = 44100;
struct Out { std::vector<float> L, R; std::vector<float> mono() const { std::vector<float> m(L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5f * (L[i] + R[i]); return m; } };
struct Run {
    Engine e; Params p; HostTime ht; int block = 512;
    std::function<void(Run&, size_t)> perBlock;   // called before each block with its start sample
    explicit Run(double sr = SR) { p.air = 0; e.prepare(sr, 4096); e.setParams(p); }  // Air 0 unless a test asks
    Out go(const std::vector<float>& inL, const std::vector<float>* inR = nullptr) {
        Out o; o.L = inL; o.R = inR ? *inR : inL;
        for (size_t s = 0; s < inL.size(); s += (size_t)block) {
            if (perBlock) perBlock(*this, s);
            e.setParams(p); e.setTime(ht);
            int n = (int)std::min<size_t>((size_t)block, inL.size() - s);
            e.process(o.L.data() + s, o.R.data() + s, n);
            if (ht.playing) ht.ppq += n / SRof() * ht.bpm / 60.0;
        }
        return o;
    }
    double SRof() const { return srate; }
    double srate = SR;
};
static Run* mk(double sr = SR) { auto* r = new Run(sr); r->srate = sr; r->p.air = 0; return r; }

static std::vector<float> harmonic(double f0, const std::vector<double>& amps, double secs, double sr = SR, double gain = 1.0) {
    std::vector<float> x((size_t)(secs * sr));
    for (size_t i = 0; i < x.size(); ++i) { double t = i / sr, s = 0; for (size_t k = 0; k < amps.size(); ++k) s += amps[k] * std::sin(husk::kTwoPi * f0 * (k + 1) * t + 0.3 * k); x[i] = (float)(gain * s); }
    return x;
}
static double resonance(double f, double F, double bw) { double r = f / F; return 1.0 / std::sqrt((1 - r * r) * (1 - r * r) + (f * bw / (F * F)) * (f * bw / (F * F))); }
static std::vector<double> vowelAmps(double f0, double sr = SR, double F1 = 700, double F2 = 1220, double F3 = 2600) {
    std::vector<double> a; for (int k = 1; k * f0 < 0.42 * sr && k <= 64; ++k) { double f = k * f0; a.push_back(0.02 / k * resonance(f, F1, 90) * resonance(f, F2, 120) * resonance(f, F3, 170)); }
    double s = 0; for (double v : a) s += v * v; double sc = 0.25 / std::sqrt(s / 2); for (double& v : a) v *= sc;  // ~ -12 dBFS rms
    return a;
}
static std::vector<float> concat(std::vector<float> a, const std::vector<float>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static std::vector<float> silence(double secs, double sr = SR) { return std::vector<float>((size_t)(secs * sr), 0.0f); }
static double segRms(const std::vector<float>& x, double t0, double t1, double sr = SR) { return tu::rms(x, (size_t)(t0 * sr), (size_t)(t1 * sr)); }

int main() {
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("HUSK engine tests");

    // ---------------------------------------------------------------- latency budget
    { std::puts("- latency: declared value and 25 ms budget");
      for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
          Engine e; e.prepare(sr, 512);
          int L = e.latencySamples();
          CHECKM(L == e.frameSize() / 2 + e.hopSize() + e.limiter().la, "sr %g L %d", sr, L);
          CHECKM(L / sr * 1000.0 <= 25.0, "sr %g latency %.2f ms", sr, L / sr * 1000.0);
          std::printf("  %6.0f Hz: frame %d hop %d lookahead %d -> latency %d samples = %.2f ms\n", sr, e.frameSize(), e.hopSize(), e.limiter().la, L, L / sr * 1000.0);
      } }

    // ---------------------------------------------------------------- Mix 0 null / impulse / bypass
    { std::puts("- Mix 0 nulls against the latency-compensated dry (even with every colour knob up)");
      for (double sr : { 44100.0, 48000.0 }) {
          Run r(sr); r.srate = sr; r.p.mix = 0; r.p.crunch = 1; r.p.space = 1; r.p.shimmer = 1; r.p.phase = 1; r.p.water = 1; r.p.grain = 1; r.p.model = Choir;
          husk::Rng g(5); std::vector<float> x((size_t)(2 * sr)); for (auto& v : x) v = 0.3f * g.bi();
          auto o = r.go(x); int L = r.e.latencySamples(); double md = 0;
          for (size_t i = (size_t)L; i < x.size(); ++i) md = std::max(md, (double)std::fabs(o.L[i] - x[i - (size_t)L]));
          CHECKM(md == 0.0, "sr %g max diff %g", sr, md);
          std::printf("  %g Hz: mix-0 max |out - dry(t-%d)| = %g\n", sr, L, md);
      }
      Run r; r.p.mix = 0; std::vector<float> x((size_t)SR, 0.0f); x[1000] = 0.5f;
      auto o = r.go(x); int at = tu::argmaxAbs(o.L);
      CHECKM(at == 1000 + r.e.latencySamples(), "impulse at %d", at);
      Run b; b.p.bypass = true; husk::Rng g(9); std::vector<float> y((size_t)SR); for (auto& v : y) v = 0.8f * g.bi();
      auto ob = b.go(y); double md = 0; int L = b.e.latencySamples();
      for (size_t i = (size_t)L; i < y.size(); ++i) md = std::max(md, (double)std::fabs(ob.L[i] - y[i - (size_t)L]));
      CHECKM(md == 0.0, "bypass diff %g", md);
      Run ab; ab.p.ab = true; auto oa = ab.go(y); md = 0;
      for (size_t i = (size_t)L; i < y.size(); ++i) md = std::max(md, (double)std::fabs(oa.L[i] - y[i - (size_t)L]));
      CHECKM(md == 0.0, "A/B diff %g", md);
    }

    // ---------------------------------------------------------------- measured latency of the wet path
    { std::puts("- measured wet latency matches the report (tone-burst centroid)");
      for (double sr : { 44100.0, 48000.0 }) {
          Run r(sr); r.srate = sr; r.p.air = 0;
          std::vector<float> x((size_t)(1.0 * sr), 0.0f);
          size_t c0 = (size_t)(0.3 * sr), len = (size_t)(0.2 * sr);
          for (size_t i = 0; i < len; ++i) { double w = 0.5 - 0.5 * std::cos(husk::kTwoPi * i / len); x[c0 + i] = (float)(0.5 * w * std::sin(husk::kTwoPi * 220 * i / sr)); }
          auto o = r.go(x);
          auto centroid = [](const std::vector<float>& v) { double s = 0, sw = 0; for (size_t i = 0; i < v.size(); ++i) { double e = (double)v[i] * v[i]; s += e * i; sw += e; } return s / sw; };
          double meas = centroid(o.L) - centroid(x);
          double err = (meas - r.e.latencySamples()) / sr * 1000.0;
          std::printf("  %g Hz: reported %d (%.2f ms), measured %.1f samples (%.2f ms), error %.3f ms\n", sr, r.e.latencySamples(), r.e.latencySamples() / sr * 1000, meas, meas / sr * 1000, err);
          CHECKM(std::fabs(err) < 1.0, "latency error %.3f ms", err);
      } }

    // ---------------------------------------------------------------- pitch tracking
    { std::puts("- pitch tracker vs known notes and cents offsets (sine and vowel)");
      double worstS = 0, worstV = 0; int bad = 0;
      for (int note = 43; note <= 79; note += 3)
          for (double ct : { -37.0, 0.0, 23.0 }) {
              double f = 440.0 * std::pow(2.0, (note - 69 + ct / 100.0) / 12.0);
              for (int kind = 0; kind < 2; ++kind) {
                  auto x = kind == 0 ? harmonic(f, { 0.3 }, 0.35) : harmonic(f, vowelAmps(f), 0.35);
                  Run r; r.go(x);
                  if (!r.e.live.voiced) { ++bad; continue; }
                  double e = std::fabs(cents(r.e.live.f0, f));
                  (kind == 0 ? worstS : worstV) = std::max(kind == 0 ? worstS : worstV, e);
              }
          }
      std::printf("  notes G2..G5 x {-37,0,+23} cents: worst error sine %.3f cents, vowel %.3f cents, unvoiced misses %d\n", worstS, worstV, bad);
      CHECK(bad == 0); CHECKM(worstS < 2.0, "%.3f", worstS); CHECKM(worstV < 3.0, "%.3f", worstV);
      // unpitched material must be flagged
      husk::Rng g(3); int voicedFrames = 0, frames = 0;
      { Run r; std::vector<float> x((size_t)SR); for (auto& v : x) v = 0.2f * g.bi();
        r.block = r.e.hopSize(); r.perBlock = [&](Run& rr, size_t s) { if (s > 4096) { ++frames; voicedFrames += rr.e.live.voiced; } }; r.go(x); }
      { Run r; auto x = silence(0.5); r.block = r.e.hopSize(); r.perBlock = [&](Run& rr, size_t) { ++frames; voicedFrames += rr.e.live.voiced; }; r.go(x); }
      { Run r; std::vector<float> x((size_t)(0.5 * SR)); husk::BandPass bp; bp.set(SR, 6000, 2); for (auto& v : x) v = 0.5f * bp.tick(g.bi());   // "s"-like hiss
        r.block = r.e.hopSize(); r.perBlock = [&](Run& rr, size_t s) { if (s > 4096) { ++frames; voicedFrames += rr.e.live.voiced; } }; r.go(x); }
      std::printf("  noise / silence / hiss frames flagged voiced: %d of %d\n", voicedFrames, frames);
      CHECK(voicedFrames <= frames / 50);
    }

    // ---------------------------------------------------------------- partial analysis
    { std::puts("- partial analysis recovers a 1/k harmonic series");
      for (double f0 : { 110.0, 220.0, 330.0 }) {
          std::vector<double> amps; for (int k = 1; k <= 20; ++k) amps.push_back(0.3 / k);
          Run r; r.go(harmonic(f0, amps, 0.4)); const auto& F = r.e.live;
          double wc = 0, wa = 0; for (int k = 1; k <= 20; ++k) { wc = std::max(wc, std::fabs(cents(F.fk[k - 1], f0 * k))); wa = std::max(wa, std::fabs(db(F.ak[k - 1] / (0.3 / k)))); }
          std::printf("  f0 %g: %d partials, worst freq error %.3f cents, worst level error %.3f dB\n", f0, F.nh, wc, wa);
          CHECK(F.nh >= 20); CHECKM(wc < 2.0, "%.3f", wc); CHECKM(wa < 0.3, "%.3f", wa);
      } }

    // ---------------------------------------------------------------- round trip: harmonic tones
    { std::puts("- analysis/resynthesis round trip on harmonic tones (Sine model, all partials)");
      for (double f0 : { 147.0, 220.0, 392.0 }) {
          std::vector<double> amps; for (int k = 1; k <= 12; ++k) amps.push_back(0.25 / k);
          Run r; auto x = harmonic(f0, amps, 1.5); auto o = r.go(x); auto m = o.mono();
          Spec s; s.run(m, (size_t)(0.5 * SR), 32768, SR);
          double wc = 0, wa = 0;
          for (int k = 1; k <= 12; ++k) { double fr; double a = s.peak(f0 * k, f0 * 0.3, fr); wc = std::max(wc, std::fabs(cents(fr, f0 * k))); wa = std::max(wa, std::fabs(db(a / amps[(size_t)k - 1]))); }
          double rIn = segRms(x, 0.5, 1.2), rOut = segRms(m, 0.5, 1.2);
          std::printf("  f0 %g: worst partial freq %.3f cents, level %.3f dB, rms in/out %.3f dB\n", f0, wc, wa, db(rOut / rIn));
          CHECKM(wc < 2.0, "%.3f", wc); CHECKM(wa < 0.5, "%.3f", wa); CHECKM(std::fabs(db(rOut / rIn)) < 0.5, "%.3f", db(rOut / rIn));
      } }

    // ---------------------------------------------------------------- round trip: sine sweep
    { std::puts("- round trip on a sine sweep (110 -> 880 Hz over 4 s)");
      double secs = 4.0; std::vector<float> x((size_t)(secs * SR)); double ph = 0;
      auto finst = [&](double t) { return 110.0 * std::pow(8.0, t / secs); };
      for (size_t i = 0; i < x.size(); ++i) { ph += husk::kTwoPi * finst(i / SR) / SR; x[i] = (float)(0.4 * std::sin(ph)); }
      Run r; auto o = r.go(x); auto m = o.mono(); int L = r.e.latencySamples();
      double wc = 0, wa = 0; Spec s;
      for (double t = 0.3; t < secs - 0.2; t += 0.1) {
          size_t c = (size_t)(t * SR); int len = 1024; s.run(m, c - (size_t)len / 2, len, SR);
          double fexp = finst((double)(c - (size_t)L) / SR); double fr; double a = s.peak(fexp, fexp * 0.2, fr);
          wc = std::max(wc, std::fabs(cents(fr, fexp))); wa = std::max(wa, std::fabs(db(a / 0.4)));
      }
      std::printf("  worst frequency error %.2f cents, worst level error %.2f dB (23 ms windows, sweep 0.75 oct/s)\n", wc, wa);
      CHECKM(wc < 10.0, "%.2f", wc); CHECKM(wa < 1.0, "%.2f", wa);
    }

    // ---------------------------------------------------------------- Partials knob
    { std::puts("- Partials: 1 keeps only the fundamental; K keeps the K lowest tones");
      std::vector<double> amps; for (int k = 1; k <= 16; ++k) amps.push_back(0.25 / k);
      for (float K : { 1.0f, 3.0f, 8.0f }) {
          Run r; r.p.partials = K; auto o = r.go(harmonic(196, amps, 1.2)); auto m = o.mono();
          Spec s; s.run(m, (size_t)(0.5 * SR), 16384, SR);
          double a1 = s.amp(196, 50); double keptMin = 1e9, dropMax = -999;
          for (int k = 1; k <= 16; ++k) { double a = s.amp(196.0 * k, 50); if (k <= (int)K) keptMin = std::min(keptMin, db(a / a1)); else dropMax = std::max(dropMax, db(a / a1)); }
          std::printf("  K=%g: weakest kept %.1f dB, strongest dropped %.1f dB (re fundamental), display count %d\n", K, keptMin, (int)K < 16 ? dropMax : -999.0, r.e.displayCount);
          CHECK(keptMin > -30); if ((int)K < 16) CHECKM(dropMax < -60, "%.1f", dropMax);
          CHECK(r.e.displayCount == (int)K);
      }
      // one partial follows a sung melody
      std::vector<float> mel; std::vector<double> fs = { 196.0, 220.0, 246.94, 293.66, 261.63, 174.61 };
      for (double f : fs) mel = concat(mel, harmonic(f, vowelAmps(f), 0.35));
      Run r; r.p.partials = 1; auto o = r.go(mel); auto m = o.mono(); int L = r.e.latencySamples();
      double wc = 0, h2 = -999;
      for (size_t i = 0; i < fs.size(); ++i) {
          size_t c = (size_t)((i * 0.35 + 0.2) * SR) + (size_t)L; Spec s; s.run(m, c - 2048, 4096, SR); double fr; double a = s.peak(fs[i], fs[i] * 0.2, fr);
          wc = std::max(wc, std::fabs(cents(fr, fs[i]))); h2 = std::max(h2, db(s.amp(2 * fs[i], fs[i] * 0.2) / a));
      }
      std::printf("  melody with Partials=1: worst pitch error %.2f cents, 2nd harmonic %.1f dB\n", wc, h2);
      CHECKM(wc < 3.0, "%.2f", wc); CHECKM(h2 < -50, "%.1f", h2);
    }

    // ---------------------------------------------------------------- pitch shift and octave
    { std::puts("- Pitch moves every partial by 2^(st/12); Octave is exactly x2 / x0.5; length unchanged");
      std::vector<double> amps; for (int k = 1; k <= 8; ++k) amps.push_back(0.25 / k);
      for (auto cfg : { std::pair<float, int>{ 7.0f, 0 }, { -5.0f, 0 }, { 12.0f, 0 }, { 0.0f, 1 }, { 0.0f, -1 }, { 3.0f, 1 } }) {
          Run r; r.p.pitch = cfg.first; r.p.octave = cfg.second;
          auto x = concat(harmonic(220, amps, 1.0), silence(0.5)); auto o = r.go(x); auto m = o.mono();
          double ratio = std::pow(2.0, (cfg.first + 12.0 * cfg.second) / 12.0);
          Spec s; s.run(m, (size_t)(0.3 * SR), 16384, SR); double wc = 0;
          for (int k = 1; k <= 8; ++k) { if (220 * k * ratio > 0.44 * SR) break; double fr; s.peak(220 * k * ratio, 220 * ratio * 0.3, fr); wc = std::max(wc, std::fabs(cents(fr, 220 * k * ratio))); }
          // end of tone: last sample above -40 dB of the steady level, minus latency
          double steady = segRms(m, 0.4, 0.8); size_t last = 0; for (size_t i = 0; i < m.size(); ++i) if (std::fabs(m[i]) > steady * 0.01) last = i;
          double endT = (double)(last - (size_t)r.e.latencySamples()) / SR;
          std::printf("  pitch %+g st, octave %+d: worst partial %.3f cents, tone ends at %.3f s (input 1.000 s)\n", cfg.first, cfg.second, wc, endT);
          CHECKM(wc < (cfg.first == 0 ? 1.0 : 2.0), "%.3f", wc); CHECKM(std::fabs(endT - 1.0) < 0.03, "%.3f", endT);
      } }

    // ---------------------------------------------------------------- formant
    { std::puts("- Formant: Pitch keeps the mouth in place; Formant moves it by the expected ratio");
      auto envPeak = [](const std::vector<float>& m, double f0) {
          Spec s; s.run(m, (size_t)(0.4 * SR), 16384, SR);
          std::vector<double> a; for (int k = 1; k * f0 < 4000; ++k) a.push_back(std::log(s.amp(f0 * k, f0 * 0.3)));
          size_t kb = 0; for (size_t k = 0; k < a.size(); ++k) if (a[k] > a[kb]) kb = k;
          double p = 0; if (kb > 0 && kb + 1 < a.size()) { double d = a[kb - 1] - 2 * a[kb] + a[kb + 1]; if (d < 0) p = 0.5 * (a[kb - 1] - a[kb + 1]) / d; }
          return (kb + 1 + p) * f0;
      };
      // single-formant vowel at 800 Hz, f0 110 (dense harmonics)
      std::vector<double> amps; for (int k = 1; k * 110.0 < 8000; ++k) amps.push_back(0.02 * resonance(110.0 * k, 800, 120) / std::sqrt((double)k));
      auto x = harmonic(110, amps, 1.0);
      double base; { Run r; base = envPeak(r.go(x).mono(), 110); }
      double pitched; { Run r; r.p.pitch = 5; pitched = envPeak(r.go(x).mono(), 110 * std::pow(2.0, 5 / 12.0)); }
      double moved, f0moved; { Run r; r.p.formant = 5; auto m = r.go(x).mono(); moved = envPeak(m, 110); Spec s; s.run(m, (size_t)(0.4 * SR), 16384, SR); s.peak(110, 30, f0moved); }
      double expect = 800 * std::pow(2.0, 5 / 12.0);
      std::printf("  envelope peak: unshifted %.0f Hz, pitch +5 st %.0f Hz, formant +5 st %.0f Hz (expect %.0f), f0 with formant shift %.2f Hz\n", base, pitched, moved, expect, f0moved);
      CHECKM(std::fabs(base / 800 - 1) < 0.06, "%.0f", base); CHECKM(std::fabs(pitched / base - 1) < 0.08, "%.0f", pitched);
      CHECKM(std::fabs(moved / expect - 1) < 0.08, "%.0f", moved); CHECKM(std::fabs(cents(f0moved, 110)) < 2, "%.2f", f0moved);
    }

    // ---------------------------------------------------------------- models
    { std::puts("- Models: Sine/Saw/Square/Glass/Choir/Flute/String recipes");
      auto sine = harmonic(220, { 0.3 }, 1.5);
      auto spec = [&](int model, const std::vector<float>& x, Spec& s, Out* keep = nullptr) { Run r; r.p.model = model; auto o = r.go(x); s.run(o.mono(), (size_t)(0.5 * SR), 32768, SR); if (keep) *keep = o; };
      Spec s;
      spec(Sine, sine, s); double a1 = s.amp(220, 60), hmax = -999; for (int k = 2; k <= 10; ++k) hmax = std::max(hmax, db(s.amp(220.0 * k, 60) / a1));
      double inter = db(std::sqrt(s.bandPower(240, 420)) / a1);
      std::printf("  Sine: harmonics above f0 %.1f dB, between-harmonic energy %.1f dB\n", hmax, inter);
      CHECK(hmax < -60); CHECK(inter < -60);
      spec(Saw, sine, s); a1 = s.amp(220, 60); double we = 0; for (int k = 2; k <= 10; ++k) we = std::max(we, std::fabs(db(s.amp(220.0 * k, 60) / a1) - db(1.0 / k)));
      std::printf("  Saw: worst deviation from 1/n over n=2..10: %.2f dB\n", we); CHECKM(we < 1.0, "%.2f", we);
      spec(Square, sine, s); a1 = s.amp(220, 60); double ev = -999, od = 999; for (int k = 2; k <= 9; ++k) { double v = db(s.amp(220.0 * k, 60) / a1); if (k % 2 == 0) ev = std::max(ev, v); else od = std::min(od, v + db(k)); }
      std::printf("  Square: loudest even harmonic %.1f dB; odd harmonics within %.2f dB of 1/n\n", ev, std::fabs(od)); CHECK(ev < -30); CHECK(std::fabs(od) < 1.0);
      spec(Glass, sine, s); double f8; s.peak(220 * 8 * std::sqrt(1 + 0.0018 * 64) / std::sqrt(1.0018), 80, f8);
      std::printf("  Glass: 8th partial at %.2f x f0 (harmonic would be 8.000)\n", f8 / 220); CHECK(f8 / 220 > 8.3);
      { double fH; s.peak(220 * 8, 25, fH); (void)fH; }
      // Choir: beating between detuned copies
      auto beat = [&](int model) { Run r; r.p.model = model; auto m = r.go(sine).mono(); std::vector<double> env; for (double t = 0.4; t < 1.4; t += 0.05) env.push_back(segRms(m, t, t + 0.05)); double mu = 0, sd = 0; for (double v : env) mu += v; mu /= env.size(); for (double v : env) sd += (v - mu) * (v - mu); return std::sqrt(sd / env.size()) / mu; };
      double bS = beat(Sine), bC = beat(Choir);
      std::printf("  Choir: envelope beating %.3f vs Sine %.4f\n", bC, bS); CHECK(bC > 0.05); CHECK(bS < 0.01);
      // Flute: more breath; String: brighter (vowel with a breath band)
      auto vow = harmonic(196, vowelAmps(196), 1.5); husk::Rng g(11); husk::BandPass hb; hb.set(SR, 5000, 1.5);
      for (auto& v : vow) v += 0.01f * hb.tick(g.bi());
      auto resid = [&](int model) { Run r; r.p.model = model; r.p.air = 0.5f; auto m = r.go(vow).mono(); Spec q; q.run(m, (size_t)(0.5 * SR), 32768, SR); return q.bandPower(3500, 7000); };
      auto cen = [&](int model) { Run r; r.p.model = model; auto m = r.go(vow).mono(); Spec q; q.run(m, (size_t)(0.5 * SR), 32768, SR); return q.centroid(12000); };
      double rs = resid(Sine), rf = resid(Flute), cs = cen(Sine), cst = cen(String);
      std::printf("  Flute residual %+.1f dB vs Sine; String centroid %.0f Hz vs Sine %.0f Hz\n", db(std::sqrt(rf / rs)), cst, cs);
      CHECK(rf > rs * 1.5); CHECK(cst > cs * 1.1);
    }

    // ---------------------------------------------------------------- freeze
    { std::puts("- Freeze holds the partials through a pitch change and silence; release follows again");
      std::vector<double> a4 = { 0.3, 0.15, 0.1 };
      auto x = concat(concat(harmonic(220, a4, 1.0), harmonic(330, a4, 1.0)), silence(1.0));
      x = concat(x, harmonic(330, a4, 1.0));
      Run r; r.perBlock = [](Run& rr, size_t s) { rr.p.freeze = s >= (size_t)(0.8 * SR) && s < (size_t)(3.2 * SR); };
      auto o = r.go(x); auto m = o.mono(); int L = r.e.latencySamples();
      auto fAt = [&](double t) { Spec s; s.run(m, (size_t)(t * SR) + (size_t)L, 8192, SR); double fr; s.peak(275, 120, fr); return fr; };
      double fa = fAt(0.85), fb = fAt(1.4), fc = fAt(2.4), fd = fAt(3.6);
      double la = segRms(m, 0.9, 1.0), lb = segRms(m, 1.5, 1.6), lc = segRms(m, 2.5, 2.6);
      std::printf("  frozen: %.2f / %.2f / %.2f Hz (220 in), levels %.2f / %.2f dB; after release %.2f Hz (330 in)\n", fa, fb, fc, db(lb / la), db(lc / la), fd);
      CHECK(std::fabs(cents(fb, 220)) < 2); CHECK(std::fabs(cents(fc, 220)) < 2); CHECK(std::fabs(db(lb / la)) < 1); CHECK(std::fabs(db(lc / la)) < 1);
      CHECK(std::fabs(cents(fd, 330)) < 2);
      // clicks: largest 2nd difference near the freeze/release moments vs steady state
      auto d2 = [&](double t0, double t1) { double mx = 0; for (size_t i = (size_t)(t0 * SR) + 2; i < (size_t)(t1 * SR); ++i) mx = std::max(mx, (double)std::fabs(m[i] - 2 * m[i - 1] + m[i - 2])); return mx; };
      double Ls = (double)L / SR, steady = d2(0.5 + Ls, 0.75 + Ls), atF = d2(0.78 + Ls, 0.86 + Ls), atR = d2(3.18 + Ls, 3.3 + Ls), steady2 = d2(3.5 + Ls, 3.9 + Ls);
      std::printf("  2nd-difference: steady %.4f, at freeze %.4f, at release %.4f (steady after %.4f)\n", steady, atF, atR, steady2);
      CHECK(atF < steady * 1.5); CHECK(atR < std::max(steady, steady2) * 2.0);
    }

    // ---------------------------------------------------------------- chord mode
    { std::puts("- Chord mode: C3-E3-G3 sung on the voice; amplitude follows; 7th note ignored");
      auto vox = concat(harmonic(220, vowelAmps(220), 1.0), harmonic(220, vowelAmps(220), 1.0, SR, 0.5));
      Run r; r.p.chord = true; r.e.noteOn(48); r.e.noteOn(52); r.e.noteOn(55);
      auto m = r.go(vox).mono(); int L = r.e.latencySamples();
      Spec s; s.run(m, (size_t)(0.3 * SR), 16384, SR);
      double wc = 0; double tot = std::sqrt(s.bandPower(60, 10000));
      for (int n : { 48, 52, 55 }) { double f = 440 * std::pow(2.0, (n - 69) / 12.0), fr; double a = s.peak(f, 8, fr); wc = std::max(wc, std::fabs(cents(fr, f))); CHECK(db(a / tot) > -25); }
      double at220 = db(s.amp(220, 4) / tot);
      double l1 = segRms(m, 0.4 + (double)L / SR, 0.9 + (double)L / SR), l2 = segRms(m, 1.4 + (double)L / SR, 1.9 + (double)L / SR);
      std::printf("  chord tones within %.3f cents; voice f0 220 Hz at %.1f dB; voice -6 dB -> output %.2f dB; held %d\n", wc, at220, db(l2 / l1), r.e.activeNoteCount());
      CHECK(wc < 2); CHECK(at220 < -30); CHECK(std::fabs(db(l2 / l1) + 6.02) < 1.0); CHECK(r.e.activeNoteCount() == 3);
      // chord off / no MIDI -> monophonic
      { Run q; q.p.chord = true; auto mm = q.go(vox).mono(); Spec t; t.run(mm, (size_t)(0.3 * SR), 16384, SR); double fr; t.peak(220, 20, fr); CHECK(std::fabs(cents(fr, 220)) < 2); CHECK(db(t.amp(130.81, 4) / t.amp(220, 4)) < -50); }
      { Run q; q.e.noteOn(48); auto mm = q.go(vox).mono(); Spec t; t.run(mm, (size_t)(0.3 * SR), 16384, SR); CHECK(db(t.amp(130.81, 4) / t.amp(220, 4)) < -50); }
      // seven notes: the seventh is ignored
      Run q; q.p.chord = true; int ns[7] = { 48, 52, 55, 59, 62, 65, 69 }; for (int n : ns) q.e.noteOn(n);
      auto mm = q.go(vox).mono(); Spec t; t.run(mm, (size_t)(0.3 * SR), 16384, SR); double tt = std::sqrt(t.bandPower(60, 10000));
      double a7 = db(t.amp(440, 3) / tt), a6 = db(t.amp(349.23, 3) / tt);
      std::printf("  7 notes held: active %d, ignored %lld, 6th note %.1f dB, 7th (A4) %.1f dB\n", q.e.activeNoteCount(), q.e.ignoredNotes, a6, a7);
      CHECK(q.e.activeNoteCount() == 6); CHECK(q.e.ignoredNotes == 1); CHECK(a6 > -35); CHECK(a7 < a6 - 20);
    }

    // ---------------------------------------------------------------- steps
    { std::puts("- Steps lock recipe updates to the host tempo grid");
      struct Case { double bpm; int steps; double expect; };
      for (Case c : { Case{ 120, 4, 0.125 }, Case{ 100, 3, 0.2 }, Case{ 140, 1, 60.0 / 140 }, Case{ 120, 6, 0.0625 } }) {
          std::vector<float> x((size_t)(3 * SR)); double ph = 0; for (size_t i = 0; i < x.size(); ++i) { ph += husk::kTwoPi * (200 + 100 * i / (3 * SR)) / SR; x[i] = (float)(0.3 * std::sin(ph)); }
          Run r; r.p.steps = c.steps; r.ht.valid = true; r.ht.playing = true; r.ht.bpm = c.bpm; r.ht.ppq = 0; r.block = r.e.hopSize();
          std::vector<double> times; long long last = -1; std::vector<float> f0s; int holdsBroken = 0; float prevF = 0; long long prevU = -1;
          r.perBlock = [&](Run& rr, size_t s) {
              if (rr.e.recipeUpdates != last) { if (last >= 0) times.push_back(s / SR); last = rr.e.recipeUpdates; }
              float f = rr.e.tgtFreq(0, 0);
              if (prevU == rr.e.recipeUpdates && s > 8192 && std::fabs(f - prevF) > 1e-3f) ++holdsBroken;
              prevF = f; prevU = rr.e.recipeUpdates;
          };
          r.go(x);
          double worst = 0; for (size_t i = 2; i < times.size(); ++i)  // first update is the start, not a grid line
          worst = std::max(worst, std::fabs(times[i] - times[i - 1] - c.expect));
          std::printf("  %g BPM %s: %zu updates, worst interval error %.2f ms (hop %.2f ms), recipe changes between updates %d\n", c.bpm, divName(c.steps - 1), times.size(), worst * 1000, r.e.hopSize() / SR * 1000, holdsBroken);
          CHECK(times.size() >= 3); CHECKM(worst <= r.e.hopSize() / SR + 1e-6, "%.4f", worst); CHECK(holdsBroken == 0);
      }
      Run r; long long u0 = r.e.recipeUpdates; r.go(harmonic(220, { 0.3 }, 1.0)); long long hops = (long long)(SR / r.e.hopSize());
      CHECK(r.e.recipeUpdates - u0 >= hops - 1);
    }

    // ---------------------------------------------------------------- phase
    { std::puts("- Phase distortion: grit rises, fundamental stays; Phase Rate period");
      auto x = harmonic(220, { 0.25, 0.12, 0.08, 0.06 }, 2.0);
      auto grit = [&](float ph, double& f1) {
          Run r; r.p.phase = ph; auto m = r.go(x).mono(); Spec s; s.run(m, (size_t)(0.5 * SR), 32768, SR);
          double tot = s.bandPower(50, 15000), harm = 0;
          for (int k = 1; k <= 4; ++k) { double a = s.amp(220.0 * k, 40); harm += a * a / 2; }
          s.peak(220, 30, f1); return 10 * std::log10(std::max(1e-12, (tot - harm) / tot));
      };
      double f; double g0 = grit(0, f), prev = g0; std::printf("  non-harmonic energy: phase 0 %.1f dB", g0); CHECK(g0 < -50);
      for (float ph : { 0.3f, 0.6f, 1.0f }) { double g = grit(ph, f); std::printf(", %.1f %.1f dB (f1 %.2f Hz)", ph, g, f); CHECK(g > prev); CHECKM(std::fabs(cents(f, 220)) < (ph < 0.9f ? 3.0 : 6.0), "%.2f", cents(f, 220)); prev = g; }
      std::puts("");
      { Run r; r.p.phase = 0.5f; r.p.phaseRate = 4; r.go(silence(2.0)); std::printf("  free 4 Hz for 2 s: %lld re-scrambles\n", r.e.scrambles); CHECK(std::llabs(r.e.scrambles - 8) <= 1); }
      { Run r; r.p.phase = 0.5f; r.p.phaseSync = true; r.p.phaseDiv = 1; r.ht.valid = true; r.ht.playing = true; r.ht.bpm = 120; r.go(silence(2.0)); std::printf("  synced 1/8 at 120 BPM for 2 s: %lld re-scrambles\n", r.e.scrambles); CHECK(std::llabs(r.e.scrambles - 8) <= 1); }
      // Phase 0 is bit-identical to an untouched engine even with Phase Rate moving
      { Run a, b; b.p.phaseRate = 13; b.p.phaseSync = false; auto oa = a.go(x), ob = b.go(x); CHECK(tu::ident(oa.L, ob.L) == (int)oa.L.size()); }
    }

    // ---------------------------------------------------------------- pad
    { std::puts("- Water / Grain pad: centre neutral, Water wanders, Grain roughens; Glide / Soft smooth");
      auto x = harmonic(220, vowelAmps(220), 2.0);
      auto wanderC = [&](float w, float g) { Run r; r.p.water = w; r.p.grain = g; auto m = r.go(x).mono(); std::vector<double> f; for (double t = 0.4; t < 1.9; t += 0.05) { Spec s; s.run(m, (size_t)(t * SR), 2048, SR); double fr; s.peak(220, 40, fr); f.push_back(cents(fr, 220)); } double mu = 0, sd = 0; for (double v : f) mu += v; mu /= f.size(); for (double v : f) sd += (v - mu) * (v - mu); return std::sqrt(sd / f.size()); };
      auto rough = [&](float w, float g, const std::vector<float>& in) { Run r; r.p.water = w; r.p.grain = g; auto m = r.go(in).mono(); std::vector<double> e; for (double t = 0.4; t < 1.9; t += 0.005) e.push_back(segRms(m, t, t + 0.005)); double s = 0, mu = 0; for (size_t i = 1; i < e.size(); ++i) s += std::fabs(e[i] - e[i - 1]); for (double v : e) mu += v; return s / (e.size() - 1) / (mu / e.size()); };
      double w0 = wanderC(0, 0), w1 = wanderC(1, 0), r0 = rough(0, 0, x), r1 = rough(0, 1, x);
      std::printf("  pitch wander: centre %.2f cents, Water %.2f cents; roughness: centre %.3f, Grain %.3f\n", w0, w1, r0, r1);
      CHECK(w1 > w0 + 3); CHECK(r1 > r0 * 3);
      // tremolo input: Soft smooths the envelope
      std::vector<float> tr = x; for (size_t i = 0; i < tr.size(); ++i) tr[i] *= (float)(0.6 + 0.4 * std::sin(husk::kTwoPi * 9 * i / SR));
      double rt0 = rough(0, 0, tr), rts = rough(0, -1, tr); std::printf("  tremolo roughness: centre %.3f, Soft %.3f\n", rt0, rts); CHECK(rts < rt0);
      Run a, b; b.p.water = 0; b.p.grain = 0; auto oa = a.go(x), ob = b.go(x); CHECK(tu::ident(oa.L, ob.L) == (int)oa.L.size());
    }

    // ---------------------------------------------------------------- air, shimmer, space, crunch
    { std::puts("- Air / Shimmer / Space / Crunch");
      auto tone = harmonic(220, { 0.3, 0.1 }, 1.5); husk::Rng g(21); husk::BandPass b1, b2; b1.set(SR, 5600, 1.4); b2.set(SR, 5600, 1.4);
      std::vector<float> nz(tone.size()); for (auto& v : nz) v = 0.05f * b2.tick(b1.tick(g.bi()));
      auto x = tone; for (size_t i = 0; i < x.size(); ++i) x[i] += nz[i];
      Spec si; si.run(nz, (size_t)(0.5 * SR), 32768, SR); double pin = si.bandPower(4000, 8000);
      auto airP = [&](float air) { Run r; r.p.air = air; auto m = r.go(x).mono(); Spec s; s.run(m, (size_t)(0.5 * SR), 32768, SR); return s.bandPower(4000, 8000); };
      double p0 = airP(0), p5 = airP(0.5f), p10 = airP(1.0f);
      std::printf("  Air: 4-8 kHz residual vs input band: Air 0 %.1f dB, 0.5 %.1f dB, 1 %.1f dB\n", 10 * std::log10(p0 / pin), 10 * std::log10(p5 / pin), 10 * std::log10(p10 / pin));
      CHECK(10 * std::log10(p0 / pin) < -25); CHECK(std::fabs(10 * std::log10(p5 / pin)) < 3); CHECK(p10 > p5 * 2);
      // shimmer: octave-up energy and a tail
      auto sx = concat(harmonic(220, { 0.3 }, 1.0), silence(1.5));
      auto shim = [&](float s, double& oct, double& tail) { Run r; r.p.shimmer = s; auto m = r.go(sx).mono(); Spec q; q.run(m, (size_t)(0.5 * SR), 8192, SR); oct = db(q.amp(440, 30) / q.amp(220, 30)); tail = db(segRms(m, 1.6, 1.8)); };
      double o0, t0, o1, t1; shim(0, o0, t0); shim(1, o1, t1);
      std::printf("  Shimmer: octave-up %.1f -> %.1f dB re f0; tail 0.6 s after input %.1f -> %.1f dBFS\n", o0, o1, t0, t1);
      CHECK(o1 > o0 + 30); CHECK(o1 > -20); CHECK(t1 > -45); CHECK(t0 < -100);
      // space: tail grows with the knob; zero leaves no hall
      auto bx = concat(harmonic(220, { 0.3 }, 0.5), silence(2.0));
      auto tailT = [&](float sp) { Run r; r.p.space = sp; auto m = r.go(bx).mono(); double ref = segRms(m, 0.3, 0.45); double t = 0.5 + (double)r.e.latencySamples() / SR; double last = t; for (double u = t; u < 2.4; u += 0.01) if (segRms(m, u, u + 0.01) > ref * 0.001) last = u; return last - t; };
      double s0 = tailT(0), s3 = tailT(0.3f), s6 = tailT(0.6f), s10 = tailT(1.0f);
      std::printf("  Space: -60 dB tail after input stops: 0 -> %.2f s, 0.3 -> %.2f s, 0.6 -> %.2f s, 1 -> %.2f s\n", s0, s3, s6, s10);
      CHECK(s0 < 0.05); CHECK(s3 > s0); CHECK(s6 > s3); CHECK(s10 > s6); CHECK(s10 < 2.0);
      // crunch: THD rises
      auto thd = [&](float c) { Run r; r.p.crunch = c; auto m = r.go(harmonic(220, { 0.3 }, 1.2)).mono(); Spec s; s.run(m, (size_t)(0.5 * SR), 16384, SR); double a1 = s.amp(220, 40), h = 0; for (int k = 2; k <= 9; ++k) { double a = s.amp(220.0 * k, 40); h += a * a; } return std::sqrt(h) / a1 * 100; };
      double prev = thd(0); std::printf("  Crunch THD: 0 -> %.4f%%", prev); CHECK(prev < 0.1);
      for (float c : { 0.25f, 0.5f, 0.75f, 1.0f }) { double t = thd(c); std::printf(", %.2f -> %.2f%%", c, t); CHECK(t > prev); prev = t; }
      std::puts("");
      Run a, b; b.p.crunch = 0; auto oa = a.go(tone), ob = b.go(tone); CHECK(tu::ident(oa.L, ob.L) == (int)oa.L.size());
    }

    // ---------------------------------------------------------------- safety
    { std::puts("- Safety ceiling: stress cases never exceed 0 dBTP; modest signals untouched");
      auto vox = harmonic(196, vowelAmps(196), 2.0, SR, 4.0);  // +12 dB hot voice (peaks well over 0 dBFS)
      float inPk = tu::peak(vox);
      Run r; r.p.chord = true; for (int n : { 43, 48, 52, 55, 59, 62 }) r.e.noteOn(n);
      r.p.crunch = 1; r.p.outDb = 12; r.p.shimmer = 1; r.p.space = 1; r.p.model = Choir; r.p.air = 1; r.p.phase = 1; r.p.grain = 1;
      auto o = r.go(vox);
      float tp = std::max(tu::truePeak(o.L), tu::truePeak(o.R));
      std::printf("  hot chord+crunch+out +12 dB: input peak %.1f dBFS, output true peak %.2f dBTP, limiter min gain %.1f dB\n", db(inPk), db(tp), db(r.e.minLimGain));
      CHECK(tu::finite(o.L) && tu::finite(o.R)); CHECK(tp <= 1.0f);
      Run h; h.p.mix = 0; h.p.outDb = 12; auto oh = h.go(vox); float tph = std::max(tu::truePeak(oh.L), tu::truePeak(oh.R));
      std::printf("  hot dry at Mix 0 + Out +12 dB: true peak %.2f dBTP\n", db(tph)); CHECK(tph <= 1.0f);
      // impulsive/square stress
      std::vector<float> sq((size_t)SR); for (size_t i = 0; i < sq.size(); ++i) sq[i] = ((i / 50) % 2 ? 1.f : -1.f) * 2.0f;
      Run s2; s2.p.mix = 0.5f; s2.p.outDb = 12; auto os = s2.go(sq); float tps = std::max(tu::truePeak(os.L), tu::truePeak(os.R));
      std::printf("  +6 dBFS square, Mix 50%%, Out +12: true peak %.2f dBTP\n", db(tps)); CHECK(tps <= 1.0f);
      Run m; m.p.mix = 0.5f; auto om = m.go(harmonic(196, vowelAmps(196), 1.0, SR, 0.25)); CHECK(m.e.limiter().engaged == 0);
      std::printf("  -24 dBFS-rms voice at Mix 50%%: limiter engaged on %d samples\n", m.e.limiter().engaged);
    }

    // ---------------------------------------------------------------- fuzz: NaN, denormals, peaks
    { std::puts("- Random automation fuzz (all controls, 30 s): finite, no subnormals, under 0 dBTP");
      Run r; husk::Rng g(99); size_t len = (size_t)(30 * SR); std::vector<float> x(len);
      for (size_t i = 0; i < len; ++i) { double t = i / SR; int seg = (int)(t * 2) % 5;
          x[i] = seg == 0 ? 0.0f : seg == 1 ? (float)(0.5 * std::sin(husk::kTwoPi * 180 * t) + 0.3 * std::sin(husk::kTwoPi * 360 * t)) : seg == 2 ? 0.7f * g.bi() : seg == 3 ? (i % 4410 == 0 ? 1.0f : 1e-30f) : 1.5f; }
      r.perBlock = [&](Run& rr, size_t) {
          auto& p = rr.p; if (g.uni() < 0.2f) {
          p.model = (int)(g.uni() * 7); p.partials = 1 + 63 * g.uni(); p.air = g.uni(); p.pitch = 24 * g.uni() - 12; p.octave = (int)(g.uni() * 3) - 1; p.formant = 24 * g.uni() - 12;
          p.phase = g.uni(); p.phaseRate = 0.05f + 20 * g.uni(); p.phaseSync = g.uni() < 0.5f; p.phaseDiv = (int)(g.uni() * 6); p.steps = (int)(g.uni() * 7);
          p.water = g.bi(); p.grain = g.bi(); p.freeze = g.uni() < 0.3f; p.chord = g.uni() < 0.5f; p.shimmer = g.uni(); p.space = g.uni(); p.crunch = g.uni(); p.mix = g.uni(); p.outDb = 36 * g.uni() - 24; p.ab = g.uni() < 0.1f; p.bypass = g.uni() < 0.05f; }
          if (g.uni() < 0.1f) rr.e.noteOn(36 + (int)(g.uni() * 48)); if (g.uni() < 0.08f) rr.e.noteOff(36 + (int)(g.uni() * 48)); if (g.uni() < 0.01f) rr.e.allNotesOff();
          rr.block = 1 + (int)(g.uni() * 2047); rr.ht.valid = g.uni() < 0.8f; rr.ht.playing = g.uni() < 0.7f; rr.ht.bpm = 60 + 140 * g.uni();
      };
      auto o = r.go(x);
      // bypass/A-B pass the +3.5 dBFS DC section untouched (that is the point of bypass), so judge peaks with those off
      Run q; q.perBlock = r.perBlock; g = husk::Rng(99); auto o2 = q.go(x);
      bool fin = tu::finite(o.L) && tu::finite(o.R), sub = tu::hasSubnormal(o.L) || tu::hasSubnormal(o.R);
      std::printf("  finite %d, subnormal %d, sample peak %.2f dBFS\n", fin, sub, db(tu::peak2(o.L, o.R)));
      CHECK(fin); CHECK(!sub);
      Run z; z.perBlock = [&](Run& rr, size_t s) { r.perBlock(rr, s); rr.p.bypass = false; rr.p.ab = false; }; g = husk::Rng(7);
      auto xz = concat(x, silence(1.0));   // end in silence so the meter sees no truncated edge
      auto oz = z.go(xz); float tp = std::max(tu::truePeak(oz.L), tu::truePeak(oz.R));
      std::printf("  without bypass/A-B: true peak %.2f dBTP, sample peak %.2f dBFS\n", db(tp), db(tu::peak2(oz.L, oz.R)));
      CHECK(tp <= 1.0f); CHECK(tu::peak2(oz.L, oz.R) <= 0.97f); CHECK(tu::finite(oz.L));
    }

    // ---------------------------------------------------------------- higher sample rates
    { std::puts("- 88.2 / 96 kHz round trip");
      for (double sr : { 88200.0, 96000.0 }) {
          Run r(sr); r.srate = sr; std::vector<double> amps; for (int k = 1; k <= 8; ++k) amps.push_back(0.25 / k);
          auto x = harmonic(220, amps, 1.0, sr); auto m = r.go(x).mono(); Spec s; s.run(m, (size_t)(0.4 * sr), 32768, sr); double wc = 0;
          for (int k = 1; k <= 8; ++k) { double fr; s.peak(220.0 * k, 60, fr); wc = std::max(wc, std::fabs(cents(fr, 220.0 * k))); }
          double lv = db(segRms(m, 0.4, 0.9, sr) / segRms(x, 0.4, 0.9, sr));
          std::printf("  %g Hz: worst partial %.3f cents, level %.2f dB\n", sr, wc, lv); CHECK(wc < 2); CHECK(std::fabs(lv) < 0.5);
      } }

    // ---------------------------------------------------------------- CPU
    { std::puts("- CPU budget (single core, this machine)");
      auto vox = harmonic(165, vowelAmps(165), 10.0);
      auto bench = [&](const char* name, std::function<void(Run&)> cfg, double budget) {
          Run r; cfg(r); auto t0 = std::chrono::steady_clock::now(); r.go(vox); double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          double pct = el / 10.0 * 100; std::printf("  %-44s %.2f%% of one core (budget %.0f%%)\n", name, pct, budget); CHECKM(pct < budget, "%s %.2f", name, pct); };
      bench("default (Sine, 64 partials, Air)", [](Run& r) { r.p.air = 0.5f; }, 15);
      bench("default + Shimmer 0.4 + Space 0.4", [](Run& r) { r.p.air = 0.5f; r.p.shimmer = 0.4f; r.p.space = 0.4f; }, 15);
      bench("Choir + Water + Phase 0.5", [](Run& r) { r.p.model = Choir; r.p.water = 0.6f; r.p.phase = 0.5f; r.p.air = 0.5f; }, 40);
      bench("Chord 6 notes + Choir + Phase 1 (worst case)", [](Run& r) { r.p.chord = true; r.p.model = Choir; r.p.phase = 1; for (int n : { 48, 52, 55, 59, 62, 64 }) r.e.noteOn(n); }, 100);
    }

    std::printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

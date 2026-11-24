// HUSK v2 "not static" quality gates. Every check here fails the build when it misses its target.
// Inputs: a real unaccompanied sung vocal (tests/data/welsh_vocal_12s.wav, CC BY-SA 4.0) and a
// generated sung phrase (vowels, vibrato, glides, consonants). Optional extra WAVs are reported only.
#include "Quality.h"
#include "TestUtil.h"
#include <chrono>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif
using namespace q;

static husk::Params P(std::initializer_list<std::pair<const char*, float>> kv = {}) { husk::Params p; for (auto& e : kv) setById(p, e.first, e.second); return p; }
static double sideToMidDb(const Stereo& s) { double m = 0, d = 0; for (size_t i = 0; i < s.L.size(); ++i) { double a = 0.5 * (s.L[i] + s.R[i]), b = 0.5 * (s.L[i] - s.R[i]); m += a * a; d += b * b; } return std::max(-60.0, 10.0 * std::log10((d + 1e-12) / (m + 1e-12))); }   // floor: a mono signal reads -60 dB

struct Report { std::string name; double value, target; bool higher; bool pass; };
static std::vector<Report> rep;
static void gate(const std::string& name, double v, double target, bool higherIsBetter = true) {
    bool ok = higherIsBetter ? v >= target : v < target;
    rep.push_back({ name, v, target, higherIsBetter, ok });
    CHECKM(ok, "%s = %.3f (target %s %.3f)", name.c_str(), v, higherIsBetter ? ">=" : "<", target);
    std::printf("  %-58s %9.3f  (%s %.2f)  %s\n", name.c_str(), v, higherIsBetter ? ">=" : "<", target, ok ? "ok" : "FAIL");
}
static void note(const std::string& name, double v) { std::printf("  %-58s %9.3f  (report only)\n", name.c_str(), v); }

struct Input { std::string name; std::vector<float> x; std::vector<float> f0; bool real; bool gated; };

static void suite(const Input& in) {
    std::printf("\n== %s (%s, %.1f s)\n", in.name.c_str(), in.real ? "real vocal" : "generated", in.x.size() / SR);
    auto G = [&](const std::string& n, double v, double t, bool hi = true) { if (in.gated) gate(in.name + ": " + n, v, t, hi); else note(in.name + ": " + n, v); };
    const auto Md = mel(in.x); const auto act = active(Md); const auto Fd = fine(in.x); const auto actF = active(Fd);
    auto base = render(in.x, P()); auto bm = base.mono(); auto Mb = mel(bm); auto Fb = fine(bm);

    // 1. pitch follows the singer
    auto ref = pitchTrack(in.x); auto out = pitchTrack(bm);
    long n = 0, ok = 0, refv = 0, cov = 0;
    for (size_t t = 0; t < std::min(ref.size(), out.size()); ++t) {
        float r = 0;
        if (!in.f0.empty()) { size_t s = t * 512; if (s < in.f0.size() && in.f0[s] > 0 && (s < 1024 || in.f0[s - 1024] > 0) && (s + 1024 >= in.f0.size() || in.f0[s + 1024] > 0)) r = in.f0[s]; }
        else if (ref[t].sure) r = ref[t].f0;
        if (r <= 0 || !act[std::min(t, act.size() - 1)]) continue;
        ++refv; if (!out[t].sure) continue; ++cov;
        ++n; if (std::fabs(1200.0 * std::log2(out[t].f0 / r)) < 25.0) ++ok;
    }
    G("pitch: output within 25 cents of the singer (fraction)", n ? (double)ok / n : 0.0, 0.95);
    G("pitch: sung frames that come out as a clear note (fraction)", refv ? (double)cov / refv : 0.0, 0.80);
    // 1b. Note Snap: locked to the scale
    { auto s = render(in.x, P({ { "snap", 1 }, { "scale", husk::Major }, { "key", 0 } })).mono(); auto o = pitchTrack(s); long a = 0, b = 0;
      for (size_t t = 0; t < o.size(); ++t) if (o[t].sure && t < act.size() && act[t]) { ++a; double m = 69.0 + 12.0 * std::log2(o[t].f0 / 440.0); int nn = (int)std::lround(m); int pc = ((nn % 12) + 12) % 12; bool inScale = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11; if (inScale && std::fabs(m - nn) * 100.0 < 25.0) ++b; }
      G("snap: output locked to a C-major note within 25 cents (fraction)", a ? (double)b / a : 0.0, 0.95); }
    // 2. loudness follows
    { auto a = rmsDb(in.x), b = rmsDb(bm); std::vector<double> x, y; for (size_t t = 0; t < std::min({ a.size(), b.size(), act.size() }); ++t) if (act[t]) { x.push_back(a[t]); y.push_back(b[t]); }
      G("loudness: correlation with the singer", corr(x, y), 0.90); }
    // 3. hiss on held notes
    { auto noAir = render(in.x, P({ { "air", 0 } })).mono(); double eN = 0, eA = 0;
      for (size_t t = 0; t < ref.size(); ++t) {
          bool held = in.f0.empty() ? ref[t].sure : (t * 512 < in.f0.size() && in.f0[t * 512] > 0);
          if (!held || t >= act.size() || !act[t]) continue;
          for (long j = (long)t * 512 - 256; j < (long)t * 512 + 256; ++j) if (j >= 0 && j < (long)bm.size()) { double d = bm[(size_t)j] - noAir[(size_t)j]; eN += d * d; eA += (double)bm[(size_t)j] * bm[(size_t)j]; }
      }
      G("hiss: noise share of the sound on held notes (percent)", 100.0 * eN / (eA + 1e-20), 10.0, false); }
    // 4. the default is an instrument, not the voice
    G("default timbre distance from the dry voice (dB)", dist(Mb, Md, act), 10.0);
    // 5. presets clearly different
    { int np = 0; auto* ps = husk::presets(np); std::vector<std::vector<std::vector<float>>> M; for (int i = 0; i < np; ++i) { husk::Params p; applyPreset(p, i); M.push_back(mel(render(in.x, p).mono())); }
      double mn = 1e9; int ia = 0, ib = 0; for (int i = 0; i < np; ++i) for (int j = i + 1; j < np; ++j) { double d = dist(M[(size_t)i], M[(size_t)j], act); if (d < mn) { mn = d; ia = i; ib = j; } }
      std::printf("  closest presets: %s / %s\n", ps[ia].name, ps[ib].name);
      G("presets: smallest distance between any two (dB)", mn, 6.0);
      for (int i = 1; i < np; ++i) if (std::string(ps[i].name) == "Ghost") note(in.name + ": Ghost preset distance from dry (dB)", dist(M[(size_t)i], Md, act)); }
    // 6. every control at full moves the sound
    struct C { const char* name; husk::Params p; husk::Params b; std::vector<int> notes; };
    std::vector<C> cs = {
        { "Partials 1", P({ { "partials", 1 } }), P(), {} }, { "Talk 0", P({ { "talk", 0 } }), P(), {} },
        { "Pitch +12", P({ { "pitch", 12 } }), P(), {} }, { "Pitch -12", P({ { "pitch", -12 } }), P(), {} }, { "Octave -1", P({ { "octave", -1 } }), P(), {} }, { "Octave +1", P({ { "octave", 1 } }), P(), {} },
        { "Formant +12", P({ { "formant", 12 } }), P(), {} }, { "Formant -12", P({ { "formant", -12 } }), P(), {} },
        { "Phase 100%", P({ { "phase", 1 } }), P(), {} }, { "Phase Rate 20 Hz (Phase on)", P({ { "phase", 1 }, { "phase_rate", 20 } }), P({ { "phase", 1 } }), {} },
        { "Phase Rate synced 1/32 (Phase on)", P({ { "phase", 1 }, { "phase_sync", 1 }, { "phase_div", 5 } }), P({ { "phase", 1 }, { "phase_rate", 0.05f } }), {} },
        { "Unison 7", P({ { "unison", 7 } }), P(), {} }, { "Detune 100%", P({ { "detune", 1 } }), P(), {} }, { "Drift 100%", P({ { "drift", 1 } }), P(), {} },
        { "Shimmer 100%", P({ { "shimmer", 1 } }), P(), {} }, { "Space 100%", P({ { "space", 1 } }), P(), {} }, { "Crunch 100%", P({ { "crunch", 1 } }), P(), {} },
        { "Steps 1/4", P({ { "steps", 1 } }), P(), {} }, { "Steps 1/32", P({ { "steps", 6 } }), P(), {} }, { "Freeze", P({ { "freeze", 1 } }), P(), {} }, { "Glide 1000 ms", P({ { "glide", 1000 } }), P(), {} },
        { "Note Snap (C major)", P({ { "snap", 1 }, { "scale", 1 } }), P(), {} }, { "Key D vs C (Snap major)", P({ { "snap", 1 }, { "scale", 1 }, { "key", 2 } }), P({ { "snap", 1 }, { "scale", 1 } }), {} },
        { "Scale minor pent. vs chromatic (Snap)", P({ { "snap", 1 }, { "scale", 4 } }), P({ { "snap", 1 } }), {} },
        { "Chord C-E-G (MIDI)", P({ { "chord", 1 } }), P(), { 48, 52, 55 } }, { "Mix 0", P({ { "mix", 0 } }), P(), {} },
    };
    for (int m = 1; m < husk::kNumModels; ++m) cs.push_back({ husk::modelName(m), P({ { "model", (float)m } }), P(), {} });
    for (auto& c : cs) {
        auto y = render(in.x, c.p, c.notes).mono(); bool def = c.b.model == husk::Params().model && !c.b.phaseSync && c.b.phase == 0 && !c.b.snap; auto b = def ? bm : render(in.x, c.b).mono();
        double d1 = dist(mel(y), mel(b), act), d2 = dist(fine(y), fine(b), actF);
        G(std::string("control: ") + c.name + " (dB, broad|fine " + std::to_string((int)std::lround(d1 * 10) / 10.0).substr(0, 4) + "|" + std::to_string((int)std::lround(d2 * 10) / 10.0).substr(0, 4) + ")", std::max(d1, d2), 3.0);
    }
    { auto w0 = render(in.x, P({ { "width", 0 } })), w1 = render(in.x, P({ { "width", 1 } }));
      G("control: Width 0 -> 100% (side/mid change, dB)", sideToMidDb(w1) - sideToMidDb(w0), 3.0); }
    { auto o = render(in.x, P({ { "out", -24 } })).mono(); G("control: Out -24 dB (level change, dB)", 10.0 * std::log10((tu::rms(bm) * tu::rms(bm) + 1e-20) / (tu::rms(o) * tu::rms(o) + 1e-20)), 3.0); }
}

int main(int argc, char** argv) {
#if defined(__x86_64__) || defined(_M_X64)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("HUSK quality tests (not-static gates)");
    std::string data = "tests/data"; std::vector<std::string> extra;
    for (int i = 1; i < argc; ++i) { std::string a = argv[i]; if (a == "--data" && i + 1 < argc) data = argv[++i]; else if (a == "--extra" && i + 1 < argc) extra.push_back(argv[++i]); }
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Input> ins;
    { Input r; double sr = 0; r.name = "real vocal (Welsh solo)"; r.real = true; r.gated = true;
      bool ok = readWav(data + "/welsh_vocal_12s.wav", r.x, sr); CHECKM(ok && sr == SR, "cannot read %s/welsh_vocal_12s.wav", data.c_str()); if (ok) ins.push_back(r); }
    { auto v = phrase(); Input s; s.name = "generated phrase"; s.x = v.x; s.f0 = v.f0; s.real = false; s.gated = true; ins.push_back(s); }
    for (auto& e : extra) { Input r; double sr = 0; if (readWav(e, r.x, sr) && sr == SR) { r.name = "extra: " + e; r.real = true; r.gated = false; ins.push_back(r); } }
    for (auto& in : ins) suite(in);

    std::puts("\n== vowels (one sung pitch, five vowels)");
    { auto x = vowels(); auto y = render(x, P()).mono(); auto My = mel(y), Mx = mel(x); double mn = 1e9, mni = 1e9;
      for (int s = 0; s < 4; ++s) {
          size_t a0 = (size_t)((s * 1.5 + 0.3) * SR / 512), a1 = (size_t)((s * 1.5 + 1.3) * SR / 512), b0 = (size_t)(((s + 1) * 1.5 + 0.3) * SR / 512), b1 = (size_t)(((s + 1) * 1.5 + 1.3) * SR / 512);
          auto A = ltas(My, a0, a1), B = ltas(My, b0, b1), Ai = ltas(Mx, a0, a1), Bi = ltas(Mx, b0, b1); double d = 0, di = 0;
          for (size_t k = 0; k < A.size(); ++k) { d += std::fabs(A[k] - B[k]); di += std::fabs(Ai[k] - Bi[k]); } d /= A.size(); di /= A.size(); mn = std::min(mn, d); mni = std::min(mni, di);
      }
      gate("vowels: smallest output change between neighbouring vowels (dB)", mn, 4.0); note("vowels: same measure on the input voice (dB)", mni); }
    std::puts("\n== Air (consonants), generated phrase, consonant moments only");
    { auto v = phrase(); auto b = render(v.x, P()).mono(), a = render(v.x, P({ { "air", 1 } })).mono(), z = render(v.x, P({ { "air", 0 } })).mono();
      auto Mb = mel(b), Ma = mel(a), Mz = mel(z); std::vector<bool> cf(Mb.size(), false);
      static const double st[5] = { 0.0, 1.3, 2.6, 3.5, 5.3 }; for (double s : st) for (double t = s - 0.03; t < s + 0.03; t += 0.005) { long f = (long)std::lround(t * SR / 512); if (f >= 0 && f < (long)cf.size()) cf[(size_t)f] = true; }
      gate("control: Air 100% on consonants (dB)", dist(Ma, Mb, cf), 3.0); note("Air 0% on consonants (dB from default)", dist(Mz, Mb, cf)); }

    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int fails = 0; for (auto& r : rep) if (!r.pass) ++fails;
    std::printf("\nquality gates: %d passed, %d failed (%zu gates) in %.1f s\n", (int)rep.size() - fails, fails, rep.size(), el);
    std::printf("checks: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

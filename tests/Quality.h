// HUSK v2 sound-quality measurement helpers (no JUCE): WAV reading, rendering through the engine,
// mel / fine spectral distances, a YIN pitch meter, loudness envelopes and test-voice generators.
#pragma once
#include "HuskEngine.h"
#include "HuskPresets.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace q {
using husk::kTwoPi; using husk::kPi;
constexpr double SR = 44100.0;

// ---------------------------------------------------------------- WAV (PCM16/24/32, float32; mono or stereo -> mono)
inline bool readWav(const std::string& path, std::vector<float>& out, double& sr) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    std::vector<unsigned char> d; unsigned char buf[65536]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    size_t p = 12; int fmt = 0, ch = 0, bits = 0; unsigned rate = 0; const unsigned char* data = nullptr; size_t dlen = 0;
    auto u16 = [&](size_t o) { return (unsigned)d[o] | ((unsigned)d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    while (p + 8 <= d.size()) {
        size_t len = u32(p + 4);
        if (!std::memcmp(d.data() + p, "fmt ", 4)) { fmt = (int)u16(p + 8); ch = (int)u16(p + 10); rate = u32(p + 12); bits = (int)u16(p + 22); if (fmt == 0xFFFE) fmt = (int)u16(p + 32); }
        else if (!std::memcmp(d.data() + p, "data", 4)) { data = d.data() + p + 8; dlen = std::min(len, d.size() - p - 8); }
        p += 8 + len + (len & 1);
    }
    if (!data || ch < 1 || bits == 0) return false;
    int bps = bits / 8; size_t frames = dlen / (size_t)(bps * ch); out.assign(frames, 0.0f); sr = rate;
    for (size_t i = 0; i < frames; ++i) {
        double s = 0;
        for (int c = 0; c < ch; ++c) {
            const unsigned char* q = data + (i * (size_t)ch + (size_t)c) * (size_t)bps; double v = 0;
            if (fmt == 3 && bits == 32) { float fv; std::memcpy(&fv, q, 4); v = fv; }
            else if (bits == 16) v = (int16_t)(q[0] | (q[1] << 8)) / 32768.0;
            else if (bits == 24) { int32_t iv = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); v = (iv >> 8) / 8388608.0; }
            else if (bits == 32) { int32_t iv; std::memcpy(&iv, q, 4); v = iv / 2147483648.0; }
            s += v;
        }
        out[i] = (float)(s / ch);
    }
    return true;
}

// ---------------------------------------------------------------- rendering
struct Stereo { std::vector<float> L, R; std::vector<float> mono() const { std::vector<float> m(L.size()); for (size_t i = 0; i < L.size(); ++i) m[i] = 0.5f * (L[i] + R[i]); return m; } };
using Setter = std::function<void(husk::Params&)>;
inline void applyPreset(husk::Params& p, int idx);
inline void setById(husk::Params& p, const std::string& k, float v) {
    if (k == "model") p.model = (int)v; else if (k == "partials") p.partials = v; else if (k == "air") p.air = v; else if (k == "pitch") p.pitch = v; else if (k == "talk") p.talk = v;
    else if (k == "octave") p.octave = (int)v; else if (k == "formant") p.formant = v; else if (k == "phase") p.phase = v; else if (k == "phase_rate") p.phaseRate = v;
    else if (k == "steps") p.steps = (int)v; else if (k == "freeze") p.freeze = v > 0.5f; else if (k == "snap") p.snap = v > 0.5f; else if (k == "key") p.key = (int)v; else if (k == "scale") p.scale = (int)v;
    else if (k == "glide") p.glideMs = v; else if (k == "unison") p.unison = (int)v; else if (k == "detune") p.detune = v; else if (k == "width") p.width = v; else if (k == "drift") p.drift = v;
    else if (k == "shimmer") p.shimmer = v; else if (k == "space") p.space = v; else if (k == "crunch") p.crunch = v; else if (k == "mix") p.mix = v; else if (k == "out") p.outDb = v;
    else if (k == "chord") p.chord = v > 0.5f; else if (k == "phase_sync") p.phaseSync = v > 0.5f; else if (k == "phase_div") p.phaseDiv = (int)v;
    else { std::printf("unknown param %s\n", k.c_str()); }
}
inline void applyPreset(husk::Params& p, int idx) { int n = 0; auto* ps = husk::presets(n); for (int j = 0; j < ps[idx].n; ++j) setById(p, ps[idx].v[j].id, ps[idx].v[j].value); }
inline Stereo render(const std::vector<float>& in, const husk::Params& p, const std::vector<int>& chordNotes = {}, double sr = SR, husk::Engine* keep = nullptr) {
    husk::Engine local; husk::Engine& e = keep ? *keep : local;
    e.prepare(sr, 512); e.setParams(p);
    for (int nn : chordNotes) e.noteOn(nn);
    const int L = e.latencySamples(); Stereo o; o.L.assign(in.size() + (size_t)L, 0.0f);
    std::copy(in.begin(), in.end(), o.L.begin()); o.R = o.L;
    husk::HostTime ht; ht.valid = true; ht.playing = true; ht.bpm = 120;
    for (size_t s = 0; s < o.L.size(); s += 512) {
        int n = (int)std::min<size_t>(512, o.L.size() - s); e.setTime(ht);
        e.process(o.L.data() + s, o.R.data() + s, n); ht.ppq += n / sr * 2.0;
    }
    o.L.erase(o.L.begin(), o.L.begin() + L); o.R.erase(o.R.begin(), o.R.begin() + L);
    return o;
}

// ---------------------------------------------------------------- spectra
struct Spectro {
    int nfft, hop; std::vector<std::vector<float>> fr;  // frames x bins (dB)
};
inline const husk::FFT& fftOf(int n) { static std::map<int, husk::FFT> m; auto it = m.find(n); if (it == m.end()) { husk::FFT f; f.init(n); it = m.emplace(n, f).first; } return it->second; }
inline std::vector<std::vector<float>> power(const std::vector<float>& x, int nfft, int hop) {
    const auto& F = fftOf(nfft); std::vector<float> re((size_t)nfft), im((size_t)nfft), w((size_t)nfft);
    for (int i = 0; i < nfft; ++i) w[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * i / nfft));
    std::vector<std::vector<float>> out; long half = nfft / 2;
    for (long c = 0; c <= (long)x.size(); c += hop) {   // centred frames, zero padded
        for (int i = 0; i < nfft; ++i) { long j = c - half + i; re[(size_t)i] = (j >= 0 && j < (long)x.size()) ? x[(size_t)j] * w[(size_t)i] : 0.0f; im[(size_t)i] = 0; }
        F.forward(re.data(), im.data());
        std::vector<float> p((size_t)half + 1); for (int k = 0; k <= half; ++k) p[(size_t)k] = re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k];
        out.push_back(std::move(p));
    }
    return out;
}
inline double hzToMel(double f) { const double fsp = 200.0 / 3, minLog = 1000.0, minLogMel = minLog / fsp, step = std::log(6.4) / 27.0; return f < minLog ? f / fsp : minLogMel + std::log(f / minLog) / step; }
inline double melToHz(double m) { const double fsp = 200.0 / 3, minLog = 1000.0, minLogMel = minLog / fsp, step = std::log(6.4) / 27.0; return m < minLogMel ? m * fsp : minLog * std::exp(step * (m - minLogMel)); }
// 64-band Slaney mel spectrogram in dB (2048 / 512), like the diagnosis scripts.
inline std::vector<std::vector<float>> mel(const std::vector<float>& x, double sr = SR) {
    const int nfft = 2048, hop = 512, nm = 64; const double fmax = 16000;
    static std::vector<std::vector<float>> fb; static double fbSr = 0;
    if (fbSr != sr) {
        fbSr = sr; fb.assign(nm, std::vector<float>(nfft / 2 + 1, 0.0f));
        double m0 = hzToMel(0), m1 = hzToMel(fmax); std::vector<double> pts(nm + 2);
        for (int i = 0; i < nm + 2; ++i) pts[(size_t)i] = melToHz(m0 + (m1 - m0) * i / (nm + 1));
        for (int m = 0; m < nm; ++m) for (int k = 0; k <= nfft / 2; ++k) {
            double f = k * sr / nfft, lo = pts[(size_t)m], c = pts[(size_t)m + 1], hi = pts[(size_t)m + 2];
            double v = std::max(0.0, std::min((f - lo) / (c - lo), (hi - f) / (hi - c)));
            fb[(size_t)m][(size_t)k] = (float)(v * 2.0 / (hi - lo));
        }
    }
    auto P = power(x, nfft, hop); std::vector<std::vector<float>> out;
    for (auto& p : P) { std::vector<float> m((size_t)nm); for (int b = 0; b < nm; ++b) { double s = 0; for (int k = 0; k <= nfft / 2; ++k) s += fb[(size_t)b][(size_t)k] * p[(size_t)k]; m[(size_t)b] = (float)(10.0 * std::log10(s + 1e-10)); } out.push_back(std::move(m)); }
    return out;
}
// fine harmonic-detail spectrum: 4096 / 1024, 60..5000 Hz, floored 60 dB below each frame's peak
inline std::vector<std::vector<float>> fine(const std::vector<float>& x, double sr = SR) {
    auto P = power(x, 4096, 1024); int k0 = (int)std::ceil(60.0 * 4096 / sr), k1 = (int)std::floor(5000.0 * 4096 / sr);
    std::vector<std::vector<float>> out;
    for (auto& p : P) { std::vector<float> v; float mx = -1e9f; for (int k = k0; k <= k1; ++k) { float d = (float)(10.0 * std::log10(p[(size_t)k] + 1e-10)); v.push_back(d); mx = std::max(mx, d); } for (auto& d : v) d = std::max(d, mx - 60.0f); out.push_back(std::move(v)); }
    return out;
}
inline std::vector<bool> active(const std::vector<std::vector<float>>& S, float below = 45.0f) {
    float g = -1e9f; for (auto& f : S) for (float v : f) g = std::max(g, v);
    std::vector<bool> a; for (auto& f : S) { float m = -1e9f; for (float v : f) m = std::max(m, v); a.push_back(m > g - below); } return a;
}
// level-normalised frame-wise distance (mean |dB difference| after removing each frame's mean)
inline double dist(const std::vector<std::vector<float>>& A, const std::vector<std::vector<float>>& B, const std::vector<bool>& act) {
    double s = 0; long n = 0; size_t F = std::min({ A.size(), B.size(), act.size() });
    for (size_t t = 0; t < F; ++t) {
        if (!act[t]) continue;
        const auto& a = A[t]; const auto& b = B[t]; double ma = 0, mb = 0; for (size_t k = 0; k < a.size(); ++k) { ma += a[k]; mb += b[k]; } ma /= a.size(); mb /= b.size();
        for (size_t k = 0; k < a.size(); ++k) { s += std::fabs((a[k] - ma) - (b[k] - mb)); ++n; }
    }
    return n ? s / n : 0.0;
}
// long-term (average) spectrum of a time range, level-normalised
inline std::vector<double> ltas(const std::vector<std::vector<float>>& S, size_t f0, size_t f1) {
    std::vector<double> acc(S[0].size(), 0.0); for (size_t t = f0; t < std::min(f1, S.size()); ++t) for (size_t k = 0; k < acc.size(); ++k) acc[k] += std::pow(10.0, S[t][k] / 10.0);
    double m = 0; for (auto& v : acc) { v = 10.0 * std::log10(v / std::max<size_t>(1, f1 - f0) + 1e-12); m += v; } m /= acc.size(); for (auto& v : acc) v -= m; return acc;
}

// ---------------------------------------------------------------- pitch meter (YIN, 2048 / 512) and loudness
struct PitchFrame { float f0 = 0; bool sure = false; };
inline std::vector<PitchFrame> pitchTrack(const std::vector<float>& x, double sr = SR) {
    const int W = 1024, tauMax = (int)(sr / 60.0), tauMin = (int)(sr / 1000.0); std::vector<PitchFrame> out; std::vector<double> d((size_t)tauMax + 2);
    for (long c = 0; c <= (long)x.size(); c += 512) {
        PitchFrame pf; long s0 = c - (W + tauMax) / 2;
        if (s0 >= 0 && s0 + W + tauMax + 1 < (long)x.size()) {
            double e = 0; for (int j = 0; j < W; ++j) e += (double)x[(size_t)(s0 + j)] * x[(size_t)(s0 + j)];
            if (e / W > 1e-7) {
                double run = 0; d[0] = 1;
                for (int t = 1; t <= tauMax; ++t) { double s = 0; for (int j = 0; j < W; ++j) { double v = x[(size_t)(s0 + j)] - x[(size_t)(s0 + j + t)]; s += v * v; } run += s; d[(size_t)t] = run > 0 ? s * t / run : 1.0; }
                int best = -1; for (int t = tauMin; t < tauMax; ++t) if (d[(size_t)t] < 0.12) { while (t + 1 < tauMax && d[(size_t)t + 1] < d[(size_t)t]) ++t; best = t; break; }
                if (best > tauMin && best < tauMax - 1) {
                    double a = d[(size_t)best - 1], b = d[(size_t)best], cc = d[(size_t)best + 1], den = a - 2 * b + cc, p = den > 1e-12 ? 0.5 * (a - cc) / den : 0.0;
                    pf.f0 = (float)(sr / (best + std::clamp(p, -0.5, 0.5))); pf.sure = b < 0.12;
                }
            }
        }
        out.push_back(pf);
    }
    // Octave decision by subharmonic summation (Hermes 1988). YIN finds the waveform period, but a
    // quiet sub-octave layer doubles that period and a strong formant on the 2nd harmonic can halve
    // it, while the ear follows the whole harmonic series. Score f/2, f and 2f by weighted harmonic
    // amplitude (weight 0.84^k up to 5 kHz) and keep the best. Same meter for input and output.
    auto S = power(x, 4096, 512);
    for (size_t t = 0; t < out.size() && t < S.size(); ++t) {
        if (out[t].f0 <= 0) continue; const double f = out[t].f0;
        auto peak = [&](double fr) { int c = (int)std::lround(fr * 4096 / sr), w = std::max(1, (int)std::lround(fr * 0.02 * 4096 / sr)); double m = 0; for (int k = c - w; k <= c + w; ++k) if (k > 0 && k < (int)S[t].size()) m = std::max(m, (double)S[t][(size_t)k]); return std::sqrt(m); };
        auto score = [&](double c) { double sc = 0, w = 1; for (int k = 1; k * c < 5000.0; ++k) { w *= 0.84; sc += w * peak(k * c); } return sc; };
        double best = f, bs = score(f);
        for (double c : { f / 2, 2 * f }) { if (c < 50 || c > 1200) continue; double sc = score(c); if (sc > bs) { best = c; bs = sc; } }
        out[t].f0 = (float)best;
    }
    return out;
}
inline std::vector<double> rmsDb(const std::vector<float>& x) {
    std::vector<double> out; for (long c = 0; c <= (long)x.size(); c += 512) { double e = 0; int n = 0; for (long j = c - 1024; j < c + 1024; ++j) if (j >= 0 && j < (long)x.size()) { e += (double)x[(size_t)j] * x[(size_t)j]; ++n; } out.push_back(10.0 * std::log10(e / std::max(1, n) + 1e-12)); } return out;
}
inline double corr(const std::vector<double>& a, const std::vector<double>& b) {
    double ma = 0, mb = 0; size_t n = std::min(a.size(), b.size()); if (!n) return 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return sab / std::sqrt(saa * sbb + 1e-30);
}

// ---------------------------------------------------------------- generated voices
inline double reso(double f, double F, double bw) { double r = f / F; return 1.0 / std::sqrt((1 - r * r) * (1 - r * r) + (f * bw / (F * F)) * (f * bw / (F * F))); }
struct Voiced { std::vector<float> x; std::vector<float> f0; };   // f0 per sample (0 = no pitch)
// Sung phrase: five notes and vowels, vibrato, glides between notes, s/t noise bursts, breath.
inline Voiced phrase(double sr = SR) {
    static const double F[5][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 }, { 530, 1840, 2480 }, { 570, 840, 2410 } };
    static const double nt[5][3] = { { 0.0, 1.2, 220 }, { 1.3, 2.5, 247 }, { 2.6, 3.4, 196 }, { 3.5, 5.2, 262 }, { 5.3, 7.6, 165 } };
    size_t n = (size_t)(8.0 * sr); Voiced v; v.x.assign(n, 0.0f); v.f0.assign(n, 0.0f);
    std::vector<double> amp(n, 0.0), f0(n, 0.0); std::vector<int> vow(n, 0);
    for (int i = 0; i < 5; ++i) for (size_t s = (size_t)(nt[i][0] * sr); s < (size_t)(nt[i][1] * sr) && s < n; ++s) {
        double t = s / sr, a = nt[i][0], b = nt[i][1];
        double e = std::min(1.0, std::min((t - a) / 0.08, (b - t) / 0.15)); amp[s] = std::max(0.0, e) * (0.5 + 0.5 * std::sin(kPi * (t - a) / (b - a)));
        f0[s] = nt[i][2]; vow[s] = i;
    }
    double last = 220; for (size_t s = 0; s < n; ++s) { if (f0[s] > 0) last = f0[s]; else f0[s] = last; }
    double ph = 0; husk::Rng r(5);
    for (size_t s = 0; s < n; ++s) {
        double t = s / sr; double f = f0[s] * std::pow(2.0, 0.35 / 12.0 * std::sin(kTwoPi * 5.5 * t) * std::min(1.0, std::fmod(t, 1.5) / 0.6));
        ph += kTwoPi * f / sr; const double* Fm = F[vow[s]]; double y = 0;
        if (amp[s] > 0) for (int k = 1; k < 60 && k * f < 0.45 * sr; ++k) y += (1.0 / k) * reso(k * f, Fm[0], 80) * reso(k * f, Fm[1], 100) * reso(k * f, Fm[2], 150) * std::sin(k * ph);
        v.x[s] = (float)(y * amp[s] + 1e-3 * r.bi() * amp[s]); v.f0[s] = amp[s] > 0.2 ? (float)f : 0.0f;
    }
    // consonant bursts: band-limited noise just before each note
    for (int i = 0; i < 5; ++i) {
        size_t a = (size_t)std::max(0.0, nt[i][0] * sr - 0.035 * sr), L = (size_t)(0.07 * sr); husk::BandPass bp; bp.set(sr, 5500, 0.8); husk::Rng rr(17 + i);
        for (size_t k = 0; k < L && a + k < n; ++k) { double w = 0.5 - 0.5 * std::cos(kTwoPi * k / L); v.x[a + k] += (float)(0.9 * w * bp.tick(rr.bi())); }
    }
    float pk = 0; for (float s : v.x) pk = std::max(pk, std::fabs(s)); for (float& s : v.x) s *= 0.5f / pk;
    return v;
}
// Five vowels on one sung pitch (220 Hz with light vibrato), 1.5 s each.
inline std::vector<float> vowels(double sr = SR) {
    static const double F[5][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 }, { 530, 1840, 2480 }, { 570, 840, 2410 } };
    size_t n = (size_t)(7.5 * sr); std::vector<float> x(n); double ph = 0;
    for (size_t s = 0; s < n; ++s) {
        double t = s / sr, f = 220.0 * std::pow(2.0, 0.25 / 12.0 * std::sin(kTwoPi * 5 * t)); ph += kTwoPi * f / sr;
        const double* Fm = F[std::min(4, (int)(t / 1.5))]; double y = 0;
        for (int k = 1; k < 80 && k * f < 0.45 * sr; ++k) y += (1.0 / k) * reso(k * f, Fm[0], 80) * reso(k * f, Fm[1], 100) * reso(k * f, Fm[2], 150) * std::sin(k * ph);
        x[s] = (float)y;
    }
    float pk = 0; for (float s : x) pk = std::max(pk, std::fabs(s)); for (float& s : x) s *= 0.4f / pk;
    return x;
}
} // namespace q

// HUSK — small DSP building blocks (no JUCE). Original code.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace husk {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-12f)); }

// xorshift32 random numbers, cheap and allocation-free.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    Rng() = default;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    inline float bi() { return uni() * 2.0f - 1.0f; }                            // [-1,1)
};

// Radix-2 complex FFT, in-place, sizes up to 1<<16. Tables built in init (not on the audio thread).
struct FFT {
    int n = 0, logn = 0;
    std::vector<float> cs, sn;
    std::vector<int> rev;
    void init(int size) {
        n = size; logn = 0; while ((1 << logn) < n) ++logn;
        cs.resize((size_t)n / 2); sn.resize((size_t)n / 2); rev.resize((size_t)n);
        for (int i = 0; i < n / 2; ++i) { cs[(size_t)i] = (float)std::cos(kTwoPi * i / n); sn[(size_t)i] = (float)-std::sin(kTwoPi * i / n); }
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < logn; ++b) if (i & (1 << b)) r |= 1 << (logn - 1 - b); rev[(size_t)i] = r; }
    }
    void forward(float* re, float* im) const {
        for (int i = 0; i < n; ++i) { int r = rev[(size_t)i]; if (r > i) { std::swap(re[i], re[r]); std::swap(im[i], im[r]); } }
        for (int len = 2; len <= n; len <<= 1) {
            int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j) {
                    float wr = cs[(size_t)(j * step)], wi = sn[(size_t)(j * step)];
                    float xr = re[i + j + half], xi = im[i + j + half];
                    float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
                    re[i + j + half] = re[i + j] - tr; im[i + j + half] = im[i + j] - ti;
                    re[i + j] += tr; im[i + j] += ti;
                }
        }
    }
};

// Sine lookup with linear interpolation; phase in cycles. Error < 3e-7.
struct SineTable {
    static constexpr int N = 4096;
    std::array<float, N + 1> t {};
    SineTable() { for (int i = 0; i <= N; ++i) t[(size_t)i] = (float)std::sin(kTwoPi * i / N); }
    inline float at(double cycles) const {
        double p = cycles - std::floor(cycles);
        double x = p * N; int i = (int)x; float f = (float)(x - i);
        return t[(size_t)i] + f * (t[(size_t)i + 1] - t[(size_t)i]);
    }
};
inline const SineTable& sineTable() { static const SineTable s; return s; }

// RBJ band-pass, constant 0 dB peak gain.
struct BandPass {
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(double sr, double fc, double q) {
        double w = kTwoPi * fc / sr, al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float)(al / a0); b2 = (float)(-al / a0); a1 = (float)(-2.0 * std::cos(w) / a0); a2 = (float)((1.0 - al) / a0);
    }
    inline float tick(float x) { float y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void clear() { z1 = z2 = 0; }
};

// Plain integer delay line.
struct Delay {
    std::vector<float> buf; int w = 0, d = 0;
    void init(int maxDelay) { buf.assign((size_t)std::max(1, maxDelay + 1), 0.0f); w = 0; d = 0; }
    void setDelay(int samples) { d = std::clamp(samples, 0, (int)buf.size() - 1); }
    inline float tick(float x) {
        buf[(size_t)w] = x; int r = w - d; if (r < 0) r += (int)buf.size();
        float y = buf[(size_t)r]; if (++w >= (int)buf.size()) w = 0; return y;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
};

// Small 8-line feedback delay network room ("Space"). Short to medium, never a big hall.
struct Room {
    static constexpr int L = 8;
    std::array<std::vector<float>, L> line; std::array<int, L> len {}, pos {};
    std::array<float, L> g {}, lp {};
    double sr = 44100; float damp = 0.3f; float t60 = 0.5f;
    void init(double fs) {
        sr = fs;
        static const double ms[L] = { 23.1, 29.7, 33.3, 37.9, 41.3, 45.7, 51.1, 57.7 };
        for (int i = 0; i < L; ++i) { len[(size_t)i] = std::max(8, (int)std::lround(ms[i] * 0.001 * sr)); line[(size_t)i].assign((size_t)len[(size_t)i], 0.0f); }
        clear(); setDecay(0.5f);
    }
    void clear() { for (int i = 0; i < L; ++i) { std::fill(line[(size_t)i].begin(), line[(size_t)i].end(), 0.0f); pos[(size_t)i] = 0; lp[(size_t)i] = 0; } }
    void setDecay(float seconds) {
        t60 = seconds;
        for (int i = 0; i < L; ++i) g[(size_t)i] = (float)std::pow(10.0, -3.0 * len[(size_t)i] / (seconds * sr));
    }
    inline void tick(float in, float& outL, float& outR) {
        float o[L]; float sum = 0;
        for (int i = 0; i < L; ++i) { o[i] = line[(size_t)i][(size_t)pos[(size_t)i]]; sum += o[i]; }
        float hh = sum * (2.0f / L);   // Householder feedback matrix
        outL = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
        outR = (o[1] - o[3] + o[5] - o[7]) * 0.5f;
        for (int i = 0; i < L; ++i) {
            float fb = (o[i] - hh) * g[(size_t)i];
            lp[(size_t)i] += (1.0f - damp) * (fb - lp[(size_t)i]);
            float v = lp[(size_t)i] + in * 0.35f;
            line[(size_t)i][(size_t)pos[(size_t)i]] = v;
            if (++pos[(size_t)i] >= len[(size_t)i]) pos[(size_t)i] = 0;
        }
    }
};

// Lookahead true-peak safety limiter. Linked stereo. The requirement for each sample comes from
// 4x polyphase-FIR oversampled peaks (windowed sinc, 32 taps per phase, as in BS.1770 style
// meters); the gain curve is a sliding minimum smoothed by a box filter that provably stays at or
// below every sample's requirement. A final clamp catches anything left.
struct SafetyLimiter {
    static constexpr int kTaps = 32, kHalf = kTaps / 2, kPh = 4;
    int la = 64, win = 60, avg = 28;
    float ceiling = 0.891f;     // -1 dBTP target
    float clampAt = 0.966f;     // -0.3 dBFS hard safety
    float rel = 0.999f;
    std::vector<float> dl, dr, req, avgBuf;
    float coef[kPh][kTaps] {};
    float hl[kTaps] {}, hr[kTaps] {};
    int w = 0, n = 0, aw = 0, hp = 0; double avgSum = 0; float g = 1.0f; float minGain = 1.0f;
    int engaged = 0;
    double fs = 44100;
    void init(double sr) {
        fs = sr;
        la = std::max(48, (int)std::lround(0.0015 * sr));
        win = la - 3; avg = std::max(1, (win - kHalf - 2) / 2);
        n = la + 1;
        dl.assign((size_t)n, 0.0f); dr.assign((size_t)n, 0.0f); req.assign((size_t)n, 1.0f);
        avgBuf.assign((size_t)avg, 1.0f); avgSum = avg; aw = 0; w = 0; g = 1.0f; hp = 0;
        std::fill(std::begin(hl), std::end(hl), 0.0f); std::fill(std::begin(hr), std::end(hr), 0.0f);
        rel = (float)std::exp(-1.0 / (0.08 * sr));
        minGain = 1.0f; engaged = 0;
        for (int ph = 0; ph < kPh; ++ph)
            for (int t = 0; t < kTaps; ++t) {
                double d = (t - (kHalf - 1)) - (double)ph / kPh;   // position relative to the interpolated point
                double x = kPi * d, sinc = std::fabs(d) < 1e-12 ? 1.0 : std::sin(x) / x;
                double wv = 0.5 + 0.5 * std::cos(kPi * d / (kHalf + 0.5));
                coef[ph][t] = (float)(sinc * wv);
            }
        for (int ph = 1; ph < kPh; ++ph) { double sum = 0; for (int t = 0; t < kTaps; ++t) sum += coef[ph][t]; for (int t = 0; t < kTaps; ++t) coef[ph][t] = (float)(coef[ph][t] / sum); }
    }
    void reset() { init(fs); }
    inline float interPeak(const float* h) const {
        float m = 0;
        for (int ph = 1; ph < kPh; ++ph) {
            float s = 0;
            for (int t = 0; t < kTaps; ++t) { int idx = hp - kTaps + t; if (idx < 0) idx += kTaps; s += coef[ph][t] * h[idx]; }
            m = std::max(m, std::fabs(s));
        }
        return m;
    }
    inline void tick(float& L, float& R) {
        hl[hp] = L; hr[hp] = R; if (++hp >= kTaps) hp = 0;
        // inter-sample peaks between the samples kHalf-1 and kHalf back; stored at this slot
        float p = std::max({ std::fabs(L), std::fabs(R), interPeak(hl), interPeak(hr) });
        float rq = p > ceiling ? ceiling / p : 1.0f;
        dl[(size_t)w] = L; dr[(size_t)w] = R; req[(size_t)w] = rq;
        int outIdx = w + 1; if (outIdx >= n) outIdx = 0;          // oldest sample = delay of la
        float mn = 1.0f;
        for (int k = 0; k < win + 3; ++k) { int idx = outIdx + k; if (idx >= n) idx -= n; mn = std::min(mn, req[(size_t)idx]); }
        avgSum += mn - avgBuf[(size_t)aw]; avgBuf[(size_t)aw] = mn; if (++aw >= avg) aw = 0;
        float s = (float)(avgSum / avg);
        float rising = g + (1.0f - g) * (1.0f - rel);
        if (rising > 0.99999f) rising = 1.0f;
        g = std::min(s, rising);
        L = dl[(size_t)outIdx] * g; R = dr[(size_t)outIdx] * g;
        if (g < 0.9999f) ++engaged;
        minGain = std::min(minGain, g);
        L = std::clamp(L, -clampAt, clampAt); R = std::clamp(R, -clampAt, clampAt);
        if (++w >= n) w = 0;
    }
};

} // namespace husk

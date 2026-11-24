// HUSK — voice analysis: YIN-family pitch tracking, harmonic partial picking from a
// zero-padded Hann spectrum, a partial-based spectral (formant) envelope, and residual
// (breath / consonant) band energies. Textbook methods, original code. No ML.
#pragma once
#include "HuskDsp.h"
#include <complex>

namespace husk {

constexpr int kMaxPartials = 64;
constexpr int kBands = 24;

struct Frame {
    bool voiced = false;
    float f0 = 0;            // Hz
    float aperiodicity = 1;  // YIN cumulative-mean-normalised dip (0 = perfectly periodic)
    float rms = 0;
    int nh = 0;              // number of measured harmonics
    float fk[kMaxPartials] {}, ak[kMaxPartials] {};
    float energy = 0;        // sum of ak^2
    float tilt = 0;          // fitted slope of log amplitude vs log frequency
    int ne = 0;              // envelope points (log amplitude vs frequency)
    float ef[kMaxPartials] {}, el[kMaxPartials] {};
    float res[kBands] {};    // residual band gains (noise amplitude per band)

    // Spectral envelope from the measured partials: piecewise-linear in log amplitude, flat outside.
    float env(float f) const {
        if (ne <= 0) return 0.0f;
        if (f <= ef[0]) return std::exp(el[0]);
        if (f >= ef[ne - 1]) return std::exp(el[ne - 1]);
        int lo = 0, hi = ne - 1;
        while (hi - lo > 1) { int m = (lo + hi) / 2; if (ef[m] <= f) lo = m; else hi = m; }
        float t = (f - ef[lo]) / std::max(1e-6f, ef[hi] - ef[lo]);
        return std::exp(el[lo] + t * (el[hi] - el[lo]));
    }
};

class Analyzer {
public:
    double sr = 44100;
    int N = 1024, H = 256, Npad = 4096, D = 1;   // frame, hop, padded FFT size, YIN decimation
    float minF0 = 70.0f, maxF0 = 1000.0f;
    float voicedThresh = 0.20f, gateRms = 0.0008f;
    bool wantPartials = true;   // v2: only the Ghost model needs the full partial list
    std::array<float, kBands> bandFc {}, bandLo {}, bandHi {};
    int nBands = kBands;

    void prepare(double fs) {
        sr = fs;
        N = 1024; while (N * 44100.0 < 1024.0 * sr * 0.75) N *= 2;   // ~23 ms at any rate
        H = N / 4; Npad = 4 * N; D = N / 1024;
        fft.init(Npad);
        win.resize((size_t)N); winSum = 0;
        for (int i = 0; i < N; ++i) { win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * i / N)); winSum += win[(size_t)i]; }
        winSq = 0; for (float w : win) winSq += (double)w * w;
        re.assign((size_t)Npad, 0); im.assign((size_t)Npad, 0); pre.assign((size_t)Npad / 2 + 1, 0); pim.assign((size_t)Npad / 2 + 1, 0); prevValid = false;
        // Hann kernel W(delta) (complex, delta in bins of N), for removing neighbour / mirror leakage
        wtab.resize((size_t)(2 * kTabHalf * kTabRes + 1));
        for (int i = 0; i < (int)wtab.size(); ++i) {
            double d = (double)(i - kTabHalf * kTabRes) / kTabRes;
            auto D = [&](double db) { double w = kTwoPi * db / N, sh = std::sin(0.5 * w); double mag = std::fabs(sh) < 1e-12 ? (double)N : std::sin(0.5 * N * w) / sh; double ph = -0.5 * w * (N - 1); return std::complex<double>(mag * std::cos(ph), mag * std::sin(ph)); };
            wtab[(size_t)i] = 0.5 * D(d) - 0.25 * D(d - 1.0) - 0.25 * D(d + 1.0);
        } pw.assign((size_t)Npad / 2 + 1, 0); excl.assign((size_t)Npad / 2 + 1, 0);
        srD = sr / D;
        tauMin = std::max(2, (int)std::floor(srD / maxF0));
        tauMax = (int)std::ceil(srD / minF0);
        W = 512;
        seg.assign((size_t)(W + tauMax + 2), 0);
        dprime.assign((size_t)tauMax + 2, 0);
        double top = std::min(14000.0, 0.45 * sr), bot = 80.0;
        double r = std::pow(top / bot, 1.0 / kBands);
        for (int b = 0; b < kBands; ++b) {
            bandLo[(size_t)b] = (float)(bot * std::pow(r, b)); bandHi[(size_t)b] = (float)(bot * std::pow(r, b + 1));
            bandFc[(size_t)b] = std::sqrt(bandLo[(size_t)b] * bandHi[(size_t)b]);
        }
    }
    int yinSpan() const { return (W + tauMax) * D; }

    // x: the last `max(N, yinSpan())` input samples, oldest first; `len` is that count.
    void analyse(const float* x, int len, Frame& f) {
        const float* fr = x + (len - N);
        double e = 0; for (int i = 0; i < N; ++i) e += (double)fr[i] * fr[i];
        f.rms = (float)std::sqrt(e / N);
        // ---- pitch (YIN) ----
        float tau = 0, ap = 1;
        if (f.rms > gateRms) yin(x + (len - yinSpan()), tau, ap);
        f.aperiodicity = ap;
        bool voiced = f.rms > gateRms && tau > 0 && ap < voicedThresh;
        // ---- spectrum ----
        std::fill(re.begin(), re.end(), 0.0f); std::fill(im.begin(), im.end(), 0.0f);
        for (int i = 0; i < N; ++i) re[(size_t)i] = fr[i] * win[(size_t)i];
        fft.forward(re.data(), im.data());
        const int nb = Npad / 2;
        for (int k = 0; k <= nb; ++k) pw[(size_t)k] = re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k];
        std::fill(excl.begin(), excl.end(), 0);
        f.nh = 0; f.energy = 0; f.ne = 0;
        const double binHz = sr / Npad;
        if (voiced) {
            double f0 = srD / tau;
            // refine f0 from the first harmonics' interpolated peaks
            double num = 0, den = 0;
            for (int h = 1; h <= 8; ++h) {
                double fc = h * f0; if (fc > 0.45 * sr) break;
                float pf, pa; bool ok = peakNear(fc, 0.3 * f0, binHz, pf, pa);
                if (ok && pa > 0) { num += pa * (pf / h); den += pa; }
            }
            if (den > 0) f0 = num / den;
            f.f0 = (float)f0;
            double fTop = std::min(0.45 * sr, 16000.0);
            int K = wantPartials ? std::min(kMaxPartials, (int)std::floor(fTop / f0)) : 0;
            float amax = 0;
            // pass 1: interpolated peaks
            for (int h = 1; h <= K; ++h) {
                float pf, pa;
                bool ok = peakNear(h * f0, 0.3 * f0, binHz, pf, pa);
                if (!ok) { pf = (float)(h * f0); pa = ampAt(pf, binHz); }
                f.fk[h - 1] = pf; f.ak[h - 1] = pa;
                cAmp[h - 1] = complexAt(pf, pa, binHz);
            }
            // pass 2: remove neighbour and mirror-image leakage (Gauss-Seidel), re-fit each peak
            for (int it = 0; it < 2; ++it)
                for (int h = 1; h <= K; ++h) refine(h - 1, K, binHz, f0, f);
            { double nm = 0, dn = 0; for (int h = 1; h <= std::min(K, 8); ++h) { nm += f.ak[h - 1] * (f.fk[h - 1] / h); dn += f.ak[h - 1]; } if (dn > 0) f.f0 = (float)(nm / dn); }
            // harmonic check: a real voiced partial sits close to h*f0 and stands clear of the
            // spectrum half-way to its neighbours; noise peaks (breath, "s") fail and go to Air.
            for (int j = 0; j < K; ++j) fkTmp[j] = f.fk[j];
            for (int h = 1; h <= K; ++h) {
                float pf = f.fk[h - 1], pa = f.ak[h - 1];
                double dev = std::fabs(pf - h * f0) / f0;
                double wgt = std::clamp(1.0 - (dev - 0.04) / 0.06, 0.0, 1.0);
                if (wgt > 0) {
                    // what is left half-way to the neighbours once every partial is removed
                    auto left = [&](double fc) { int c = std::clamp((int)std::lround(fc / binHz), 1, nb - 1); return std::norm(cleanedAll(c, h - 1, K, binHz)); };
                    double mid = 0.5 * (left((h - 0.5) * f0) + left((h + 0.5) * f0));
                    double pk = 0.25 * pa * pa * winSum * winSum;
                    double ratioDb = 10.0 * std::log10((pk + 1e-30) / (mid + 1e-30));
                    wgt *= std::clamp((ratioDb - 10.0) / 8.0, 0.0, 1.0);
                }
                pa *= (float)wgt;
                f.ak[h - 1] = pa; f.energy += pa * pa; amax = std::max(amax, pa);
                if (wgt > 0.5) {
                    int c = (int)std::lround(pf / binHz), wdt = 9;
                    for (int k = std::max(0, c - wdt); k <= std::min(nb, c + wdt); ++k) excl[(size_t)k] = 1;
                }
            }
            f.nh = K;
            // envelope points: partials within 60 dB of the strongest
            for (int h = 0; h < K; ++h)
                if (f.ak[h] > amax * 3e-3f) { f.ef[f.ne] = f.fk[h]; f.el[f.ne] = std::log(f.ak[h]); ++f.ne; }
            // overall source tilt (log amplitude vs log frequency), so models can keep the mouth
            // shape (formants) without inheriting the voice's own brightness
            f.tilt = 0;
            if (f.ne >= 3) {
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                for (int i = 0; i < f.ne; ++i) { double x = std::log(f.ef[i]), y = f.el[i]; sx += x; sy += y; sxx += x * x; sxy += x * y; }
                double den = f.ne * sxx - sx * sx;
                if (den > 1e-9) f.tilt = (float)std::clamp((f.ne * sxy - sx * sy) / den, -3.0, 0.5);
            }
        }
        f.voiced = voiced;
        // ---- residual band levels (noise density per band; the engine scales for its filters) ----
        int firstValid = -1;
        for (int b = 0; b < kBands; ++b) {
            int k0 = std::max(1, (int)std::ceil(bandLo[(size_t)b] / binHz)), k1 = std::min(nb, (int)std::floor(bandHi[(size_t)b] / binHz));
            double s = 0; int cnt = 0;
            for (int k = k0; k <= k1; ++k) if (!excl[(size_t)k]) { s += pw[(size_t)k]; ++cnt; }
            if (cnt >= 2) { f.res[b] = (float)std::sqrt(s / cnt / winSq); if (firstValid < 0) firstValid = b; }
            else f.res[b] = -1.0f;   // band fully covered by partials: borrow from a neighbour
        }
        for (int b = 0; b < kBands; ++b) if (f.res[b] < 0) {
            float lo = -1, hi = -1;
            for (int j = b - 1; j >= 0; --j) if (f.res[j] >= 0) { lo = f.res[j]; break; }
            for (int j = b + 1; j < kBands; ++j) if (f.res[j] >= 0) { hi = f.res[j]; break; }
            f.res[b] = (lo >= 0 && hi >= 0) ? std::min(lo, hi) : std::max(0.0f, std::max(lo, hi));
        }
        (void)firstValid;
        for (int k = 0; k <= nb; ++k) { pre[(size_t)k] = re[(size_t)k]; pim[(size_t)k] = im[(size_t)k]; }
        prevValid = true;
    }
    void resetHistory() { prevValid = false; }

private:
    static constexpr int kTabHalf = 12, kTabRes = 256;
    std::vector<std::complex<double>> wtab;
    std::vector<float> pre, pim; bool prevValid = false;
    std::complex<double> cAmp[kMaxPartials];
    std::complex<double> kern(double dBins) const {
        if (dBins <= -kTabHalf || dBins >= kTabHalf) return {};
        double x = (dBins + kTabHalf) * kTabRes; int i = (int)x; double t = x - i;
        return wtab[(size_t)i] * (1.0 - t) + wtab[(size_t)i + 1] * t;
    }
    // complex amplitude c = (A/2) e^{j phi} of a partial from the spectrum bin nearest pf
    std::complex<double> complexAt(double pf, double pa, double binHz) const {
        int kb = std::clamp((int)std::lround(pf / binHz), 0, Npad / 2);
        double ph = std::atan2(im[(size_t)kb], re[(size_t)kb]) + kTwoPi * (kb * binHz - pf) / sr * 0.5 * (N - 1);
        return std::polar(0.5 * pa, ph);
    }
    // spectrum at padded bin b with every other partial's (and every mirror image's) leakage removed
    std::complex<double> cleaned(int b, int self, int K, double binHz) const {
        std::complex<double> x(re[(size_t)b], im[(size_t)b]);
        const double binN = sr / N, fb = b * binHz;
        for (int j = std::max(0, self - 4); j < std::min(K, self + 5); ++j)
            if (j != self) x -= cAmp[j] * kern((fb - fkTmp[j]) / binN);
        for (int j = 0; j < K; ++j) { double d = (fb + fkTmp[j]) / binN; if (d >= kTabHalf) break; x -= std::conj(cAmp[j]) * kern(d); }
        return x;
    }
    float fkTmp[kMaxPartials] {};
    std::complex<double> cleanedAll(int b, int centre, int K, double binHz) const {
        std::complex<double> x(re[(size_t)b], im[(size_t)b]);
        const double binN = sr / N, fb = b * binHz;
        for (int j = std::max(0, centre - 4); j < std::min(K, centre + 5); ++j) x -= cAmp[j] * kern((fb - fkTmp[j]) / binN);
        for (int j = 0; j < K; ++j) { double d = (fb + fkTmp[j]) / binN; if (d >= kTabHalf) break; x -= std::conj(cAmp[j]) * kern(d); }
        return x;
    }
    void refine(int i, int K, double binHz, double f0, Frame& f) {
        for (int j = 0; j < K; ++j) fkTmp[j] = f.fk[j];
        const int nb = Npad / 2;
        int kb = std::clamp((int)std::lround(f.fk[i] / binHz), 2, nb - 2);
        double p = 0, A = 0, B = 0, C = 0; std::complex<double> xb;
        for (int pass = 0; pass < 2; ++pass) {
            auto xa = cleaned(kb - 1, i, K, binHz); xb = cleaned(kb, i, K, binHz); auto xc = cleaned(kb + 1, i, K, binHz);
            A = std::log(std::abs(xa) + 1e-30); B = std::log(std::abs(xb) + 1e-30); C = std::log(std::abs(xc) + 1e-30);
            double den = A - 2 * B + C; p = den < 0 ? 0.5 * (A - C) / den : 0.0;
            if (std::fabs(p) > 0.5 && pass == 0) { kb = std::clamp(kb + (p > 0 ? 1 : -1), 2, nb - 2); continue; }
            break;
        }
        p = std::clamp(p, -0.5, 0.5);
        double pf = (kb + p) * binHz;
        if (std::fabs(pf - (i + 1) * f0) > 0.35 * f0) pf = (i + 1) * f0;
        double pa = 2.0 * std::exp(B - 0.25 * (A - C) * p) / winSum;
        double ph = std::arg(xb) + kTwoPi * (kb * binHz - pf) / sr * 0.5 * (N - 1);
        f.fk[i] = (float)pf; f.ak[i] = (float)pa; cAmp[i] = std::polar(0.5 * pa, ph);
    }
    FFT fft;
    std::vector<float> win, re, im, pw, seg, dprime;
    std::vector<uint8_t> excl;
    double winSum = 0, winSq = 0, srD = 44100;
    int tauMin = 44, tauMax = 630, W = 512;

    void yin(const float* x, float& tauOut, float& apOut) {
        const int L = W + tauMax;
        for (int i = 0; i < L; ++i) {
            float s = 0; for (int j = 0; j < D; ++j) s += x[i * D + j];
            seg[(size_t)i] = s / D;
        }
        // difference function d(tau), cumulative-mean normalised
        double run = 0; dprime[0] = 1;
        for (int t = 1; t <= tauMax; ++t) {
            double d = 0; const float* a = seg.data(); const float* b = seg.data() + t;
            for (int j = 0; j < W; ++j) { float v = a[j] - b[j]; d += (double)v * v; }
            run += d;
            dprime[(size_t)t] = (float)(run > 0 ? d * t / run : 1.0);
        }
        int best = -1;
        for (int t = tauMin; t < tauMax; ++t) {
            if (dprime[(size_t)t] < 0.15f) { while (t + 1 < tauMax && dprime[(size_t)t + 1] < dprime[(size_t)t]) ++t; best = t; break; }
        }
        if (best < 0) {  // no clear dip: take the shortest period whose dip is close to the deepest one
            float m = 1e9f; int g = -1; for (int t = tauMin; t < tauMax; ++t) if (dprime[(size_t)t] < m) { m = dprime[(size_t)t]; g = t; }
            best = g;
            const float lim = std::min(m * 1.35f + 0.05f, m + 0.12f);
            for (int t = tauMin + 1; t < g; ++t)
                if (dprime[(size_t)t] < lim && dprime[(size_t)t] <= dprime[(size_t)t - 1] && dprime[(size_t)t] <= dprime[(size_t)t + 1]) { best = t; break; }
        }
        if (best <= tauMin || best >= tauMax - 1) { tauOut = 0; apOut = 1; return; }
        float a = dprime[(size_t)best - 1], b = dprime[(size_t)best], c = dprime[(size_t)best + 1];
        float den = a - 2 * b + c; float p = den > 1e-12f ? 0.5f * (a - c) / den : 0.0f;
        tauOut = best + std::clamp(p, -0.5f, 0.5f); apOut = b;
    }
    // Interpolated spectral peak (log-parabolic) within +-halfWidth Hz of fc.
    bool peakNear(double fc, double halfWidth, double binHz, float& pf, float& pa) {
        const int nb = Npad / 2;
        int k0 = std::max(1, (int)std::floor((fc - halfWidth) / binHz)), k1 = std::min(nb - 1, (int)std::ceil((fc + halfWidth) / binHz));
        if (k1 <= k0) return false;
        int kb = k0; float m = -1;
        for (int k = k0; k <= k1; ++k) if (pw[(size_t)k] > m) { m = pw[(size_t)k]; kb = k; }
        if (kb <= k0 || kb >= k1) return false;  // not an interior maximum
        double a = 0.5 * std::log(pw[(size_t)kb - 1] + 1e-30), b = 0.5 * std::log(pw[(size_t)kb] + 1e-30), c = 0.5 * std::log(pw[(size_t)kb + 1] + 1e-30);
        double den = a - 2 * b + c; double p = den < 0 ? 0.5 * (a - c) / den : 0.0;
        p = std::clamp(p, -0.5, 0.5);
        pf = (float)((kb + p) * binHz);
        pa = (float)(2.0 * std::exp(b - 0.25 * (a - c) * p) / winSum);
        return true;
    }
    float ampAt(double f, double binHz) const {
        int k = std::clamp((int)std::lround(f / binHz), 0, Npad / 2);
        return (float)(2.0 * std::sqrt(pw[(size_t)k]) / winSum);
    }
};

} // namespace husk

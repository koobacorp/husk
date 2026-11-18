// HUSK — voice-to-instrument resynthesis engine (mixer insert). No JUCE, no allocation on the
// audio path after prepare(). Original code.
//
// Signal path:  input -> analysis (every hop) -> recipe (model, partials, pitch, formant, chord,
// freeze/steps, pad, phase) -> oscillator bank + residual noise bands -> crunch -> shimmer -> space
// -> mix with latency-matched dry -> out gain -> lookahead true-peak safety limiter.
// Reported latency = N/2 + H (analysis) + limiter lookahead. Mix 0 nulls against the delayed dry.
#pragma once
#include "HuskAnalysis.h"
#include <atomic>

namespace husk {

enum Model { Sine = 0, Saw, Square, Glass, Flute, Choir, String, kNumModels };
inline const char* modelName(int m) {
    static const char* n[] = { "Sine", "Saw", "Square", "Glass", "Flute", "Choir", "String" };
    return n[std::clamp(m, 0, kNumModels - 1)];
}
// Note values for Steps and synced Phase Rate, in beats (quarter notes).
constexpr int kNumDivs = 6;
inline double divBeats(int i) { static const double b[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[std::clamp(i, 0, kNumDivs - 1)]; }
inline const char* divName(int i) { static const char* n[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return n[std::clamp(i, 0, kNumDivs - 1)]; }

constexpr int kMaxNotes = 6;     // Chord mode cap
constexpr int kMaxCopies = 3;    // Choir copies
constexpr int kGroups = kMaxNotes * kMaxCopies;
constexpr int kShimmer = 16;

struct Params {
    int model = Sine;
    float partials = 64;      // 1..64
    float air = 0.5f;         // 0..1 (0.5 = residual at its measured level)
    float pitch = 0;          // semitones -12..12
    int octave = 0;           // -1, 0, +1
    float formant = 0;        // semitones -12..12 (mouth shift)
    float phase = 0;          // 0..1
    float phaseRate = 1.0f;   // Hz (free)
    bool phaseSync = false; int phaseDiv = 3;
    int steps = 0;            // 0 = off, 1..6 = divBeats(steps-1)
    float water = 0;          // pad X: -1..1 (left = Glide, right = Water)
    float grain = 0;          // pad Y: -1..1 (down = Soft, up = Grain)
    bool freeze = false, chord = false;
    float shimmer = 0, space = 0, crunch = 0;
    float mix = 1.0f, outDb = 0.0f;
    bool ab = false, bypass = false;
};

struct HostTime { bool valid = false, playing = false; double bpm = 120, ppq = 0; };

class Engine {
public:
    // ------------------------------------------------------------------ setup
    void prepare(double fs, int /*maxBlock*/) {
        sr = fs;
        an.prepare(sr);
        N = an.N; H = an.H;
        span = std::max(N, an.yinSpan());
        ringSize = 1; while (ringSize < span * 2 + 8) ringSize <<= 1;
        ring.assign((size_t)ringSize, 0.0f); lin.assign((size_t)span, 0.0f);
        lim.init(sr);
        dryDelay = N / 2 + H;
        latency = dryDelay + lim.la;
        for (auto* d : { &dryL, &dryR }) { d->init(dryDelay); d->setDelay(dryDelay); }
        for (auto* d : { &bypL, &bypR }) { d->init(latency); d->setDelay(latency); }
        room.init(sr);
        for (int b = 0; b < an.nBands; ++b) {
            // 4th-order band (two RBJ band-passes); scale so unit noise reproduces the measured density
            double bw = an.bandHi[(size_t)b] - an.bandLo[(size_t)b], q = an.bandFc[(size_t)b] / bw * 0.65;
            bp[(size_t)b].set(sr, an.bandFc[(size_t)b], q); bp2[(size_t)b] = bp[(size_t)b];
            double integ = 0; const int M = 8192;
            for (int i = 1; i < M; ++i) {
                double fhz = 0.5 * sr * i / M, w = kTwoPi * fhz / sr; auto& f = bp[(size_t)b];
                std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
                std::complex<double> h = ((double)f.b0 + (double)f.b2 * z2) / (1.0 + (double)f.a1 * z1 + (double)f.a2 * z2);
                integ += std::norm(h) * std::norm(h) * (0.5 * sr / M);
            }
            // analysis gives sqrt(mean|X|^2 / sum w^2); unit-variance noise has one-sided PSD 2/sr
            bandNorm[(size_t)b] = (float)std::sqrt(bw / integ);
        }
        reset();
    }
    void reset() {
        std::fill(ring.begin(), ring.end(), 0.0f); rw = 0; count = 0; hopPos = 0; an.resetHistory(); fresh = true;
        dryL.clear(); dryR.clear(); bypL.clear(); bypR.clear(); room.clear(); lim.init(sr);
        for (auto& b : bp) b.clear(); for (auto& b : bp2) b.clear(); for (int b = 0; b < kBands; ++b) bandNoise[(size_t)b] = Rng(1000u + 7919u * (uint32_t)b);
        live = Frame(); held = Frame(); frozen = Frame(); stepF = Frame(); lastVoiced = Frame(); lastVoicedAge = 1 << 30;
        for (auto& o : osc) o = Osc();
        for (auto& s : shim) s = Osc();
        for (auto& g : resG) g = 0; for (auto& g : resD) g = 0;
        for (int i = 0; i < kMaxPartials; ++i) { thetaCur[(size_t)i] = 0; thetaTgt[(size_t)i] = 0; dTheta[(size_t)i] = 0; }
        for (auto& w : wander) w = 0; for (auto& w : wanderT) w = 0;
        nNotes = 0; activeGroups = 1; groupsTarget = 1;
        wasFrozen = false; freezeDone = false; lastGrid = -1; lastPhaseGrid = -1; freeBeat = 0; phaseClock = 0;
        mixS = p.mix; outS = dbToGain(p.outDb); crS = p.crunch; shS = p.shimmer; spS = p.space; byS = (p.bypass || p.ab) ? 1.f : 0.f;
        lastF0 = 220; rampLeft = 0; rampLen = H; roomActive = false;
        recipeUpdates = 0; scrambles = 0; prevSrc = nullptr; ignoredNotes = 0;
        tpeak = 0; minLimGain = 1;
    }
    void snapSmoothers() {
        mixS = std::clamp(p.mix, 0.0f, 1.0f); outS = dbToGain(std::clamp(p.outDb, -24.0f, 12.0f)); crS = std::clamp(p.crunch, 0.0f, 1.0f);
        shS = std::clamp(p.shimmer, 0.0f, 1.0f); spS = std::clamp(p.space, 0.0f, 1.0f); byS = (p.bypass || p.ab) ? 1.f : 0.f;
    }
    void setParams(const Params& np) { p = np; p.partials = std::clamp(p.partials, 1.0f, (float)kMaxPartials); }
    void setTime(const HostTime& t) { host = t; }
    int latencySamples() const { return latency; }
    int hopSize() const { return H; }
    int frameSize() const { return N; }

    // ------------------------------------------------------------------ MIDI (Chord mode)
    void noteOn(int note) {
        for (int i = 0; i < nNotes; ++i) if (notes[(size_t)i] == note) return;
        if (nNotes >= kMaxNotes) { ++ignoredNotes; return; }   // seventh note is ignored
        notes[(size_t)nNotes++] = note; ++midiEvents;
    }
    void noteOff(int note) {
        for (int i = 0; i < nNotes; ++i) if (notes[(size_t)i] == note) { for (int j = i; j + 1 < nNotes; ++j) notes[(size_t)j] = notes[(size_t)j + 1]; --nNotes; ++midiEvents; return; }
    }
    void allNotesOff() { nNotes = 0; }
    int heldNotes() const { return nNotes; }

    // ------------------------------------------------------------------ audio
    // In-place stereo processing. L and R may hold the same mono signal.
    void process(float* L, float* R, int n) {
        if (fresh) { snapSmoothers(); fresh = false; }
        int pos = 0;
        blockStartSample = 0;
        while (pos < n) {
            int chunk = std::min(n - pos, H - hopPos);
            renderChunk(L + pos, R + pos, chunk);
            pos += chunk; hopPos += chunk;
            if (hopPos >= H) { hopPos = 0; blockStartSample = pos; onHop(pos); }
        }
        blockSamples = n;
    }

    // ------------------------------------------------------------------ telemetry (read by UI / tests)
    Frame live;                       // latest analysis frame
    float displayAmp[kMaxPartials] {};// kept partial levels (bank 0, copy 0), normalised 0..1
    float displayFreq[kMaxPartials] {};
    int displayCount = 0;
    long long recipeUpdates = 0, scrambles = 0, midiEvents = 0, ignoredNotes = 0;
    float tpeak = 0, minLimGain = 1;
    int activeNoteCount() const { return (p.chord && nNotes > 0) ? nNotes : 0; }
    // Last target frequencies/amps per group & partial (tests)
    float tgtFreq(int group, int k) const { return osc[(size_t)(group * kMaxPartials + k)].fT; }
    float tgtAmp(int group, int k) const { return osc[(size_t)(group * kMaxPartials + k)].aT; }
    const SafetyLimiter& limiter() const { return lim; }

private:
    struct Osc { double ph = 0, inc = 0, dinc = 0; float a = 0, da = 0, fT = 0, aT = 0, fCur = 0, aCur = 0; };

    Params p; HostTime host;
    double sr = 44100;
    Analyzer an;
    int N = 1024, H = 256, span = 1024, ringSize = 4096, rw = 0, hopPos = 0, dryDelay = 768, latency = 834;
    long long count = 0;
    std::vector<float> ring, lin;
    Delay dryL, dryR, bypL, bypR;
    SafetyLimiter lim;
    Room room; bool roomActive = false;
    std::array<BandPass, kBands> bp, bp2;
    std::array<float, kBands> bandNorm {};
    std::array<Rng, kBands> bandNoise;
    std::array<float, kBands> resG {}, resD {};
    Rng rng { 12345 }, noise { 777 }, jit { 4242 };

    Frame held, frozen, stepF, lastVoiced; int lastVoicedAge = 1 << 30;
    const Frame* prevSrc = nullptr;
    bool wasFrozen = false, freezeDone = false;
    long long lastGrid = -1, lastPhaseGrid = -1; double freeBeat = 0, phaseClock = 0;
    int blockStartSample = 0, blockSamples = 0;

    std::array<Osc, kGroups * kMaxPartials> osc;
    std::array<Osc, kShimmer> shim;
    std::array<float, kGroups> panL {}, panR {};
    int activeGroups = 1, groupsTarget = 1;
    std::array<int, kMaxNotes> notes {}; int nNotes = 0;
    std::array<float, kMaxPartials> thetaCur {}, thetaTgt {}, dTheta {};
    std::array<float, kGroups * kMaxPartials> wander {}, wanderT {};
    float lastF0 = 220;
    int rampLeft = 0, rampLen = 256;
    bool fresh = true;
    float mixS = 1, outS = 1, crS = 0, shS = 0, spS = 0, byS = 0;
    float bufW[2][4096] {};

    static float modelSource(int m, int k) {   // k = 1-based harmonic number
        float kk = (float)k;
        switch (m) {
            case Saw: return 1.0f / kk;
            case Square: return (k & 1) ? 1.0f / kk : 0.02f / kk;
            case Glass: return std::pow(kk, -1.1f);
            case Flute: return k > 12 ? 0.0f : ((k & 1) ? 1.0f / (kk * kk) : 0.3f / (kk * kk));
            case String: return std::pow(kk, -0.75f);
            default: return 1.0f;
        }
    }
    static float stretchB(int m) { return m == Glass ? 0.0018f : (m == String ? 0.00015f : 0.0f); }
    static float residualMul(int m) {
        switch (m) { case Saw: case Square: return 0.8f; case Glass: return 0.6f; case Flute: return 1.8f; case String: return 1.3f; default: return 1.0f; }
    }

    // --------------------------------------------------------------- per-hop control
    void onHop(int offsetInBlock) {
        // analysis on the latest samples
        for (int i = 0; i < span; ++i) { int idx = (rw - span + i) & (ringSize - 1); lin[(size_t)i] = ring[(size_t)idx]; }
        an.analyse(lin.data(), span, live);
        if (live.voiced) { lastVoiced = live; lastVoicedAge = 0; } else lastVoicedAge += H;

        // musical clock for Steps and synced Phase Rate (position where this recipe is heard)
        double bpm = (host.valid && host.bpm > 1) ? host.bpm : 120.0;
        double beat;
        if (host.valid && host.playing) beat = host.ppq + (offsetInBlock + H) / sr * bpm / 60.0;
        else { freeBeat += H / sr * bpm / 60.0; beat = freeBeat; }

        // choose the source recipe frame
        const Frame* src = &live;
        if (p.freeze) {
            if (!wasFrozen) { frozen = (!live.voiced && lastVoicedAge < (int)(0.3 * sr)) ? lastVoiced : live; }
            src = &frozen; wasFrozen = true;
        } else {
            wasFrozen = false;
            if (p.steps > 0) {
                long long gi = (long long)std::floor(beat / divBeats(p.steps - 1) + 1e-9);
                if (gi != lastGrid) { stepF = live; lastGrid = gi; ++recipeUpdates; }
                src = &stepF;
            } else { lastGrid = -1; ++recipeUpdates; }
        }
        float gr = std::max(0.0f, p.grain), soft = std::max(0.0f, -p.grain);
        float wat = std::max(0.0f, p.water), glide = std::max(0.0f, -p.water);
        bool stutter = gr > 0 && rng.uni() < gr * 0.45f;
        if (stutter) src = &held; else held = *src;

        // Phase distortion: re-scramble targets at Phase Rate
        bool scr = false;
        if (p.phaseSync) {
            long long gi = (long long)std::floor(beat / divBeats(p.phaseDiv) + 1e-9);
            if (gi != lastPhaseGrid) { lastPhaseGrid = gi; scr = true; }
        } else {
            phaseClock += H / sr * std::clamp(p.phaseRate, 0.01f, 40.0f);
            if (phaseClock >= 1.0) { phaseClock -= std::floor(phaseClock); scr = true; }
            lastPhaseGrid = -1;
        }
        if (scr) { ++scrambles; for (int k = 0; k < kMaxPartials; ++k) thetaTgt[(size_t)k] = 0.5f * p.phase * rng.bi(); }
        float gam = 0.05f + 0.9f * p.phase * p.phase;
        for (int k = 0; k < kMaxPartials; ++k) {
            float tgt = p.phase > 0 ? thetaTgt[(size_t)k] : 0.0f;
            float nxt = p.phase > 0 ? thetaCur[(size_t)k] + gam * (tgt - thetaCur[(size_t)k]) : 0.0f;
            dTheta[(size_t)k] = (nxt - thetaCur[(size_t)k]) / H;
        }

        computeRecipe(*src, gr, soft, wat, glide);
        rampLen = std::max(1, (int)std::lround(H * (1.0f - 0.92f * gr)));
        rampLeft = rampLen;
        setupRamps();
    }

    void computeRecipe(const Frame& F, float gr, float soft, float wat, float glide) {
        const int m = p.model;
        const float ratio = std::pow(2.0f, (p.pitch + 12.0f * (float)std::clamp(p.octave, -1, 1)) / 12.0f);
        const float fr = std::pow(2.0f, p.formant / 12.0f);
        const bool shifted = std::fabs(ratio - 1.0f) > 1e-6f || std::fabs(fr - 1.0f) > 1e-6f;
        const bool chordOn = p.chord && nNotes > 0;
        const int banks = chordOn ? nNotes : 1;
        const int copies = (m == Choir) ? 3 : 1;
        const float B = stretchB(m);
        const float nyqLo = 0.40f * (float)sr, nyqHi = 0.45f * (float)sr;
        const float K = p.partials;
        if (F.voiced) lastF0 = F.f0;
        static const float detC[3] = { 0.0f, -11.0f, 9.0f };
        float alphaUp = 1.0f - 0.9f * soft - 0.85f * wat, alphaDn = alphaUp;
        if (m == Glass) alphaDn = std::min(alphaDn, 0.06f);
        alphaUp = std::max(alphaUp, 0.04f); alphaDn = std::max(alphaDn, 0.02f);
        const float freqBeta = 1.0f - 0.95f * glide;
        groupsTarget = banks * copies;

        float amps[kMaxPartials], freqs[kMaxPartials];
        for (int b = 0; b < kMaxNotes; ++b) {
            for (int c = 0; c < kMaxCopies; ++c) {
                int g = b * kMaxCopies + c;
                bool on = b < banks && c < copies;
                if (!on) { for (int k = 0; k < kMaxPartials; ++k) { auto& o = osc[(size_t)(g * kMaxPartials + k)]; o.aT = 0; } continue; }
                float F0out = chordOn ? 440.0f * std::pow(2.0f, (notes[(size_t)b] - 69) / 12.0f) * ratio : lastF0 * ratio;
                float Etarget = F.voiced ? F.energy / (float)banks : 0.0f;
                // raw recipe
                int nk = 0; double esum = 0;
                for (int k = 1; k <= kMaxPartials; ++k) {
                    float f, a;
                    if (m == Sine || m == Choir) {
                        if (!chordOn) { if (k > F.nh) break; f = F.fk[k - 1] * ratio; }
                        else f = F0out * (float)k;
                        float src = (k <= F.nh) ? F.ak[k - 1] : 0.0f;
                        float corr = 1.0f;
                        if ((shifted || chordOn) && k <= F.nh && F.ne > 0) {
                            float e0 = F.env(F.fk[k - 1]);
                            corr = e0 > 0 ? std::clamp(F.env(f / fr) / e0, 0.03f, 30.0f) : 1.0f;
                        }
                        a = src * corr;
                    } else {
                        f = F0out * (float)k * std::sqrt(1.0f + B * k * k) / std::sqrt(1.0f + B);
                        float mouth = 1.0f;
                        if (F.ne > 0) {   // formant shape with the voice's own tilt removed
                            float r0 = F.env(F0out / fr) * std::pow(F0out / fr, -F.tilt);
                            float r1 = F.env(f / fr) * std::pow(f / fr, -F.tilt);
                            mouth = r0 > 0 ? std::pow(std::clamp(r1 / r0, 1e-4f, 1e4f), 0.8f) : 1.0f;
                        }
                        a = modelSource(m, k) * mouth;
                    }
                    if (f >= nyqHi) break;
                    if (f > nyqLo) a *= (nyqHi - f) / (nyqHi - nyqLo);
                    freqs[k - 1] = f; amps[k - 1] = a; esum += (double)a * a; nk = k;
                }
                // loudness normalised to the voice
                float sc = (esum > 0 && Etarget > 0) ? (float)std::sqrt(Etarget / esum) : 0.0f;
                // Partials knob: keep the lowest K tones (fractional K fades the last one)
                double eAll = 0, eKept = 0;
                for (int k = 1; k <= nk; ++k) {
                    float a = amps[k - 1] * sc; eAll += (double)a * a;
                    float keep = std::clamp(K - (float)(k - 1), 0.0f, 1.0f);
                    amps[k - 1] = a * keep; eKept += (double)amps[k - 1] * amps[k - 1];
                }
                float comp = eKept > 1e-20 ? std::min(4.0f, (float)std::sqrt(eAll / eKept)) : 1.0f;
                float cg = (copies > 1) ? 1.0f / std::sqrt((float)copies) : 1.0f;
                float det = std::pow(2.0f, detC[c] / 1200.0f);
                for (int k = 0; k < kMaxPartials; ++k) {
                    auto& o = osc[(size_t)(g * kMaxPartials + k)];
                    float a = (k < nk) ? amps[k] * comp * cg : 0.0f;
                    float f = (k < nk) ? freqs[k] * det : (o.fT > 0 ? o.fT : F0out * (k + 1));
                    // Water: slow per-partial pitch wander
                    if (wat > 0) {
                        auto& wv = wander[(size_t)(g * kMaxPartials + k)]; auto& wt = wanderT[(size_t)(g * kMaxPartials + k)];
                        if (rng.uni() < 0.06f + 0.1f * wat) wt = rng.bi();
                        wv += 0.15f * (wt - wv);
                        f *= std::pow(2.0f, wat * wat * 45.0f * wv / 1200.0f);
                    }
                    // Grain: random partial drop-outs and coarse level steps
                    if (gr > 0) {
                        if (rng.uni() < gr * 0.35f) a = 0;
                        else if (a > 0) { float q = 1.0f + 5.0f * gr; float db = gainToDb(a); a = dbToGain(std::round(db / q) * q); }
                    }
                    if (f >= nyqHi) { a = 0; f = nyqHi * 0.99f; }
                    // smoothing (Soft, Water smear, Glass ring) and Glide
                    float al = a > o.aT ? alphaUp : alphaDn;
                    float aN = o.aT + al * (a - o.aT);
                    if (aN < 1e-9f) aN = 0;
                    float fN = (o.aT < 1e-6f || o.fT <= 0) ? f : o.fT * std::pow(f / o.fT, freqBeta);
                    o.aT = aN; o.fT = fN;
                }
                float pl = 0.70710678f, prr = 0.70710678f;
                if (copies > 1 && c > 0) { pl = c == 1 ? 0.92f : 0.39f; prr = c == 1 ? 0.39f : 0.92f; }
                if (banks > 1) { float sp = ((float)b / (banks - 1) - 0.5f) * 0.5f; float th = (0.25f + sp * 0.5f) * (float)kPi; pl = pl * 1.4142f * std::cos(th); prr = prr * 1.4142f * std::sin(th); }
                panL[(size_t)g] = pl * 1.41421356f; panR[(size_t)g] = prr * 1.41421356f;
                if (g == 0) {
                    float mx = 1e-9f; for (int k = 0; k < kMaxPartials; ++k) mx = std::max(mx, osc[(size_t)k].aT);
                    displayCount = 0;
                    for (int k = 0; k < kMaxPartials; ++k) { displayAmp[k] = osc[(size_t)k].aT / mx; displayFreq[k] = osc[(size_t)k].fT; if (osc[(size_t)k].aT > 1e-6f) ++displayCount; }
                }
            }
        }
        // residual (Air): band gains, formant-shifted, model-coloured
        float airG = 2.0f * std::clamp(p.air, 0.0f, 1.0f) * residualMul(m);
        for (int b = 0; b < an.nBands; ++b) {
            float g = F.res[b];
            if (std::fabs(fr - 1.0f) > 1e-6f) {   // read the band envelope at fc/fr
                float fq = an.bandFc[(size_t)b] / fr; float lr = std::log(fq / an.bandFc[0]) / std::log(an.bandFc[1] / an.bandFc[0]);
                int i0 = std::clamp((int)std::floor(lr), 0, an.nBands - 1), i1 = std::min(i0 + 1, an.nBands - 1); float t = std::clamp(lr - (float)i0, 0.0f, 1.0f);
                g = std::exp((1 - t) * std::log(F.res[i0] + 1e-9f) + t * std::log(F.res[i1] + 1e-9f));
            }
            if (m == String && an.bandFc[(size_t)b] > 1000.0f) g *= std::pow(an.bandFc[(size_t)b] / 1000.0f, 0.25f);
            if (gr > 0 && rng.uni() < gr * 0.3f) g = 0;
            resD[(size_t)b] = (g * bandNorm[(size_t)b] * airG - resG[(size_t)b]) / H;
        }
        // shimmer follows bank 0's lowest partials, an octave up, with a slow release (tail)
        float att = 0.15f, relc = 1.0f - std::exp(-(float)H / (2.0f * (float)sr));
        for (int k = 0; k < kShimmer; ++k) {
            auto& s = shim[(size_t)k]; const auto& o = osc[(size_t)k];
            float tgt = o.aT; float c = tgt > s.aT ? att : relc;
            s.aT += c * (tgt - s.aT); if (s.aT < 1e-9f) s.aT = 0;
            if (o.aT > 1e-6f) s.fT = 2.0f * o.fT;
            if (s.fT >= nyqHi) s.aT = 0;
        }
        activeGroups = kGroups;
    }

    void setupRamps() {
        const float invR = 1.0f / (float)rampLen;
        for (auto& o : osc) {
            o.da = (o.aT - o.a) * invR;
            double incT = o.fT / sr;
            if (o.a < 1e-7f && o.aT > 0) o.inc = incT;    // start silent oscillators at pitch (no sweep)
            o.dinc = (incT - o.inc) / rampLen;
        }
        for (auto& s : shim) { s.da = (s.aT - s.a) * invR; double incT = s.fT / sr; if (s.a < 1e-7f) s.inc = incT; s.dinc = (incT - s.inc) / rampLen; }
    }

    // --------------------------------------------------------------- per-sample rendering
    void renderChunk(float* L, float* R, int n) {
        // input into the analysis ring and dry delays
        float* wl = bufW[0]; float* wr = bufW[1];
        for (int i = 0; i < n; ++i) {
            ring[(size_t)rw] = 0.5f * (L[i] + R[i]); rw = (rw + 1) & (ringSize - 1);
            wl[i] = 0; wr[i] = 0;
        }
        const auto& st = sineTable();
        const float jitAmt = 0.25f * p.phase * p.phase * p.phase;
        // oscillator bank
        for (int g = 0; g < kGroups; ++g) {
            float gl = panL[(size_t)g], grr = panR[(size_t)g];
            for (int k = 0; k < kMaxPartials; ++k) {
                auto& o = osc[(size_t)(g * kMaxPartials + k)];
                if (o.a <= 0 && o.aT <= 0 && o.da == 0) continue;
                double ph = o.ph, inc = o.inc; float a = o.a; float th = thetaCur[(size_t)k]; const float dth = dTheta[(size_t)k];
                int rl = rampLeft;
                for (int i = 0; i < n; ++i) {
                    if (rl > 0) { a += o.da; inc += o.dinc; --rl; if (rl == 0) { a = o.aT; inc = o.fT / sr; } }
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    th += dth;
                    double pp = ph + th;
                    if (jitAmt > 0) pp += jitAmt * jit.bi();
                    float v = a * st.at(pp);
                    wl[i] += v * gl; wr[i] += v * grr;
                }
                o.ph = ph; o.inc = inc; o.a = a < 1e-9f ? 0 : a;
            }
        }
        for (int k = 0; k < kMaxPartials; ++k) thetaCur[(size_t)k] += dTheta[(size_t)k] * n;
        const int rl0 = rampLeft;
        // residual noise bands (Air)
        for (int i = 0; i < n; ++i) {
            float r = 0;
            const bool ramp = (rampLeft - i) > 0;
            for (int b = 0; b < an.nBands; ++b) {
                if (ramp) resG[(size_t)b] += resD[(size_t)b];
                float nz = bandNoise[(size_t)b].bi() * 1.7320508f;
                r += resG[(size_t)b] * bp2[(size_t)b].tick(bp[(size_t)b].tick(nz));
            }
            wl[i] += r; wr[i] += r;
        }
        if (rampLeft > 0) { int used = std::min(rampLeft, n); rampLeft -= used; if (rampLeft == 0) for (int b = 0; b < an.nBands; ++b) resD[(size_t)b] = 0; }
        // finishing chain
        const float mixT = (p.ab || p.bypass) ? mixS : std::clamp(p.mix, 0.0f, 1.0f);
        const float outT = dbToGain(std::clamp(p.outDb, -24.0f, 12.0f));
        const float byT = (p.bypass || p.ab) ? 1.0f : 0.0f;
        const float sm = 1.0f - std::exp(-1.0f / (0.02f * (float)sr));
        const float spaceT = std::clamp(p.space, 0.0f, 1.0f);
        if (spaceT > 1e-4f || spS > 1e-4f) { if (!roomActive) { room.clear(); roomActive = true; } room.setDecay(0.3f + 1.7f * std::max(spaceT, spS)); }
        else if (roomActive) { room.clear(); roomActive = false; }
        for (int i = 0; i < n; ++i) {
            crS += sm * (std::clamp(p.crunch, 0.0f, 1.0f) - crS); if (std::fabs(crS - p.crunch) < 1e-6f) crS = std::clamp(p.crunch, 0.0f, 1.0f);
            shS += sm * (std::clamp(p.shimmer, 0.0f, 1.0f) - shS); if (std::fabs(shS - p.shimmer) < 1e-6f) shS = std::clamp(p.shimmer, 0.0f, 1.0f);
            spS += sm * (spaceT - spS); if (std::fabs(spS - spaceT) < 1e-6f) spS = spaceT;
            mixS += sm * (mixT - mixS); if (std::fabs(mixS - mixT) < 1e-6f) mixS = mixT;
            outS += sm * (outT - outS); if (std::fabs(outS - outT) < 1e-7f) outS = outT;
            byS += sm * (byT - byS); if (std::fabs(byS - byT) < 1e-6f) byS = byT;
            float a = wl[i], b = wr[i];
            if (crS > 0) {   // Crunch: tanh saturator with unity small-signal gain (never boosts)
                float d = 1.0f + 19.0f * crS * crS;
                a = std::tanh(d * a) / d * (1.0f + 0.35f * crS); b = std::tanh(d * b) / d * (1.0f + 0.35f * crS);
                // blend keeps very low settings gentle
                float bl = std::min(1.0f, crS * 4.0f);
                a = wl[i] + bl * (a - wl[i]); b = wr[i] + bl * (b - wr[i]);
            }
            if (shS > 0) {
                float s = 0; const auto& stt = sineTable();
                for (int k = 0; k < kShimmer; ++k) {
                    auto& o = shim[(size_t)k]; if (o.a <= 0 && o.aT <= 0) continue;
                    if (rl0 - i > 0) { o.a += o.da; o.inc += o.dinc; if (rl0 - i == 1) { o.a = o.aT; o.inc = o.fT / sr; } }
                    o.ph += o.inc; if (o.ph >= 1.0) o.ph -= 1.0;
                    s += o.a * stt.at(o.ph);
                }
                s *= 0.5f * shS; a += s; b += s;
            }
            if (roomActive) { float rl, rr; room.tick(0.5f * (a + b), rl, rr); a += 0.6f * spS * rl; b += 0.6f * spS * rr; }
            float dl = dryL.tick(L[i]), dr = dryR.tick(R[i]);
            float oL = (dl * (1.0f - mixS) + a * mixS) * outS;
            float oR = (dr * (1.0f - mixS) + b * mixS) * outS;
            if (!std::isfinite(oL)) oL = 0; if (!std::isfinite(oR)) oR = 0;
            lim.tick(oL, oR);
            float bl = bypL.tick(L[i]), br = bypR.tick(R[i]);
            if (byS > 0) { oL = oL * (1.0f - byS) + bl * byS; oR = oR * (1.0f - byS) + br * byS; }
            L[i] = oL; R[i] = oR;
        }
        minLimGain = std::min(minLimGain, lim.minGain);
        // shimmer ramp bookkeeping: shimmer oscillators ramp once per hop
        if (shS <= 0 && rampLeft == 0) for (auto& o : shim) { o.a = o.aT; o.inc = o.fT / sr; }
        count += n;
    }
};

} // namespace husk

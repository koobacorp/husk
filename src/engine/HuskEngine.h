// HUSK v2 — voice-into-instrument engine (mixer insert). No JUCE, no allocation on the audio path
// after prepare(). Original code.
//
// v2 replaces the voice instead of rebuilding it:
//   input ─┬─ pitch analysis every half hop (YIN) → octave guard, 150 ms gap ride, Note Snap, Glide
//          ├─ 28-band vowel analysis (on the latency-aligned input) → vowel shape + loudness + consonants
//          └─ latency-matched dry (Mix, A/B, Bypass)
//   oscillators (model × unison × MIDI notes, detune, width, drift)
//     + consonant noise tuned to the note (Air) → body → Partials brightness → 28-band vowel
//     shaping (Talk) → loudness follow → Phase (swept phase-cancellation) → shimmer → crunch → space
//     → mix → out → true-peak limiter.
// Reported latency = N/2 + H (analysis) + limiter lookahead. Mix 0 / Bypass null bit-exact.
#pragma once
#include "HuskAnalysis.h"
#include <atomic>

namespace husk {

enum Model { SawLead = 0, Reed, Glass, Bowed, Choir, Organ, Pluck, Sub, Ghost, kNumModels };
inline const char* modelName(int m) {
    static const char* n[] = { "Saw Lead", "Reed", "Glass", "Bowed String", "Choir", "Organ", "Pluck", "Sub", "Ghost" };
    return n[std::clamp(m, 0, kNumModels - 1)];
}
enum Scale { Chromatic = 0, Major, Minor, MajorPent, MinorPent, kNumScales };
inline const char* scaleName(int s) { static const char* n[] = { "Chromatic", "Major", "Minor", "Pentatonic", "Minor Pent." }; return n[std::clamp(s, 0, kNumScales - 1)]; }
inline const char* keyName(int k) { static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((k % 12) + 12) % 12]; }
// Note values for Steps and synced Phase Rate, in beats (quarter notes).
constexpr int kNumDivs = 6;
inline double divBeats(int i) { static const double b[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; return b[std::clamp(i, 0, kNumDivs - 1)]; }
inline const char* divName(int i) { static const char* n[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; return n[std::clamp(i, 0, kNumDivs - 1)]; }

constexpr int kMaxNotes = 6;      // Chord mode cap
constexpr int kMaxUnison = 7;
constexpr int kVoices = kMaxNotes * kMaxUnison;
constexpr int kVB = 28;           // vowel bands
constexpr int kGhostK = 32;       // Ghost partials
constexpr int kCtl = 16;          // control-rate interval (samples)

struct Params {
    int model = SawLead;
    float partials = 64;      // 1..64: brightness (key-tracked: about this many harmonics pass)
    float talk = 0.6f;        // 0..1: how strongly the voice's vowels shape the instrument
    float air = 0.15f;        // 0..1: consonants / breath, in the instrument's colour
    float pitch = 0;          // semitones -12..12
    int octave = 0;           // -1, 0, +1
    float formant = 0;        // semitones -12..12
    float phase = 0;          // 0..1 swept phase-cancellation depth (feedback flanger)
    float phaseRate = 1.0f;   // Hz (free)
    bool phaseSync = false; int phaseDiv = 3;
    int steps = 0;            // 0 off, 1..6 = divBeats(steps-1)
    bool freeze = false, chord = false;
    bool snap = false; int key = 0; int scale = Chromatic;
    float glideMs = 0;        // 0..1000
    int unison = 3;           // 1..7
    float detune = 0.35f, width = 0.6f, drift = 0.4f;
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
        an.prepare(sr); an.voicedThresh = 0.42f;   // we decide voicing ourselves (with hysteresis)
        N = an.N; H = an.H; HS = std::max(64, H / 2);
        octReads = std::max(1, (int)std::lround(0.06 * sr / HS));   // octave guard: 60 ms
        span = std::max(N, an.yinSpan());
        ringSize = 1; while (ringSize < span * 2 + 8) ringSize <<= 1;
        ring.assign((size_t)ringSize, 0.0f); lin.assign((size_t)span, 0.0f);
        lim.init(sr);
        dryDelay = N / 2 + H;
        latency = dryDelay + lim.la;
        envLead = std::min(dryDelay - 1, (int)std::lround(0.005 * sr));
        for (auto* d : { &dryL, &dryR }) { d->init(dryDelay); d->setDelay(dryDelay); }
        for (auto* d : { &bypL, &bypR }) { d->init(latency); d->setDelay(latency); }
        anaDelay.init(dryDelay); anaDelay.setDelay(dryDelay - envLead);
        room.init(sr);
        // vowel bands: log-spaced 100 Hz .. min(12 kHz, 0.45 fs)
        double top = std::min(12000.0, 0.45 * sr), bot = 100.0;
        bandStepSt = (float)(12.0 * std::log2(top / bot) / (kVB - 1));
        for (int b = 0; b < kVB; ++b) {
            double fc = bot * std::pow(top / bot, (double)b / (kVB - 1));
            vbFc[(size_t)b] = (float)fc;
            const double q = 3.2;
            aBp1[(size_t)b].set(sr, fc, q); aBp2[(size_t)b] = aBp1[(size_t)b];
            for (int c = 0; c < 2; ++c) { sBp1[c][(size_t)b] = aBp1[(size_t)b]; sBp2[c][(size_t)b] = aBp1[(size_t)b]; }
            double tau = std::max(0.005, 3.0 / fc);   // envelope smoothing: >= 3 cycles of the band
            envA[(size_t)b] = (float)(1.0 - std::exp(-1.0 / (tau * sr)));
            hfMask[(size_t)b] = fc >= 3000.0 ? 1.0f : 0.0f;
        }
        // consonant noise bank: same bands; calibrate so unit noise x gain sqrt(env*nNorm) gives the band's power
        for (int b = 0; b < kVB; ++b) {
            nBp1[(size_t)b] = aBp1[(size_t)b]; nBp2[(size_t)b] = aBp1[(size_t)b];
            BandPass t1 = aBp1[(size_t)b], t2 = aBp1[(size_t)b]; Rng r(99u + (uint32_t)b); double e = 0; const int M = (int)(0.5 * sr);
            for (int k = 0; k < M; ++k) { float y = t2.tick(t1.tick(r.bi() * 1.7320508f)); if (k > M / 5) e += (double)y * y; }
            e /= (M - M / 5); nNorm[(size_t)b] = (float)(1.0 / std::max(1e-12, e));
        }
        // Pluck strings
        ksLen = 1; while (ksLen < (int)(sr / 25.0) + 4) ksLen <<= 1;
        ks.assign((size_t)kVoices * (size_t)ksLen, 0.0f);
        combLen = ksLen; comb.assign((size_t)combLen, 0.0f);
        // Bowed-string body (fixed resonances, like a wooden box)
        static const double bf[3] = { 280.0, 470.0, 2900.0 }, bq[3] = { 3.0, 4.0, 2.0 }, bg[3] = { 7.0, 5.0, 4.0 };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 3; ++i) body[c][i].peak(sr, std::min(bf[i], 0.4 * sr), bq[i], bg[i]);
        for (int c = 0; c < 2; ++c) reedBody[c].peak(sr, std::min(1400.0, 0.4 * sr), 1.2, 4.0);
        reset();
    }
    void reset() {
        std::fill(ring.begin(), ring.end(), 0.0f); rw = 0; hopPos = 0; an.resetHistory(); fresh = true;
        dryL.clear(); dryR.clear(); bypL.clear(); bypR.clear(); anaDelay.clear(); room.clear(); lim.init(sr);
        for (auto& b : aBp1) b.clear(); for (auto& b : aBp2) b.clear();
        for (auto& b : nBp1) b.clear(); for (auto& b : nBp2) b.clear(); for (auto& pb : phBuf) pb.fill(0.0f); phW[0] = phW[1] = 0; tMsSlow = 0;
        for (int c = 0; c < 2; ++c) { for (auto& b : sBp1[c]) b.clear(); for (auto& b : sBp2[c]) b.clear(); for (auto& f : bright[c]) f.clear(); for (auto& f : body[c]) f.clear(); reedBody[c].clear(); }
        for (auto& e : env) e = 0; for (auto& g : gCur) g = 1; for (auto& g : gTgt) g = 1; for (auto& g : gDisp) g = 0;
        std::fill(ks.begin(), ks.end(), 0.0f); std::fill(comb.begin(), comb.end(), 0.0f); combW = 0;
        for (int v = 0; v < kVoices; ++v) {
            auto& o = vo[(size_t)v]; o = Voice(); Rng r(1234u + 977u * (uint32_t)v);
            o.ph = r.uni(); o.ph2 = r.uni(); o.lp = 0; o.driftT = 0; o.drift = 0; o.vibPh = r.uni(); o.vibRate = 4.6f + 1.2f * r.uni(); o.amp = 0;
        }
        for (auto& g : ghA) g = 0; for (auto& g : ghAT) g = 0; for (auto& row : ghPh) for (auto& q : row) q = 0;
        shPh[0] = 0.1; shPh[1] = 0.6;
        noise = Rng(777); drng = Rng(4242); rng = Rng(12345);
        live = Frame(); vMs = 0; tMs = 0; sMs = 0; agcG = 0; fastE = 0; slowE = 0; refractory = 0;
        cons = 0; consT = 0; nTiltLp = 0; nTiltA = 0.5f; tone = 0; toneT = 0;
        curLogF = 0; tgtLogF = 0; havePitch = false; holdAge = 1 << 30; jumpCount = 0; jumpLog = 0;
        med[0] = med[1] = med[2] = 0; medN = 0; snapNote = -1000;
        lastSung = Snap(); haveSung = false; frozen = Snap(); haveFrozen = false; stepSnap = Snap(); lastGrid = -1; freeBeat = 0; lastPhaseGrid = -1;
        noteMax = 0; lfoPh = 0; attackEnv = 0; shEnv = 0;
        nNotes = 0; fresh = true;
        snapSmoothers();
        voicedHops = 0; hops = 0; onsets = 0; octaveFolds = 0; gapRides = 0; tpeak = 0; minLimGain = 1;
    }
    void snapSmoothers() {
        mixS = std::clamp(p.mix, 0.0f, 1.0f); outS = dbToGain(std::clamp(p.outDb, -24.0f, 12.0f)); crS = std::clamp(p.crunch, 0.0f, 1.0f);
        shS = std::clamp(p.shimmer, 0.0f, 1.0f); spS = std::clamp(p.space, 0.0f, 1.0f); byS = (p.bypass || p.ab) ? 1.f : 0.f;
        talkS = std::clamp(p.talk, 0.0f, 1.0f); airS = std::clamp(p.air, 0.0f, 1.0f); phS = std::clamp(p.phase, 0.0f, 1.0f);
        detS = std::clamp(p.detune, 0.0f, 1.0f); widS = std::clamp(p.width, 0.0f, 1.0f); drS = std::clamp(p.drift, 0.0f, 1.0f);
        brS = std::clamp(p.partials, 1.0f, 64.0f);
    }
    void setParams(const Params& np) {
        p = np;
        p.model = std::clamp(p.model, 0, kNumModels - 1); p.partials = std::clamp(p.partials, 1.0f, 64.0f);
        p.unison = std::clamp(p.unison, 1, kMaxUnison); p.octave = std::clamp(p.octave, -1, 1);
        p.key = ((p.key % 12) + 12) % 12; p.scale = std::clamp(p.scale, 0, kNumScales - 1);
        an.wantPartials = p.model == Ghost;
    }
    void setTime(const HostTime& t) { host = t; }
    int latencySamples() const { return latency; }
    int hopSize() const { return H; }
    int frameSize() const { return N; }

    // ------------------------------------------------------------------ MIDI (Chord mode)
    void noteOn(int note) {
        for (int i = 0; i < nNotes; ++i) if (notes[(size_t)i] == note) return;
        if (nNotes >= kMaxNotes) { ++ignoredNotes; return; }
        notes[(size_t)nNotes++] = note; ++midiEvents; pendingPluck = true;
    }
    void noteOff(int note) {
        for (int i = 0; i < nNotes; ++i) if (notes[(size_t)i] == note) { for (int j = i; j + 1 < nNotes; ++j) notes[(size_t)j] = notes[(size_t)j + 1]; --nNotes; ++midiEvents; return; }
    }
    void allNotesOff() { nNotes = 0; }
    int heldNotes() const { return nNotes; }

    // ------------------------------------------------------------------ audio
    void process(float* L, float* R, int n) {
        if (fresh) { snapSmoothers(); fresh = false; }
        int pos = 0;
        while (pos < n) {
            int chunk = std::min({ n - pos, HS - hopPos, kCtl - ctlPos });
            renderChunk(L + pos, R + pos, chunk, pos);
            pos += chunk; hopPos += chunk; ctlPos += chunk;
            if (hopPos >= HS) { hopPos = 0; onHop(pos); }
            if (ctlPos >= kCtl) { ctlPos = 0; onCtl(); }
        }
    }

    // ------------------------------------------------------------------ telemetry (UI / tests)
    Frame live;
    float gDisp[kVB] {};                  // vowel shape applied (dB-ish, 0..1 for display)
    float dispF0 = 0; int dispNote = -1; bool dispVoiced = false; float dispTone = 0, dispCons = 0;
    long long voicedHops = 0, hops = 0, onsets = 0, octaveFolds = 0, gapRides = 0, midiEvents = 0, ignoredNotes = 0;
    float tpeak = 0, minLimGain = 1;
    float currentF0() const { return havePitch ? std::exp2(curLogF) : 0.0f; }
    float toneGate() const { return tone; }
    float consonant() const { return cons; }
    bool hasSungSnapshot() const { return haveSung; }
    bool isHolding() const { return haveFrozen; }
    const SafetyLimiter& limiter() const { return lim; }
    float bandFc(int b) const { return vbFc[(size_t)std::clamp(b, 0, kVB - 1)]; }

private:
    struct Voice { double ph = 0, ph2 = 0; float lp = 0, drift = 0, driftT = 0, vibPh = 0, vibRate = 5, amp = 0, pan = 0, gl = 0.7071f, gr = 0.7071f; };
    struct Snap { bool valid = false; float g[kVB] {}; float ms = 0; float logF = 0; float cons = 0; float tilt = 0.5f; float tone = 0; float ghost[kGhostK] {}; };

    Params p; HostTime host;
    double sr = 44100;
    Analyzer an;
    int HS = 128, octReads = 21; const float octClear = -1.0f;   // no exemption: every octave jump must persist   // pitch is re-read every half hop (the frame still spans N), so it trails the voice less
    int N = 1024, H = 256, span = 1024, ringSize = 4096, rw = 0, hopPos = 0, ctlPos = 0, dryDelay = 768, latency = 834, envLead = 220;
    std::vector<float> ring, lin;
    Delay dryL, dryR, bypL, bypR, anaDelay;
    SafetyLimiter lim;
    Room room;
    bool fresh = true;
    Rng noise { 777 }, drng { 4242 }, rng { 12345 };

    // vowel bands
    std::array<BandPass, kVB> aBp1, aBp2; std::array<BandPass, kVB> sBp1[2], sBp2[2];
    std::array<float, kVB> vbFc {}, envA {}, hfMask {};
    float env[kVB] {}, gCur[kVB] {}, gTgt[kVB] {};
    float bandStepSt = 3.2f;
    float vMs = 0;            // voice loudness (mean square, aligned)
    float tMs = 0, sMs = 0, agcG = 0;
    float fastE = 0, slowE = 0; int refractory = 0;
    float cons = 0, consT = 0, nTiltA = 0.5f, nTiltLp = 0;
    std::array<SvfLP, 2> bright[2];
    Biquad body[2][3], reedBody[2];

    // oscillators
    std::array<Voice, kVoices> vo;
    std::vector<float> ks; int ksLen = 4096; std::vector<float> comb; int combLen = 4096, combW = 0;
    float ghA[kGhostK] {}, ghAT[kGhostK] {}; double ghPh[kMaxNotes * 3][kGhostK] {};
    double shPh[2] {};
    std::array<int, kMaxNotes> notes {}; int nNotes = 0; bool pendingPluck = false;

    // pitch state
    float curLogF = 0, tgtLogF = 0; bool havePitch = false; int holdAge = 1 << 30;
    int jumpCount = 0; float jumpLog = 0; float med[3] {}; int medN = 0; int snapNote = -1000;
    float tone = 0, toneT = 0;
    bool voicedNow = false;

    // snapshots for Freeze and Steps
    Snap lastSung, frozen, stepSnap; bool haveSung = false, haveFrozen = false;
    float noteMax = 0;
    long long lastGrid = -1, lastPhaseGrid = -1; double freeBeat = 0;
    double lfoPh = 0; float attackEnv = 0, shEnv = 0;

    float mixS = 1, outS = 1, crS = 0, shS = 0, spS = 0, byS = 0, talkS = 0.7f, airS = 0.15f, phS = 0, detS = 0.35f, widS = 0.6f, drS = 0.3f, brS = 64;
    float bufW[2][kCtl] {};

    static float scaleHas(int scale, int pc) {
        static const int mj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mn[7] = { 0, 2, 3, 5, 7, 8, 10 }, pj[5] = { 0, 2, 4, 7, 9 }, pm[5] = { 0, 3, 5, 7, 10 };
        switch (scale) {
            case Major: for (int v : mj) if (v == pc) return 1; return 0;
            case Minor: for (int v : mn) if (v == pc) return 1; return 0;
            case MajorPent: for (int v : pj) if (v == pc) return 1; return 0;
            case MinorPent: for (int v : pm) if (v == pc) return 1; return 0;
            default: return 1;
        }
    }
    int nearestScaleNote(float midi) const {
        int best = (int)std::lround(midi); float bd = 1e9f;
        for (int d = -3; d <= 3; ++d) {
            int m = (int)std::lround(midi) + d; int pc = (((m - p.key) % 12) + 12) % 12;
            if (scaleHas(p.scale, pc) > 0) { float dd = std::fabs(midi - (float)m); if (dd < bd) { bd = dd; best = m; } }
        }
        return best;
    }

    // --------------------------------------------------------------- per-hop: pitch, voicing, consonants
    void onHop(int offsetInBlock) {
        for (int i = 0; i < span; ++i) { int idx = (rw - span + i) & (ringSize - 1); lin[(size_t)i] = ring[(size_t)idx]; }
        an.analyse(lin.data(), span, live);
        ++hops;
        // voicing with hysteresis: enter below 0.25 aperiodicity, stay below 0.42 if pitch is continuous
        float raw = 0; bool ok = false;
        // a frame whose newest quarter has gone (nearly) quiet straddles the end of a note: YIN slides
        // flat on such frames, so don't believe them (the note rides on its last good pitch instead)
        double eAll = 1e-20, eNew = 0; for (int i = span - N; i < span; ++i) { double v = lin[(size_t)i]; eAll += v * v; if (i >= span - N / 4) eNew += v * v; }
        const bool tailing = eNew < 0.25 * 0.15 * eAll;
        if (live.rms > an.gateRms && live.f0 > 0 && !tailing) {
            float ap = live.aperiodicity;
            bool cont = havePitch && holdAge < (int)(0.05 * sr) && std::fabs(std::log2(live.f0) - curLogF) < 0.15f;
            ok = ap < 0.25f || (cont && ap < 0.42f);
            if (ok) raw = std::log2(live.f0);
        }
        if (ok && havePitch && holdAge < (int)(0.15 * sr)) {
            // octave guard: inside a sung line an octave jump has to persist for 60 ms before it is
            // believed. Pitch trackers flip octaves on voices with a weak fundamental; a singer rarely
            // leaps a clean octave in under 60 ms.
            float ref = tgtLogF - (p.pitch + 12.0f * (float)p.octave) / 12.0f;
            if (snapNote > -1000) ref = lastRaw;
            float d = raw - ref, r = std::round(d);
            if (std::fabs(r) >= 1.0f && std::fabs(d - r) < 0.1f && live.aperiodicity > octClear) {
                if (jumpCount > 0 && std::fabs(raw - jumpLog) < 0.1f) ++jumpCount; else { jumpCount = 1; jumpLog = raw; }
                if (jumpCount < octReads) { raw -= r; ++octaveFolds; }
                else { jumpCount = 0; medN = 0; }   // believed: restart the median so it can't fold it back
            } else jumpCount = 0;
        }
        if (ok) lastRaw = raw;
        voicedNow = ok;
        if (ok) {
            ++voicedHops;
            // outlier-only median of 3: a smooth line (or a small step) goes straight through, so the
            // pitch does not lag the voice by a hop; only a jump of more than a semitone waits one hop
            float prevRaw = med[(medN + 2) % 3];
            med[medN % 3] = raw; ++medN;
            float m = raw;
            if (medN >= 3 && std::fabs(raw - prevRaw) > 1.0f / 12.0f) { float a = med[0], b = med[1], c = med[2]; m = std::max(std::min(a, b), std::min(std::max(a, b), c)); }
            if (!havePitch || tone < 0.05f) { medN = 1; med[0] = raw; m = raw; }
            float target = m + (p.pitch + 12.0f * (float)p.octave) / 12.0f;
            if (p.snap) {
                float midi = 69.0f + 12.0f * (target - std::log2(440.0f));
                bool keep = snapNote > -1000 && std::fabs(midi - (float)snapNote) < 0.65f && scaleHas(p.scale, (((snapNote - p.key) % 12) + 12) % 12) > 0;
                if (!keep) { int nn = nearestScaleNote(midi); if (nn != snapNote) { snapNote = nn; noteChanged = true; } }
                target = std::log2(440.0f) + ((float)snapNote - 69.0f) / 12.0f;
            } else snapNote = -1000;
            if (!havePitch || tone < 0.02f) { curLogF = target; }
            tgtLogF = target; havePitch = true; holdAge = 0; toneT = 1.0f;
        } else {
            if (holdAge < (int)(0.15 * sr)) { if (havePitch && holdAge == 0) ++gapRides; toneT = havePitch ? 1.0f : 0.0f; }
            else toneT = 0.0f;
            holdAge += HS;
        }
        // musical clock (Steps, synced Phase Rate)
        double bpm = (host.valid && host.bpm > 1) ? host.bpm : 120.0;
        if (host.valid && host.playing) beatNow = host.ppq + offsetInBlock / sr * bpm / 60.0;
        else { freeBeat += HS / sr * bpm / 60.0; beatNow = freeBeat; }
        // Ghost partial recipe (formant-shifted envelope read at the output harmonics)
        if (p.model == Ghost) {
            float fOut = std::exp2(tgtLogF), fr = std::pow(2.0f, p.formant / 12.0f);
            if (ok && live.ne > 0) {
                double e = 0;
                for (int k = 0; k < kGhostK; ++k) { float f = fOut * (k + 1); float a = (f < 0.45f * sr) ? live.env(f / fr) : 0.0f; ghAT[k] = a; e += (double)a * a; }
                float sc = e > 0 ? 1.0f / (float)std::sqrt(e) : 0.0f; for (auto& a : ghAT) a *= sc;
            }
        }
        dispF0 = havePitch ? std::exp2(curLogF) : 0.0f; dispVoiced = ok;
        dispNote = havePitch ? (int)std::lround(69.0 + 12.0 * (curLogF - std::log2(440.0))) : -1;
    }
    bool noteChanged = false; double beatNow = 0; float lastRaw = 0;

    // --------------------------------------------------------------- control rate (every 16 samples)
    void onCtl() {
        // live vowel shape: log band envelopes, formant-shifted, broad tilt removed, scaled by Talk
        float lg[kVB], sh[kVB];
        for (int b = 0; b < kVB; ++b) lg[b] = 0.5f * std::log(env[b] + 1e-14f);
        const float fsh = p.formant / bandStepSt;
        for (int b = 0; b < kVB; ++b) {
            float x = std::clamp((float)b - fsh, 0.0f, (float)(kVB - 1)); int i0 = (int)x; int i1 = std::min(i0 + 1, kVB - 1); float t = x - i0;
            sh[b] = lg[i0] + t * (lg[i1] - lg[i0]);
        }
        // least-squares line over bands (energy-weighted so silent top bands don't tilt the fit)
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0; float mx = -1e9f; for (int b = 0; b < kVB; ++b) mx = std::max(mx, sh[b]);
        for (int b = 0; b < kVB; ++b) { double w = std::max(0.05, (double)std::exp(std::max(-6.0f, sh[b] - mx))); sw += w; sx += w * b; sy += w * sh[b]; sxx += w * b * b; sxy += w * b * sh[b]; }
        double den = sw * sxx - sx * sx, slope = den > 1e-9 ? (sw * sxy - sx * sy) / den : 0.0, icp = (sy - slope * sx) / std::max(1e-9, sw);
        Snap now; now.valid = true;
        float ex = 1.35f * talkS;
        for (int b = 0; b < kVB; ++b) { float s = std::clamp(sh[b] - (float)(icp + slope * b), -4.0f, 3.0f); now.g[b] = std::exp(ex * s); }
        // consonants: share of energy above 3 kHz, only when the voice is not clearly pitched
        double eAll = 1e-14, eHi = 0; for (int b = 0; b < kVB; ++b) { eAll += env[b]; eHi += env[b] * hfMask[(size_t)b]; }
        float hf = (float)(eHi / eAll);
        float c = std::clamp((hf - 0.18f) / 0.35f, 0.0f, 1.0f) * (voicedNow ? 0.12f : 1.0f);
        if (vMs < 1e-9f) c = 0;
        now.cons = c; now.ms = vMs; now.logF = tgtLogF; now.tone = toneT;
        now.tilt = std::clamp(0.5f + (float)slope * 4.0f, 0.0f, 1.0f);   // rising spectrum ("s") -> brighter noise
        for (int k = 0; k < kGhostK; ++k) now.ghost[k] = ghAT[k];
        // remember the last properly sung moment (for Freeze)
        if (voicedNow && toneT > 0.5f) {
            noteMax = std::max(noteMax * 0.9995f, vMs);
            if (vMs > 0.3f * noteMax && vMs > 1e-7f) { lastSung = now; haveSung = true; }
        }
        // choose the source: Freeze > Steps > live
        const Snap* src = &now;
        if (p.freeze) {
            if (!haveFrozen && haveSung) { frozen = lastSung; haveFrozen = true; }
            if (haveFrozen) src = &frozen;
        } else haveFrozen = false;
        if (!haveFrozen && p.steps > 0) {
            long long gi = (long long)std::floor(beatNow / divBeats(p.steps - 1) + 1e-9);
            if (gi != lastGrid || !stepSnap.valid) { stepSnap = now; lastGrid = gi; if (stepSnap.tone > 0.5f) pendingPluck = true; }
            src = &stepSnap;
        } else lastGrid = -1;
        for (int b = 0; b < kVB; ++b) gTgt[b] = src->g[b];
        tMsTarget = src->ms; consT = src->cons; nTiltA = src->tilt;
        if (src != &now) { tgtLogFUse = src->logF; toneUse = haveFrozen ? 1.0f : src->tone; }
        else { tgtLogFUse = tgtLogF; toneUse = toneT; }
        if (p.model == Ghost) for (int k = 0; k < kGhostK; ++k) ghAT2[k] = src->ghost[k];
        // onsets (Pluck re-strikes, Glass shine)
        float ms = vMs;
        fastE += 0.5f * (ms - fastE); slowE += (1.0f - std::exp(-(float)kCtl / (0.06f * (float)sr))) * (ms - slowE);
        if (refractory > 0) refractory -= kCtl;
        if (refractory <= 0 && fastE > 2.5f * slowE + 1e-9f && fastE > 1e-6f && !haveFrozen && p.steps == 0) { pendingPluck = true; refractory = (int)(0.08 * sr); ++onsets; noteMax = 0; }
        if (noteChanged) { noteChanged = false; if (!haveFrozen && p.steps == 0) pendingPluck = true; }
        for (int b = 0; b < kVB; ++b) gDisp[b] = std::clamp(0.5f + 0.12f * std::log(gCur[b] + 1e-6f), 0.0f, 1.0f);
        dispTone = tone; dispCons = cons;
    }
    float tMsTarget = 0, tgtLogFUse = 0, toneUse = 0, ghAT2[kGhostK] {};

    // --------------------------------------------------------------- per-sample rendering
    static inline float saw(double ph, double dt) { return (float)(2.0 * ph - 1.0) - polyBlep(ph, dt); }
    static inline float square(double ph, double dt) {
        double q = ph + 0.5; if (q >= 1.0) q -= 1.0;
        return (float)((ph < 0.5 ? 1.0 : -1.0) + polyBlep(ph, dt) - polyBlep(q, dt));
    }

    void renderChunk(float* L, float* R, int n, int /*offset*/) {
        const auto& st = sineTable();
        const float sm = 1.0f - std::exp(-1.0f / (0.02f * (float)sr));
        const int model = p.model;
        const bool chordOn = p.chord && nNotes > 0;
        const int banks = chordOn ? nNotes : 1;
        int uni = p.unison; if (model == Choir) uni = std::max(uni, 4); if (model == Pluck || model == Ghost) uni = std::min(uni, 3);
        const float ratio = std::exp2((p.pitch + 12.0f * (float)p.octave) / 12.0f);
        const float gl = std::max(0.003f, p.glideMs * 0.001f);
        const float pitchA = 1.0f - std::exp(-1.0f / (gl * (float)sr));
        const float toneA = 1.0f - std::exp(-1.0f / ((toneUse > tone ? 0.006f : 0.045f) * (float)sr));
        const float consUp = 1.0f - std::exp(-1.0f / (0.0015f * (float)sr)), consDn = 1.0f - std::exp(-1.0f / (0.03f * (float)sr));
        const float agcT = model == Pluck ? 0.15f : (model == Bowed ? 0.025f : 0.008f);
        const float agcA = 1.0f - std::exp(-1.0f / (agcT * (float)sr));
        const float vA = 1.0f - std::exp(-1.0f / (0.005f * (float)sr));
        const float gA = 1.0f - std::exp(-1.0f / (0.003f * (float)sr));
        const float ctlA = 0.5f / (float)kCtl;
        double lfoInc;
        if (p.phaseSync) { double bpm = (host.valid && host.bpm > 1) ? host.bpm : 120.0; lfoInc = 1.0 / (divBeats(p.phaseDiv) * 60.0 / bpm * sr); }
        else lfoInc = std::clamp(p.phaseRate, 0.01f, 40.0f) / sr;
        float combFb = 0.5f; switch (model) { case Reed: combFb = 0.8f; break; case Glass: combFb = 0.92f; break; case Bowed: combFb = 0.7f; break; case Choir: combFb = 0.3f; break; case Organ: combFb = 0.75f; break; case Pluck: combFb = 0.95f; break; case Sub: combFb = 0.6f; break; case Ghost: combFb = 0.0f; break; default: break; }
        const float nativeBright = model == Bowed ? 10.0f : (model == Choir ? 16.0f : (model == Sub ? 6.0f : (model == SawLead ? 26.0f : 64.0f)));
        const float brQ = model == SawLead ? 1.3f : (model == Reed ? 0.9f : 0.6f);

        float* wl = bufW[0]; float* wr = bufW[1];
        for (int i = 0; i < n; ++i) {
            // ---- input and voice analysis (latency-aligned)
            float x = 0.5f * (L[i] + R[i]);
            ring[(size_t)rw] = x; rw = (rw + 1) & (ringSize - 1);
            float xa = anaDelay.tick(x);
            vMs += vA * (xa * xa - vMs);
            for (int b = 0; b < kVB; ++b) { float y = aBp2[(size_t)b].tick(aBp1[(size_t)b].tick(xa)); env[b] += envA[(size_t)b] * (y * y - env[b]); }
            // ---- smoothers
            for (int b = 0; b < kVB; ++b) gCur[b] += ctlA * (gTgt[b] - gCur[b]);
            talkS += sm * (std::clamp(p.talk, 0.0f, 1.0f) - talkS); airS += sm * (std::clamp(p.air, 0.0f, 1.0f) - airS);
            phS += sm * (std::clamp(p.phase, 0.0f, 1.0f) - phS); detS += sm * (std::clamp(p.detune, 0.0f, 1.0f) - detS);
            widS += sm * (std::clamp(p.width, 0.0f, 1.0f) - widS); drS += sm * (std::clamp(p.drift, 0.0f, 1.0f) - drS);
            brS += sm * (p.partials - brS);
            tone += toneA * (toneUse - tone);
            cons += (consT > cons ? consUp : consDn) * (consT - cons);
            tMs += vA * (tMsTarget - tMs);
            if (havePitch) curLogF += pitchA * (tgtLogFUse - curLogF);
            attackEnv *= 0.99985f;
            lfoPh += lfoInc; if (lfoPh >= 1.0) lfoPh -= 1.0;

            float fBase = havePitch ? std::exp2(curLogF) : 220.0f;
            if (pendingPluck) { pendingPluck = false; exciteAll(chordOn, fBase, ratio, uni); attackEnv = 1.0f; }

            // ---- oscillators (tone path, not gated yet)
            float cl = 0, cr = 0;
            const float detC = 70.0f * detS * detS + 5.0f * detS;   // up to +-75 cents across the stack
            const float drC = 30.0f * drS * drS + 6.0f * drS;       // up to +-36 cents slow wander
            for (int bk = 0; bk < banks; ++bk) {
                float fN = chordOn ? 440.0f * std::exp2((notes[(size_t)bk] - 69) / 12.0f) * ratio : fBase;
                for (int u = 0; u < uni; ++u) {
                    int v = bk * kMaxUnison + u; auto& o = vo[(size_t)v];
                    float spread = uni > 1 ? ((float)u / (uni - 1)) * 2.0f - 1.0f : 0.0f;
                    if (((i + v) & 63) == 0) { if (drng.uni() < 0.03f) o.driftT = drng.bi(); o.drift += 0.06f * (o.driftT - o.drift); }
                    float cents = detC * spread + drC * o.drift;
                    if (model == Choir || model == Bowed) { o.vibPh += o.vibRate / (float)sr; if (o.vibPh >= 1) o.vibPh -= 1; cents += (model == Choir ? 7.0f : 4.0f) * (1.0f + drS) * st.at(o.vibPh); }
                    float f = fN * std::exp2(cents / 1200.0f);
                    double dt = std::min(0.45, (double)f / sr);
                    float target = 1.0f / std::sqrt((float)(uni * banks)) * (1.0f + 0.3f * drS * o.drift);
                    o.amp += 0.002f * (target - o.amp);
                    float pan = (uni > 1 ? spread * widS : 0.0f) + (banks > 1 ? 0.35f * widS * (((float)bk / (banks - 1)) * 2.0f - 1.0f) : 0.0f);
                    pan = std::clamp(pan, -1.0f, 1.0f);
                    float th = (pan + 1.0f) * 0.25f * (float)kPi; float gL = std::cos(th) * 1.41421356f, gR = std::sin(th) * 1.41421356f;
                    float s = voiceSample(o, v, bk, model, dt, f, st) * o.amp;
                    cl += s * gL; cr += s * gR;
                }
            }
            if (model == Bowed) { float bow = 0.06f * noise.bi(); cl += bow; cr += bow; }
            // ---- body
            if (model == Bowed) { for (int k = 0; k < 3; ++k) { cl = body[0][k].tick(cl); cr = body[1][k].tick(cr); } cl *= 0.5f; cr *= 0.5f; }
            else if (model == Reed) { cl = reedBody[0].tick(cl); cr = reedBody[1].tick(cr); }
            // ---- Partials = brightness (key-tracked low-pass, 24 dB/oct)
            float bEff = std::min(brS, nativeBright);
            if ((i & 63) == 0) { if (drng.uni() < 0.01f) filtDriftT = drng.bi(); filtDrift += 0.02f * (filtDriftT - filtDrift); }
            const float fdOct = drS * drS * (1.6f + 1.4f * filtDrift);   // analog drift also wanders the filter
            if (bEff < 63.5f || fdOct > 0.02f) {
                if ((i & 7) == 0) {
                    double fc = (bEff < 63.5f ? (double)fBase * (0.6 + 1.1 * bEff) : 0.45 * sr) * std::exp2(-(double)fdOct);
                    fc = std::clamp(fc, 40.0, 0.45 * sr);
                    for (int c = 0; c < 2; ++c) { bright[c][0].set(sr, fc, brQ); bright[c][1].set(sr, fc, 0.6); } }
                cl = bright[0][1].tick(bright[0][0].tick(cl)); cr = bright[1][1].tick(bright[1][0].tick(cr));
            }
            // ---- vowel shaping (Talk); Ghost already carries the voice's own shape
            float sl = cl, sr2 = cr;
            if (model != Ghost) {
                sl = 0; sr2 = 0;
                for (int b = 0; b < kVB; ++b) {
                    float g = (b & 1) ? -gCur[b] : gCur[b];
                    sl += g * sBp2[0][(size_t)b].tick(sBp1[0][(size_t)b].tick(cl));
                    sr2 += g * sBp2[1][(size_t)b].tick(sBp1[1][(size_t)b].tick(cr));
                }
            }
            // ---- follow the singer's loudness (measured on the running instrument, then gated)
            float m2 = 0.5f * (sl * sl + sr2 * sr2);
            sMs += agcA * (m2 - sMs);
            float tgtMs = (model == Pluck) ? tMsSlowUpdate(agcA) : tMs;
            float want = std::min(std::sqrt(tgtMs / (sMs + 1e-12f)), 30.0f);
            agcG += gA * (want - agcG);
            float tg = (model == Pluck) ? 1.0f : tone;
            float a = sl * agcG * tg, b = sr2 * agcG * tg;
            // ---- consonants: noise through the voice's own band levels, tuned to the note (Air)
            if (airS > 1e-4f && (cons > 1e-4f || consT > 0)) {
                float nz = noise.bi() * 1.7320508f, nv = 0;
                for (int bnd = 0; bnd < kVB; ++bnd) {
                    float g = std::sqrt(env[bnd] * nNorm[(size_t)bnd]);
                    nv += ((bnd & 1) ? -g : g) * nBp2[(size_t)bnd].tick(nBp1[(size_t)bnd].tick(nz));
                }
                if (combFb > 0) {
                    float per = (float)sr / std::max(30.0f, fBase);
                    float rp = (float)combW - per; while (rp < 0) rp += (float)combLen; int i0 = (int)rp; float fr = rp - i0; int i1 = (i0 + 1) % combLen;
                    float d = comb[(size_t)i0] + fr * (comb[(size_t)i1] - comb[(size_t)i0]);
                    float y = nv + combFb * d;
                    comb[(size_t)combW] = y; combW = (combW + 1) % combLen;
                    nv = y * std::sqrt(1.0f - combFb * combFb);
                }
                float ng = 2.2f * airS * cons;
                a += ng * nv; b += ng * nv;
            }
            // ---- Phase: swept phase-cancellation (stereo, feedback), speed = Phase Rate
            if (phS > 1e-4f) {
                for (int c = 0; c < 2; ++c) {
                    float in = c == 0 ? a : b;
                    double ph = lfoPh + (c ? 0.25 : 0.0); float lfo = 0.5f - 0.5f * (float)std::cos(kTwoPi * ph);
                    float dly = (float)sr * (0.0002f + 0.0075f * lfo * (0.3f + 0.7f * phS));
                    auto& buf = phBuf[c]; int& w = phW[c];
                    float rp = (float)w - dly; while (rp < 0) rp += (float)kPhLen; int i0 = (int)rp; float fr = rp - i0; int i1 = (i0 + 1) & (kPhLen - 1);
                    float d = buf[(size_t)i0] + fr * (buf[(size_t)i1] - buf[(size_t)i0]);
                    buf[(size_t)w] = in + 0.9f * phS * d; w = (w + 1) & (kPhLen - 1);
                    float o = (in + phS * d) / (1.0f + 2.0f * phS);
                    if (c == 0) a = o; else b = o;
                }
            }
            // ---- shimmer: an octave above, with a long tail
            shS += sm * (std::clamp(p.shimmer, 0.0f, 1.0f) - shS);
            if (shS > 1e-4f) {
                float lv = std::sqrt(tMs) * (model == Pluck ? 1.0f : tone);
                shEnv += (lv > shEnv ? 0.0005f : 0.00003f) * (lv - shEnv);
                float fs = 2.0f * fBase;
                if (fs < 0.4f * (float)sr) {
                    shPh[0] += fs * 1.004 / sr; shPh[1] += fs * 0.996 / sr; for (auto& q : shPh) if (q >= 1.0) q -= 1.0;
                    float s0 = st.at(shPh[0]) + 0.3f * st.at(3.0 * shPh[0]) + 0.15f * st.at(2.0 * shPh[1]), s1 = st.at(shPh[1]) + 0.3f * st.at(3.0 * shPh[1]) + 0.15f * st.at(2.0 * shPh[0]);
                    a += 4.8f * shS * shEnv * s0; b += 4.8f * shS * shEnv * s1;
                }
            } else shEnv = 0;
            wl[i] = a; wr[i] = b;
            if (!std::isfinite(wl[i]) || !std::isfinite(wr[i])) { wl[i] = wr[i] = 0; agcG = 0; sMs = 0; }
        }
        finish(L, R, n);
    }
    float tMsSlow = 0, filtDrift = 0, filtDriftT = 0;
    float tMsSlowUpdate(float a) { tMsSlow += a * (tMs - tMsSlow); return std::max(tMsSlow, tMs * 0.25f); }
    static constexpr int kPhLen = 2048;
    std::array<float, kPhLen> phBuf[2] {}; int phW[2] {};
    std::array<BandPass, kVB> nBp1, nBp2; std::array<float, kVB> nNorm {};

    inline float voiceSample(Voice& o, int v, int bk, int model, double dt, float f, const SineTable& st) {
        float s = 0;
        switch (model) {
            case SawLead: {
                o.ph2 += dt * 0.5; if (o.ph2 >= 1.0) o.ph2 -= 1.0;
                s = saw(o.ph, dt) * 0.6f + 0.28f * square(o.ph2, dt * 0.5); break; }
            case Bowed: s = saw(o.ph, dt) * 0.6f; break;
            case Reed: {
                float sq = square(o.ph, dt);
                float c = 1.0f - std::exp(-(float)kTwoPi * std::min(0.45f, 7.0f * f / (float)sr));
                o.lp += c * (sq - o.lp); s = 0.55f * o.lp + 0.15f * sq; break; }
            case Choir: {
                float r = saw(o.ph, dt);
                float c = 1.0f - std::exp(-(float)kTwoPi * std::min(0.45f, 4.0f * f / (float)sr));
                o.lp += c * (r - o.lp); s = o.lp * 0.9f + 0.1f * r; break; }
            case Organ: {
                static const float rt[7] = { 0.5f, 1, 2, 3, 4, 6, 8 }, am[7] = { 0.45f, 1, 0.7f, 0.45f, 0.4f, 0.25f, 0.2f };
                for (int k = 0; k < 7; ++k) if (rt[k] * dt < 0.45) s += am[k] * st.at(o.ph * rt[k] + (k == 0 ? 0.0 : 0.13 * k));
                s *= 0.42f; break; }
            case Glass: {
                static const float rt[5] = { 1, 2.756f, 5.404f, 8.933f, 2.0f }, am[5] = { 1, 0.55f, 0.35f, 0.22f, 0.25f };
                float shine = 0.6f + 1.6f * attackEnv;
                for (int k = 0; k < 5; ++k) if (rt[k] * dt < 0.45) s += am[k] * (k > 0 && k < 4 ? shine : 1.0f) * st.at(o.ph * rt[k]);
                s *= 0.5f; break; }
            case Sub: {
                o.ph2 += dt * 0.5; if (o.ph2 >= 1.0) o.ph2 -= 1.0;
                s = 0.8f * st.at(o.ph) + 0.65f * st.at(o.ph2) + 0.12f * saw(o.ph, dt); break; }
            case Pluck: {
                float* line = ks.data() + (size_t)v * (size_t)ksLen;
                float per = (float)(1.0 / std::max(dt, 1e-4)); per = std::min(per, (float)(ksLen - 4));
                int& w = ksW[(size_t)v];
                float rp = (float)w - per; while (rp < 0) rp += (float)ksLen; int i0 = (int)rp; float fr = rp - i0; int i1 = (i0 + 1) & (ksLen - 1);
                float y = line[i0] + fr * (line[i1] - line[i0]);
                float dec = std::pow(10.0f, -3.0f / (2.2f * std::max(f, 30.0f)));
                float yn = dec * (0.5f * (y + o.lp)); o.lp = y;
                line[w] = yn; w = (w + 1) & (ksLen - 1);
                s = y * 1.2f; break; }
            case Ghost: {
                int g = std::min(bk * 3 + std::min(v - bk * kMaxUnison, 2), kMaxNotes * 3 - 1);
                for (int k = 0; k < kGhostK; ++k) {
                    double inc = dt * (k + 1); if (inc >= 0.45) break;
                    if (g == 0) ghA[k] += 0.0015f * (ghAT2[k] - ghA[k]);
                    double& ph = ghPh[g][k]; ph += inc; if (ph >= 1.0) ph -= 1.0;
                    s += ghA[k] * st.at(ph);
                }
                s *= 0.8f; break; }
            default: break;
        }
        o.ph += dt; if (o.ph >= 1.0) o.ph -= 1.0;
        return s;
    }
    std::array<int, kVoices> ksW {};

    void exciteAll(bool chordOn, float fBase, float ratio, int uni) {
        if (p.model != Pluck) return;
        int banks = chordOn ? nNotes : 1;
        for (int bk = 0; bk < banks; ++bk) {
            float fN = chordOn ? 440.0f * std::exp2((notes[(size_t)bk] - 69) / 12.0f) * ratio : fBase;
            int per = std::clamp((int)(sr / std::max(30.0f, fN)), 2, ksLen - 4);
            for (int u = 0; u < uni; ++u) {
                int v = bk * kMaxUnison + u; float* line = ks.data() + (size_t)v * (size_t)ksLen; int w = ksW[(size_t)v];
                float lp = 0, bc = std::clamp(0.15f + brS / 64.0f, 0.1f, 1.0f);
                for (int k = 0; k < per; ++k) { float nz = rng.bi(); lp += bc * (nz - lp); int idx = (w - per + k) & (ksLen - 1); line[idx] = 0.6f * line[idx] + 0.9f * lp; }
            }
        }
    }

    void finish(float* L, float* R, int n) {
        const float* wl = bufW[0]; const float* wr = bufW[1];
        const float mixT = (p.ab || p.bypass) ? mixS : std::clamp(p.mix, 0.0f, 1.0f);
        const float outT = dbToGain(std::clamp(p.outDb, -24.0f, 12.0f));
        const float byT = (p.bypass || p.ab) ? 1.0f : 0.0f;
        const float sm = 1.0f - std::exp(-1.0f / (0.02f * (float)sr));
        const float spaceT = std::clamp(p.space, 0.0f, 1.0f);
        if (spaceT > 1e-4f || spS > 1e-4f) { if (!roomActive) { room.clear(); roomActive = true; } room.setDecay(0.4f + 3.2f * std::max(spaceT, spS)); }
        else if (roomActive) { room.clear(); roomActive = false; }
        for (int i = 0; i < n; ++i) {
            crS += sm * (std::clamp(p.crunch, 0.0f, 1.0f) - crS); if (std::fabs(crS - p.crunch) < 1e-6f) crS = std::clamp(p.crunch, 0.0f, 1.0f);
            spS += sm * (spaceT - spS); if (std::fabs(spS - spaceT) < 1e-6f) spS = spaceT;
            mixS += sm * (mixT - mixS); if (std::fabs(mixS - mixT) < 1e-6f) mixS = mixT;
            outS += sm * (outT - outS); if (std::fabs(outS - outT) < 1e-7f) outS = outT;
            byS += sm * (byT - byS); if (std::fabs(byS - byT) < 1e-6f) byS = byT;
            float a = wl[i], b = wr[i];
            if (crS > 0) {   // Crunch: driven tanh, level-compensated
                float d = 1.0f + 24.0f * crS * crS, comp = 1.0f / std::tanh(d * 0.3f) * 0.3f;
                float ta = std::tanh(d * a + 0.15f * crS) - std::tanh(0.15f * crS), tb = std::tanh(d * b + 0.15f * crS) - std::tanh(0.15f * crS);
                float bl = std::min(1.0f, crS * 4.0f);
                a += bl * (ta * comp - a); b += bl * (tb * comp - b);
            }
            if (roomActive) { float rl, rr; room.tick(0.5f * (a + b), rl, rr); a = a * (1.0f - 0.45f * spS) + 1.25f * spS * rl; b = b * (1.0f - 0.45f * spS) + 1.25f * spS * rr; }
            float dl = dryL.tick(L[i]), dr = dryR.tick(R[i]);
            float oL = (dl * (1.0f - mixS) + a * mixS) * outS;
            float oR = (dr * (1.0f - mixS) + b * mixS) * outS;
            if (!std::isfinite(oL)) oL = 0; if (!std::isfinite(oR)) oR = 0;
            lim.tick(oL, oR);
            float bl = bypL.tick(L[i]), br = bypR.tick(R[i]);
            if (mixS == 0.0f && mixT == 0.0f && outS == 1.0f && outT == 1.0f) { oL = bl; oR = br; }   // pure dry: bit-exact
            if (byS > 0) { oL = oL * (1.0f - byS) + bl * byS; oR = oR * (1.0f - byS) + br * byS; }
            L[i] = oL; R[i] = oR;
        }
        minLimGain = std::min(minLimGain, lim.minGain);
    }
    bool roomActive = false;
};

} // namespace husk

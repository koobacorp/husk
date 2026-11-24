# HUSK v2

A voice-into-instrument mixer insert (VST3). Put HUSK on the mixer track of a vocal or a hum. It follows the voice — its pitch, loudness and vowels — and plays a real instrument with it. By default you hear **only the instrument** (a warm, detuned Saw Lead): the voice itself is replaced, not layered.

It is an **effect**, not a generator. It also takes MIDI in, so you can play chords on it from the piano roll while you sing.

## How it works

- **Pitch**: the voice's pitch is read about every 3 ms. An octave guard ignores octave flips shorter than 60 ms, short gaps (up to 150 ms, e.g. consonants) keep the note going, and optional **Note Snap** locks notes to a key and scale with **Glide** between them.
- **Vowels**: 28 bands (100 Hz–12 kHz) read the vowel shape of the voice and impose it on the instrument (vocoder-style). **Talk** sets how strongly.
- **Loudness** follows the voice frame by frame.
- **Air**: consonants (s, t, k …) become noise in the instrument's own colour, tuned to the note. Held notes get no hiss.

## Controls

- **Model** — Saw Lead, Reed, Glass, Bowed String, Choir, Organ, Pluck, Sub, Ghost (additive, the closest to the voice).
- **Partials** — brightness (from all harmonics down to one).
- **Talk** — vowel shaping strength. **Air** — consonants (default 15%).
- **Pitch** (±12 semitones), **Octave** (−1 / 0 / +1), **Formant** (move the vowels without moving the pitch).
- **Unison** (1–7 voices), **Detune**, **Width**, **Drift** (slow analogue wander of pitch and filter).
- **Note Snap**, **Key**, **Scale**, **Glide**.
- **Phase** and **Phase Rate** (free Hz or tempo-synced) — a swept phase-cancellation (flanger-like) motion.
- **Steps** — hold each sound for a tempo-synced step (1/4 to 1/32, triplets included).
- **Freeze** — hold the last sung moment as an endless pad (engaged before singing, it catches the first sung moment).
- **Chord (MIDI)** — play up to 6 notes from MIDI; the voice's loudness and vowels drive each note.
- **Shimmer**, **Space** (reverb), **Crunch** (saturation), **Mix**, **Out**, **A/B**, **Bypass**.
- 11 presets: Synth Lead, Talkbox, Glass Choir, Bowed Pad, Organ Hymn, Pluck Chops, Sub Voice, Ghost, Frozen Halo, Night Bus, Broken Radio.

Sessions saved with v1 still open: the old model is mapped to the closest v2 model and the removed Water/Grain pad is dropped.

## Latency and loudness

HUSK reports a fixed latency to the host: 18.9 ms at 44.1/88.2 kHz, 17.5 ms at 48/96 kHz. Hosts compensate automatically. The dry path is delayed by exactly the same amount, so Mix 0, A/B and Bypass are bit-exact copies of the input.

A true-peak safety limiter at the end keeps the output under −0.1 dBFS even with every gain at the top. At normal levels it does not engage.

## Building

```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

macOS universal: add `-G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. See `.github/workflows/macos.yml`. Tests: [TESTING.md](TESTING.md).

The test vocal in `tests/data/` is a CC BY-SA 4.0 recording; see `tests/data/README.md`.

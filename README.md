# HUSK

A voice-into-instrument mixer insert for FL Studio. Put HUSK on the mixer track of a vocal (or any monophonic sound), the same way you would put on an EQ or a reverb. It listens to the voice, breaks it into its pure tones, and plays those tones back on a different instrument body while keeping the words, the vowel shapes and the breath.

It is an **effect**, not a generator. It also takes MIDI in, so you can play chords on it from the piano roll.

## Controls

- **Model** — Sine, Saw, Square, Glass, Flute, Choir, String.
- **Partials** — from the full voice (64 tones) down to one pure note.
- **Pitch** (±12 semitones) and **Octave** (−1 / 0 / +1).
- **Formant** — move the vowel shape without moving the pitch.
- **Water / Grain pad** — X: Glide ↔ Water (smear), Y: Soft ↔ Grain (stutter texture).
- **Phase** and **Phase Rate** (free Hz or tempo-synced) — phase distortion of the partials.
- **Steps** — hold each sound for a tempo-synced step (1/4 to 1/32, triplets included), locked to the host tempo.
- **Freeze** — hold the current sound as an endless pad.
- **Chord (MIDI)** — play up to 6 notes from MIDI; the voice's tone and words are put on each note.
- **Air** (breath and consonants), **Shimmer**, **Space** (reverb), **Crunch** (saturation), **Mix**, **Out**.
- A/B and Bypass. 11 factory presets.

## Latency and loudness

HUSK has to hear a short slice of the voice before it can rebuild it, so it reports a fixed latency to the host (about 18.9 ms at 44.1 kHz, 17.5 ms at 48 kHz). FL Studio compensates for it automatically. The dry path is delayed by exactly the same amount, so Mix at 0 cancels perfectly against the dry signal.

A true-peak safety limiter (4× oversampled detection, inside the reported latency) sits at the very end, so the output stays below 0 dBTP even with every gain knob at the top. At normal levels it does not engage.

## Building

```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

macOS universal: add `-G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. See `.github/workflows/macos.yml`. Tests are described in [TESTING.md](TESTING.md); the design is in [PLAN.md](PLAN.md).

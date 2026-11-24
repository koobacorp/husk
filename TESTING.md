# HUSK tests

Three test programs. All run in CI on every push, and any failure fails the build.

## husk_quality_tests — "is it really an instrument?" (no JUCE)

Runs on a real unaccompanied sung vocal (`tests/data/welsh_vocal_12s.wav`, CC BY-SA 4.0) and on a generated sung phrase (vowels, vibrato, glides, consonants). Targets:

| Gate | Target |
|---|---|
| Output pitch within 25 cents of the singer | ≥ 95% of sung frames |
| With Note Snap: output locked to a scale note (25 cents) | ≥ 95% |
| Loudness correlation with the singer | ≥ 0.9 |
| Changing the vowel moves the output | ≥ 4 dB |
| Hiss (noise share) on held notes | < 10% |
| Default sound vs the dry voice | ≥ 10 dB |
| Every pair of presets | ≥ 6 dB apart |
| Every control at its extreme vs the default | ≥ 3 dB |

Distances are the mean absolute difference of level-normalised 64-band mel spectra (dB) on active frames; for controls, the larger of that and a fine 4096-point harmonic spectrum. Width is measured as stereo side/mid change, Out as level, Air on consonant moments. Pitch is measured with YIN plus a subharmonic-summation octave check, the same meter for input and output. Extra WAVs can be added with `--extra file.wav` (reported, not gated).

## husk_engine_tests (no JUCE)

Latency (≤ 19 ms at 44.1/48/88.2/96 kHz, and measured on the wet path), bit-exact Mix 0 / Bypass / A/B with hot input, true peak ≤ −0.1 dBFS for every model with Out +12 dB, a 30 s random fuzz (finite, no subnormals), pitch G2–G5, octave guard, gap riding, Note Snap, Glide, Freeze (last sung moment, and engaged before singing), MIDI chord pitches, Steps on the host grid, Partials, Width, Formant, Pitch/Octave and worst-case CPU.

## husk_plugin_tests (JUCE)

Plugin identity, buses, latency reported to the host, Mix 0 / Bypass / host-bypass nulls, MIDI chords through processBlock, Steps with a host play head, every parameter exercised, state save/restore, presets, loading v1 sessions, fuzz, CPU through processBlock, and editor screenshots (`shots/v2-*.png`).

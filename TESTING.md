# HUSK tests

Two test programs, both run in CI on every push.

## husk_engine_tests (no JUCE)

Checks the engine against textbook math:

- declared latency at 44.1/48/88.2/96 kHz is under 25 ms, and the measured wet latency matches it
- Mix 0 nulls against the latency-compensated dry, with every colour knob up
- pitch tracking G2..G5 at -37/0/+23 cents (worst error in cents)
- partial tracking on harmonic tones (frequency in cents, level in dB), voice round trip, sine sweeps
- Partials = 1 gives one pure tone; Pitch/Octave shifts; Formant moves the envelope, not the pitch
- each instrument model's harmonic shape (Saw 1/n, Square odd 1/n, ...)
- Freeze stability, MIDI chord pitch accuracy, Steps locked to host tempo
- Phase, Water/Grain pad, Air, Shimmer, Space, Crunch each do what they say
- safety ceiling: hot inputs with Out +12 dB stay under 0 dBTP
- 30 s random automation fuzz: finite, no subnormals, under 0 dBTP
- CPU budget for the worst case (6-note chord + Choir + Phase)

## husk_plugin_tests (JUCE)

Plugin identity, buses, latency reported to the host, mix-0 / bypass / host-bypass nulls, MIDI chords through processBlock, Steps with a host play head, every parameter exercised, state save/restore, presets, fuzz, CPU through processBlock, and editor screenshots (`shots/`).

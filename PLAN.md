# Voice resynth — plan for FL Studio

Status: **planning only.** Nothing is built, pushed, or installed until you pick a name and say go.

This is a VST3 *effect* you put on a vocal. It listens to the voice, breaks it into its tiny tones (partials), and rebuilds those tones as an instrument — a sine, a saw, a glass bell, a choir, and so on. Your words keep their rhythm and shape. The sound becomes that instrument. You can thin it down to one pure note that follows the melody, freeze a moment as a pad, or play MIDI chords and have the voice sing them as a synth choir.

It is inspired by the *job* of Circle’s RESYNTH (decompose a voice, rebuild it), but it is **not a copy**. The code, the name, the look, the control names, and the layout will all be our own. We will not load wavetables, we will not use their labels (Transpose, Shift, Loco, Smooth, Ceps/Synth), and we will not recreate their square pad-with-side-sliders layout, red Mid/Side circles, or pixel scope.

The owner's name must not appear in any repo file, comment, preset, credit, or install note.

Recommended name: **WISP**. Other name ideas are in section 11. The layout sketch below uses [NAME] as a placeholder.

---

## 1. What you do with it

1. Put the plugin on a vocal track in FL (a recording, or a live input).
2. Pick an instrument model: Sine, Saw, Square, Glass, Flute, Choir, or String.
3. Turn **Partials** to go from a full rebuilt voice down to one pure note that still follows the melody.
4. Use the simple **Pitch** knob (±12 semitones), the **Octave** switch, and **Formant** (chipmunk to giant) for quick character moves.
5. Shape texture with **Phase** and **Phase Rate**, and with the X-Y pad (**Water** / **Grain**) for watery or glitchy morphs.
6. Optionally turn on **Steps** (tempo-synced 1/4 to 1/32) so the voice updates in chops, or **Freeze** to hold the current sound as an endless pad.
7. Optionally turn on **Chord** and play MIDI notes: the voice sings those chords as a synth choir.
8. Finish with **Shimmer**, **Space**, **Crunch**, **Mix**, and **Out**.

It should sound usable with only a model, Partials, and Mix. Everything else is there when you want it.

---

## 2. The controls

### Voice as instrument

Seven built-in partial models. These are original formulas, not samples and not loaded wavetables.

- **Sine** — each partial is a pure tone. The classic rebuilt voice, or a whistle when Partials is turned down.
- **Saw** — richer and buzzy, like a synth lead singing your words.
- **Square** — hollow, clarinet-like, with the odd-numbered tones louder.
- **Glass** — the tones are slightly stretched out of tune with each other and ring, so the melody feels like struck glass.
- **Flute** — fewer, softer tones, with more breath.
- **Choir** — several slightly detuned copies of the same tones, like a small ensemble.
- **String** — brighter overtones, a little bow-noise, a touch of stretch.

### Partials

One big knob. All the way up, you hear the full rebuilt voice. Turn it down and weaker / higher tones drop away. At 1, only a single tone remains — a pure note that still follows the sung melody. This is the heart of the plugin.

### Air

A smaller knob for how much breath and consonants (the leftover “hiss and t, s, p”) to keep. Default keeps words readable. Turn it down for a fully synthetic instrument; turn it up to leave more of the human air in.

### Pitch and Formant (kept simple)

- **Pitch** — ±12 semitones.
- **Octave** — a three-way switch: −1, 0, +1. Together with Pitch, that covers a wide range without a fussy tuner.
- **Formant** — moves the “mouth size” from chipmunk to giant, independently of Pitch. Because the voice is already being rebuilt, this is cleaner than doing the same move in a separate plugin. Dedicated formant/pitch plugins remain the right tool for detailed tuning work; these two knobs are for character while you play with the sound.

### Phase distortion (its own feature, not hidden in Crunch)

Phase distortion scrambles how the tiny tones line up in time, while each one keeps its pitch.

- **Phase** — how much scramble. A little gives a hollow, phasey shimmer. More gives rough, grainy, broken-radio grit. All the way up, it becomes noisy, chaotic texture.
- **Phase Rate** — how often the scramble is refreshed. Free (slow to fast, in Hz) or tempo-synced to the same note values as Steps. Slow and free feels like an evolving shimmer. Synced to the beat gives rhythmic grit.

These are independent of Steps. Steps holds the *analysis* (the voice’s current shape). Phase Rate re-scrambles the *phase warp*.

### Stutter / Steps

A rate control, off by default. When on, the rebuilt voice only takes a new snapshot of the input on a tempo grid, and holds between those points — a chopped, stepping effect. Note values: **1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32**, locked to FL’s tempo. 1/4 is a slow, obvious step; 1/32 is tight and busy.

### X-Y pad (Water / Grain)

A pad for glitchy, watery morphs, with our own axes — not a phase-vs-cepstrum copy.

- **Water** (left → right) — loosens and smears the tones in time and pitch. The voice starts to swim, chorus, and blur.
- **Grain** (bottom → top) — makes the updates rougher and more broken. The voice starts to glitch, crumble, and stutter.

The pad is a finishing gesture on top of the knobs, not the only way to drive the plugin. It starts in the middle (neutral). Double-click returns it there.

### Freeze

A big, obvious button. It latches the current tones (and a snapshot of the air) and keeps playing them as an endless pad or drone, even after the vocal stops. Press again to release. Changes fade across so it does not click. Great for ambient beds. You can still move Mix, Space, Shimmer, Crunch, Phase, and the pad while frozen.

### Chord mode (optional MIDI)

A toggle, off by default. When it is on and you hold MIDI notes, the voice’s shape — its timing, mouth, and consonants — is sung on those notes as a synth choir. That is the vocoder feeling without the robot. If no MIDI is held, it behaves as usual and just follows the sung melody.

This is a VST3 effect with MIDI input. In FL you will need to route a keyboard or a piano-roll clip to the mixer track that holds the plugin (FL’s usual MIDI-to-effect path). A small light shows when MIDI is arriving. Chord mode is capped at a small number of notes (six) so CPU stays predictable.

### Shimmer, Space, Crunch, Mix, Out

- **Shimmer** — a bright, pitch-up sparkle in the tail, lightly mixed. Made for ambient and indie beds.
- **Space** — a built-in room, short to medium, so a sound can feel finished without a separate reverb. Not a huge hall (those smear words and add delay).
- **Crunch** — a simple saturator after the rebuild. One knob. This is dirt and warmth, not the same thing as Phase.
- **Mix** — dry voice to fully rebuilt. At 0, you hear the original vocal (with the plugin’s delay compensated so it stays in time).
- **Out** — output level, plus a quiet safety limiter that does nothing until the signal is too hot (chords + crunch can pile up).

A bypass control turns all processing off.

---

## 3. How it works (plain words, then a little more precise)

The engine is **sinusoidal / partial modelling** — a well-known, textbook way of taking a sound apart into a handful of sliding pure tones, plus a leftover noise layer. It is the right fit for this plugin: the Partials knob, the instrument models, Freeze, and Chord copies all fall out of it naturally.

A purely STFT / cepstral “paint the spectrum and freeze the envelope” approach (closer to what the inspirational plugin appears to lean on) is good for watery textures, but weaker at “keep only one partial as a pure lead” or “rebuild this as a glass bell.” We will still *use* a short spectrum analysis to find the tones, and a spectral envelope (cepstrum or LPC — a compact description of the mouth shape) for Formant. We will not make a cepstrum-gate clone.

### Step by step

1. **Find the sung note (pitch tracking).** A monophonic tracker in the YIN family watches the voice and reports the current pitch, and when there isn’t a clear note (breath, an “s”, silence). Octave jumps are a known risk; a simple Low / Mid / High voice-range hint can be tucked in if tests show we need it, without crowding the main panel.
2. **Find the tiny tones.** In each short slice of sound, it looks for peaks at multiples of that pitch, and records each tone’s frequency, loudness, and phase. Whatever is left after those tones are removed is the **residual**: breath and consonants.
3. **Measure the mouth (formants).** A separate spectral envelope tracks the vowel shape, so Pitch can move without dragging the mouth with it, and Formant can move the mouth on its own.
4. **Rebuild.** Each kept tone is an oscillator. Loudness and pitch slide smoothly from slice to slice so you do not hear zipper noise. The chosen instrument model changes the recipe: which overtones are loud, whether they sit in a perfect harmonic series or slightly stretched, how much air is in the residual, and whether extra slightly detuned copies are added (Choir).
5. **Hold, step, or chord.** Freeze latches the current recipe and keeps the oscillators running. Steps only refresh that recipe on the tempo grid. Chord mode starts one copy of the bank for each held MIDI note, all sharing the voice’s mouth and amplitude.
6. **Colour.** Phase warps the tones’ alignment; the pad smears or roughens them; then Shimmer, Space, Crunch, Mix, and the limiter.

No machine-learning model is trained or shipped. No paid cloud compute.

### Latency and CPU targets

Finding a low sung note reliably needs a short window of the voice. A low note around 100 Hz wants a couple of cycles, which is already in the 20 ms range. This will not be as tight as a live tuner.

- **Latency target:** keep added plugin latency at or under **about 25 ms**, measured at 44.1 kHz and 48 kHz, and report the real number. FL can compensate on playback, so already-recorded vocals stay in time. Singing through it live will feel a little late. We will not pretend this is a zero-latency monitoring plugin, and we will not add long linear-phase filters or a huge hall on the path.
- **CPU target:** under about **10–15% of one M-series core** for the default (one voice, up to around 64–128 partials, Shimmer and Space on at modest settings). Chord mode costs more, roughly in proportion to how many notes are held, which is why it is capped. The Partials knob itself is a CPU gift: fewer tones means less work.
- **Quality vs speed:** the default path is the only path in v1, unless tests show we must add a clearly labelled “Fewer partials” safety. We will not hide a high-latency quality mode as the default.

---

## 4. Look and feel

Original UI. High contrast, resizable, simple. Not a copy of the inspirational plugin, and not a copy of our other plugins.

**Colours, as requested:**

- **Background:** dark gray (not black, not blue, not beige).
- **Accent:** pastel purple. This is the only strong colour. Main controls — Partials, Freeze, the pad cursor, the selected instrument model, and the partials display — are pastel purple and must read clearly against the dark gray.
- **Everything else:** neutral grays. Labels, lesser knobs, and dividers stay quiet so the main controls pop.
- Light text on the dark gray, large enough to read. No low-contrast purple-on-gray labels. No neon, no rainbow, no blended colour washes on the controls.

The signature visual is a **partials display**: a row of thin glowing strands (one per audible tone) in pastel purple on the dark gray. As you turn Partials down, strands wink out until one remains. It is a read-out, not a second X-Y pad, and not a pixelated scope.

### Layout (described)

```
  [NAME]        Preset ▾                              A/B    BYPASS

  SINE   SAW   SQUARE   GLASS   FLUTE   CHOIR   STRING

  ┌ strands of glowing partials ──────────────────┐   ┌  WATER →    ┐
  │  ═ ═ ═ ═ ═ ═ ═ ═                              │   │      •      │
  │                                               │   │  GRAIN  ↑   │
  └───────────────────────────────────────────────┘   └─────────────┘

  PARTIALS      PITCH     OCTAVE     FORMANT      PHASE     PHASE RATE     STEPS
    ( O )        ( O )    [-1  0  +1]   ( O )       ( O )     ( O )  free/sync   1/16 ▾

  FREEZE     CHORD (MIDI)      AIR      SHIMMER     SPACE      CRUNCH      MIX      OUT
   [  ]         [  ]           ( O )      ( O )       ( O )       ( O )      ( O )    ( O )
```

Top: name, presets, A/B, bypass, then the model row (one selected at a time, pastel purple highlight).

Middle: the partials strands on the left, the Water / Grain pad on the right. The pad is a soft rounded square, clearly labelled, not the centre of a slider-ringed square.

Lower: the big performance knobs in one row, with Partials the most prominent, then the buttons and finishing knobs along the bottom. Freeze and Chord are obvious. Steps and Phase Rate show their sync state next to the control.

The window scales cleanly. Empty / silent state still shows the dark gray panel and quiet strands, not a blank hole.

---

## 5. Presets

Starting points for alternative, indie, ambient, Fred again..-style, and electronic-folk vocals. Names are ours. They cannot promise to sound like a specific record.

- **Start Here** — default. Sine, full partials, Air on enough to keep words, pad centred, Steps off, Freeze off, Chord off, no extra colour. A clean place to begin.
- **Pure Lead** — Partials down to one or two, sine or flute, a finished monophonic hook that follows the melody.
- **Glass Room** — Glass model, modest Space, gentle Shimmer. Words become a ringing glass line.
- **Soft Choir** — Choir model, a little Space, Air still on. Ready for Chord mode.
- **Folk Reed** — Flute, warm Formant, some Air, light Crunch. Electronic-folk vocal-as-woodwind.
- **String Bed** — String, Shimmer and Space up. A sung pad.
- **Stepped Hook** — Steps at 1/16, a little Phase, short Space. Chopped, excited vocal hook.
- **Night Bus** — Steps at 1/8 or 1/16, light Crunch, short Space. Late, punched-up vocal texture.
- **Water Pad** — pad toward Water, Shimmer and Space up, Freeze-friendly. Ambient bed.
- **Broken Radio** — Phase and Crunch up, some Grain. Grainy, hollow, slightly destroyed.
- **One Note Glow** — Partials at 1, sine, a little Shimmer. A pure sung tone with a halo.

Presets are starting points. A/B compares against the unedited incoming voice (Mix at 0), so you can tell if the preset actually helped.

---

## 6. What v1 will not include

Left out on purpose, to keep the layout simple and the identity ours:

- Loading custom wavetables or user oscillators.
- Mid/side processing.
- A dual-LFO modulation matrix (Steps, Phase Rate, and the pad already cover rhythmic and textural motion).
- A pitch-graph editor, or ARA (FL does not support ARA).
- Auto-tune or scale snapping (that is a different plugin).
- A neural vocoder or any large shipped model.
- AU format (VST3 only, same as the other plugins).
- Anything installed on your Mac, or a public repo, without your OK.

---

## 7. How it is tested

Automated tests run on the engine (no plugin window needed) and again in the plugin, the same way as the other instruments. pluginval at strictness 10 on the Mac build, if CI can run. Every control is exercised. Listening in FL is still the real test.

### Pitch and partials, against textbook math

- **Pitch tracking:** sines and simple vowel-like tones at known MIDI notes and cents offsets. Reported pitch must be within a tight cents budget (we will publish the worst-case error). Silence, breath-like noise, and unpitched consonants must be flagged as unpitched rather than inventing a note.
- **Partial analysis:** a built harmonic series with known amplitudes (for example 1, 1/2, 1/3, … 1/N) is recovered at the right frequencies (± a few cents) and relative levels. Partials = 1 keeps only the fundamental; discarded partials fall below a set energy threshold. Partials = K keeps K tones, in the documented order (strongest, or lowest first — decided in the build and tested as such).
- **Pitch shift:** every kept partial moves by the ratio 2^(semitones/12), within a few cents. Length of a test tone is unchanged. The **Octave** switch is an exact ×2 or ×0.5.
- **Formant:** a vowel-like tone with known envelope peaks. Moving Pitch with Formant at centre leaves those peaks in place (within a tight percent). Moving Formant shifts the envelope peaks by the expected ratio, while the fundamental stays put if Pitch is unchanged.

### Models, freeze, chords, steps, phase

- **Models:** Sine has energy only at the harmonic frequencies. Saw’s harmonic falloff is close to 1/n. Square’s even harmonics are well below its odd ones. Glass’s partials sit on a stretched (inharmonic) series, not a pure harmonic one. Choir produces measurable beating / detune between close copies. Flute and String change residual / brightness in the documented direction.
- **Freeze:** after Freeze, the output keeps stable partial frequencies even when the input goes silent or changes pitch; releasing Freeze follows the input again. No click above a small transient budget at the moment of freeze or release.
- **Chord mode:** a sung (or synthetic vowel) tone at a known pitch, with MIDI notes C3–E3–G3 held, produces spectral peaks at those chord tones. Amplitude still follows the voice envelope. With Chord off, or with no MIDI, output stays monophonic. A seventh held note is ignored (the cap).
- **Steps:** at a known host tempo, the analysis recipe only changes on the grid (for example 1/16 at 120 BPM is every 125 ms). Between updates, parameters hold. Off means the recipe tracks continuously.
- **Phase:** at Phase = 0, a known harmonic test tone keeps its pitch and does not pick up the grit. Raising Phase increases a measured phase-deviation or distortion figure without changing the measured fundamental. Phase Rate at a known free frequency re-scrambles on that period; a synced rate follows host tempo the same way Steps does.

### Pad, finishes, mix, safety

- **X-Y pad:** centre is the neutral recipe. Water increases inter-partial frequency wander / smear. Grain increases stepwise or roughness figures. Double-click returns to centre and to the neutral measurements.
- **Air:** at 0, residual energy is at a floor; raising it restores residual on a test signal that has a known noise band.
- **Shimmer:** added energy appears near an octave above the dry resynth, and a tail remains after the input stops.
- **Space:** measured tail / decay time grows with the knob; off (or zero) does not leave an audible hall.
- **Crunch:** harmonic distortion (THD) rises with the knob on a sine or vowel test; at zero, it does not change the pre-crunch signal.
- **Mix:** 0 matches the latency-compensated dry input; 100 is wet only.
- **Limiter:** a hot input (and a hot Chord + Crunch case) does not explode or produce infinities; a modest signal is not coloured.
- **Latency:** declared and measured added delay at 44.1 and 48 kHz must stay within the 25 ms budget, or the test fails and we revisit the window.
- **State:** settings survive save and restore. A/B and bypass behave as documented. No crashes in a fuzz of random parameter automation. No audio-thread allocations, no denormals.
- **UI screenshots** of the silent state, a full-partials vowel, Partials at 1, Freeze on, and Chord mode with MIDI.

---

## 8. Risks and limits (the honest part)

- **Latency.** This needs a short analysis window, so it will add on the order of 20–25 ms. Fine on recorded vocals once FL compensates. Not as immediate as a live tuner if you sing through it.
- **Breathy, whispered, fry, or noisy vocals.** Pitch tracking gets shaky when there is no clear note. Partials can flutter or drop out, and the residual can sound like a hissy or slightly metallic ghost of the words. Air helps, but it cannot invent a solid pitch that was never there. Stacked, heavily distorted, or already-reverbed vocals are harder still.
- **Consonants.** “S,” “t,” and “p” live in the residual, not in the pretty tones. If Air is low, words can turn into a mush of vowels. That is sometimes the sound you want (Pure Lead, One Note Glow); it is a problem if you expected every lyric to stay clear.
- **Octave errors.** Trackers sometimes jump an octave on low or mid voices. We will test for this and add a quiet range hint if needed. It may still happen on awkward takes.
- **Chord mode** can get dense, phasey, or loud, and CPU goes up with each held note. The cap and the limiter are there for that. It is not a full choir arranger.
- **Freeze** can feel static on a long pad if nothing else is moving. Phase, the pad, Shimmer, and Space are the ways to keep a frozen tone alive.
- **Steps at 1/4** will feel laggy or obvious; that is the musical point of a slow grid, not a bug, but it can make a vocal feel late against the beat.
- **Phase and Grain at the top** will make words hard to understand. Defaults stay low.
- **Formant and Pitch** are character controls. They will not replace a dedicated tuning or formant plugin for precise corrective work.
- **FL MIDI-to-effect routing** is a little fussy. Chord mode depends on it. We will document the setup clearly; we cannot make FL’s mixer MIDI path more obvious than it is.
- **Private-repo GitHub Actions** has been blocked by billing before. If CI is still blocked when we build, the Mac zip will not be produced until that is resolved, without making the repo public unless you ask.
- None of this is verified in FL Studio until you open it and put it on your voice.

---

## 9. Rough size and build

- The Mac zip should stay in the same range as the other effects, on the order of **5–15 MB**. There is no neural model and no sample library; the instrument models are formulas.
- JUCE + CMake, C++, macOS 11+, universal arm64 + x86_64, company koobacorp, VST3 audio effect with MIDI input. Ad-hoc signed, same path as the other plugins.
- Repo (only after you say go) stays private unless you decide otherwise. The owner's name must not appear in any file in that repo.
- Engine tests, plugin tests, and pluginval on the Mac build if CI can run.
- Not installed on your Mac until you ask.

---

## 10. What “done” means

Before calling v1 done: the engine tests above pass, plugin tests and pluginval pass, every control has a test, latency and CPU are measured and written down, and the UI matches the dark gray / pastel purple brief without overlapping text.

What only you can confirm: how it feels on your real voice in FL, whether the models are the character you want, and whether the presets help on your kind of records.

---

## 11. Name ideas

Five original names. None of these is RESYNTH, and none is the name of a well-known existing VST of the same job. You can pick one, or suggest another.

- **WISP** (recommended) — a thin thread of smoke or light. The voice thinned down to a glowing tone, or held as a floating pad. Short, easy to spot in a plugin list, and close in spirit to names like CRUMBS or HONE. (There is a hardware MIDI controller called “The Wisp,” but no well-known VST of this name.)
- **HUSK** — the shell of the voice, taken apart and rebuilt as something new. Short and tactile.
- **LITANY** — a repeated sung prayer. Fits Choir, Freeze, Steps, and the folk / ambient side.
- **APSE** — the quiet, arched end of a church, where a voice blooms into space. Unusual, and it suits Shimmer, Space, and Soft Choir.
- **LARK** — a clear singing bird. Fits Pure Lead, the “one note following the melody” idea, and electronic-folk.

Not used: RESYNTH or any play on Circle’s name; VEIL (already a vocal de-esser); LOOM (already an additive synth); HALO, CANTOR, FILAMENT, VESPER, IOTA, HUSH, MOTE, and MURMUR (already used by other audio products).

Pick a name, say go, and then it can be built.

// HUSK v2 factory presets (our own names). Each preset starts from the defaults and changes a few
// controls. Values are real (unnormalised) parameter values keyed by parameter ID.
#pragma once
#include <utility>

namespace husk {
struct PresetValue { const char* id; float value; };
struct Preset { const char* name; PresetValue v[14]; int n; };

// Models: 0 Saw Lead, 1 Reed, 2 Glass, 3 Bowed String, 4 Choir, 5 Organ, 6 Pluck, 7 Sub, 8 Ghost.
// Steps: 0 Off, 1 1/4, 2 1/8, 3 1/8T, 4 1/16, 5 1/16T, 6 1/32. Scale: 0 Chromatic, 1 Major, 2 Minor, 3 Pent, 4 Minor Pent.
inline const Preset* presets(int& count) {
    static const Preset p[] = {
        { "Synth Lead",   { }, 0 },
        { "Talkbox",      { { "model", 1 }, { "talk", 1.0f }, { "unison", 1 }, { "partials", 28 }, { "crunch", 0.3f }, { "drift", 0.1f } }, 6 },
        { "Glass Choir",  { { "model", 2 }, { "unison", 3 }, { "detune", 0.25f }, { "shimmer", 0.45f }, { "space", 0.45f }, { "snap", 1 }, { "glide", 40 } }, 7 },
        { "Bowed Pad",    { { "model", 3 }, { "unison", 5 }, { "detune", 0.5f }, { "width", 1.0f }, { "glide", 120 }, { "space", 0.5f }, { "partials", 22 } }, 7 },
        { "Organ Hymn",   { { "model", 5 }, { "unison", 2 }, { "detune", 0.15f }, { "snap", 1 }, { "scale", 1 }, { "glide", 30 }, { "space", 0.55f }, { "talk", 0.55f } }, 8 },
        { "Pluck Chops",  { { "model", 6 }, { "unison", 2 }, { "steps", 4 }, { "space", 0.25f }, { "snap", 1 } }, 5 },
        { "Sub Voice",    { { "model", 7 }, { "octave", 0 }, { "unison", 1 }, { "partials", 8 }, { "crunch", 0.25f }, { "talk", 0.5f } }, 6 },
        { "Ghost",        { { "model", 8 }, { "unison", 1 }, { "space", 0.2f }, { "air", 0.3f } }, 4 },
        { "Frozen Halo",  { { "model", 4 }, { "unison", 6 }, { "octave", 1 }, { "shimmer", 0.85f }, { "space", 0.85f }, { "glide", 300 }, { "snap", 1 }, { "partials", 18 } }, 8 },
        { "Night Bus",    { { "model", 1 }, { "octave", -1 }, { "steps", 2 }, { "crunch", 0.5f }, { "unison", 2 }, { "space", 0.15f } }, 6 },
        { "Broken Radio", { { "phase", 1.0f }, { "phase_rate", 9.0f }, { "crunch", 0.9f }, { "partials", 7 }, { "unison", 1 }, { "air", 0.5f }, { "steps", 6 } }, 7 },
    };
    count = (int)(sizeof(p) / sizeof(p[0]));
    return p;
}
} // namespace husk

// HUSK factory presets (our own names). Each preset starts from the defaults and changes a few
// controls. Values are real (unnormalised) parameter values keyed by parameter ID.
#pragma once
#include <utility>

namespace husk {
struct PresetValue { const char* id; float value; };
struct Preset { const char* name; PresetValue v[10]; int n; };

// Model indices: 0 Sine, 1 Saw, 2 Square, 3 Glass, 4 Flute, 5 Choir, 6 String.
// Steps: 0 Off, 1 1/4, 2 1/8, 3 1/8T, 4 1/16, 5 1/16T, 6 1/32.
inline const Preset* presets(int& count) {
    static const Preset p[] = {
        { "Start Here",    { }, 0 },
        { "Pure Lead",     { { "partials", 2 }, { "air", 0.25f }, { "space", 0.15f } }, 3 },
        { "Glass Room",    { { "model", 3 }, { "partials", 40 }, { "space", 0.35f }, { "shimmer", 0.25f }, { "air", 0.35f } }, 5 },
        { "Soft Choir",    { { "model", 5 }, { "partials", 48 }, { "space", 0.25f }, { "air", 0.45f } }, 4 },
        { "Folk Reed",     { { "model", 4 }, { "formant", -2.0f }, { "air", 0.6f }, { "crunch", 0.2f } }, 4 },
        { "String Bed",    { { "model", 6 }, { "shimmer", 0.45f }, { "space", 0.55f }, { "air", 0.4f } }, 4 },
        { "Stepped Hook",  { { "partials", 32 }, { "steps", 4 }, { "phase", 0.15f }, { "space", 0.15f } }, 4 },
        { "Night Bus",     { { "model", 1 }, { "steps", 2 }, { "crunch", 0.3f }, { "space", 0.12f }, { "air", 0.4f } }, 5 },
        { "Water Pad",     { { "water", 0.7f }, { "shimmer", 0.5f }, { "space", 0.6f }, { "air", 0.3f } }, 4 },
        { "Broken Radio",  { { "model", 2 }, { "phase", 0.6f }, { "crunch", 0.55f }, { "grain", 0.45f }, { "air", 0.5f } }, 5 },
        { "One Note Glow", { { "partials", 1 }, { "shimmer", 0.3f }, { "space", 0.2f }, { "air", 0.1f } }, 4 },
    };
    count = (int)(sizeof(p) / sizeof(p[0]));
    return p;
}
} // namespace husk

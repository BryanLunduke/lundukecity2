// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cmath>

// Stick and D-pad timing shared by the shell and the headless checks.
// A fresh press always takes one step. Repeats wait, then fire on a
// steady interval. A new press never inherits a clock that is already due.

namespace pad_feel {

inline constexpr int kRepeatDelayMs = 300;
inline constexpr int kRepeatIntervalMs = 95;
inline constexpr int kSpeedHoldMs = 500;
inline constexpr float kStickDead = 0.18f;

struct RepeatClock {
    int remaining_ms = 0;
    bool held = false;
};

inline void repeat_arm(RepeatClock &clock)
{
    clock.held = true;
    clock.remaining_ms = kRepeatDelayMs;
}

inline void repeat_clear(RepeatClock &clock)
{
    clock.held = false;
    clock.remaining_ms = 0;
}

// Steps owed by a hold that was already armed (the edge step is separate).
// A long frame can owe more than one step. Four is the cap so a hitch
// cannot paint a road across the map.
inline int repeat_tick(RepeatClock &clock, int elapsed_ms)
{
    if (!clock.held) {
        return 0;
    }
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    clock.remaining_ms -= elapsed_ms;
    if (clock.remaining_ms > 0) {
        return 0;
    }
    int steps = 0;
    while (clock.remaining_ms <= 0 && steps < 4) {
        ++steps;
        clock.remaining_ms += kRepeatIntervalMs;
    }
    return steps;
}

// Radial dead zone, rescaled to 0..1, then squared so the centre is fine
// and full deflection is still full speed.
inline void stick_axes(float x, float y, float &out_x, float &out_y)
{
    const float mag = std::sqrt(x * x + y * y);
    if (!(mag > kStickDead)) {
        out_x = 0.0f;
        out_y = 0.0f;
        return;
    }
    float scaled = (mag - kStickDead) / (1.0f - kStickDead);
    if (scaled > 1.0f) {
        scaled = 1.0f;
    }
    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    scaled = scaled * scaled;
    const float gain = scaled / mag;
    out_x = x * gain;
    out_y = y * gain;
}

} // namespace pad_feel

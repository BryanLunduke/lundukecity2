// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
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

// A trigger has to be pulled at least this much before it takes over from the mouse
// or trackpad.
inline constexpr float kTriggerWake = 0.5f;

// Whether an axis event may switch the game to the controller: a stick
// pair past the dead zone, or a trigger past kTriggerWake. Noise from a
// resting stick, a light touch or a drifting pad does not hide the pointer.
inline bool axis_wakes(bool trigger, float value, float pair)
{
    if (trigger) {
        return value >= kTriggerWake;
    }
    return std::sqrt(value * value + pair * pair) > kStickDead;
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

// The left stick as a D-pad in windows and menus (Deck hardware notes,
// Nov 25). A push past kStickNavOn counts as a press in the stronger
// axis's direction; it stays held until that axis falls under
// kStickNavOff, so a stick resting near the edge does not chatter. Raw
// values: x right positive, y down positive, both -1..1.
enum class StickDir { None, Up, Down, Left, Right };

inline constexpr float kStickNavOn = 0.5f;
inline constexpr float kStickNavOff = 0.3f;

inline StickDir stick_nav_dir(float x, float y, StickDir held)
{
    switch (held) {
    case StickDir::Up:
        if (-y >= kStickNavOff && -y >= std::fabs(x)) {
            return held;
        }
        break;
    case StickDir::Down:
        if (y >= kStickNavOff && y >= std::fabs(x)) {
            return held;
        }
        break;
    case StickDir::Left:
        if (-x >= kStickNavOff && -x >= std::fabs(y)) {
            return held;
        }
        break;
    case StickDir::Right:
        if (x >= kStickNavOff && x >= std::fabs(y)) {
            return held;
        }
        break;
    case StickDir::None:
        break;
    }
    const float ax = std::fabs(x);
    const float ay = std::fabs(y);
    if (ax < kStickNavOn && ay < kStickNavOn) {
        return StickDir::None;
    }
    if (ay >= ax) {
        return y < 0.0f ? StickDir::Up : StickDir::Down;
    }
    return x < 0.0f ? StickDir::Left : StickDir::Right;
}

} // namespace pad_feel

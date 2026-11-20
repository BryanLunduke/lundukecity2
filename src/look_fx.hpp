// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Small, pure helpers for the look-and-feel effects: the Effects setting,
// easing curves, the 4x4 ordered dither the Classic look uses in place of
// alpha, and a stable per-tile hash for flicker and puff phases.

#include <cmath>
#include <cstdint>
#include <string>

namespace look_fx {

// Pi spelled out: MinGW <cmath> has no M_PI without _USE_MATH_DEFINES.
constexpr double kPi = 3.14159265358979323846;

// Settings > Effects. Full is everything. Calm drops the ambient motion
// that never stops (drifting cloud shadows, water shimmer). Off is the
// reduced-motion choice: no animation at all, static effects only.
enum Effects : int { EffectsFull = 0, EffectsCalm = 1, EffectsOff = 2, EffectsCount = 3 };

inline int clamp_effects(int value)
{
    return value < EffectsFull || value >= EffectsCount ? EffectsFull : value;
}

inline const char *effects_label(int value)
{
    switch (clamp_effects(value)) {
    case EffectsCalm:
        return "Calm";
    case EffectsOff:
        return "Off";
    default:
        return "Full";
    }
}

inline bool ambient_allowed(int effects)
{
    return clamp_effects(effects) == EffectsFull;
}

inline bool motion_allowed(int effects)
{
    return clamp_effects(effects) != EffectsOff;
}

// Cloud shadows drift with the city speed: Slow 0.75x, Medium (the normal
// pace) 1x, Fast 2x. Paused holds them still. Only the cloud offset uses this;
// the shared ambient clock (water shimmer, smoke) keeps its own pace.
inline double cloud_speed_factor(int speed)
{
    if (speed <= 0) {
        return 0.0;
    }
    if (speed == 1) {
        return 0.75;
    }
    return speed == 2 ? 1.0 : 2.0;
}

inline double clamp01(double t)
{
    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

inline double ease_out_cubic(double t)
{
    t = 1.0 - clamp01(t);
    return 1.0 - t * t * t;
}

inline double ease_in_out(double t)
{
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}

// Rank 0..15 of a pixel in the 4x4 Bayer matrix.
inline int bayer_rank(int x, int y)
{
    static const int kBayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };
    return kBayer[y & 3][x & 3];
}

// True when an ordered dither of the given coverage (0..1) sets this pixel.
inline bool dither_on(int x, int y, double coverage)
{
    return bayer_rank(x, y) < static_cast<int>(coverage * 16.0 + 0.5);
}

inline std::uint32_t hash32(std::uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

inline double hash01(std::uint32_t v)
{
    return static_cast<double>(hash32(v) & 0xffffU) / 65535.0;
}

// The text with every figure scaled by t (0..1), for a count-up: "$1,240"
// at t = 0.5 reads "$620". Commas are kept where the figure had them, and a
// bare four-digit year (1900..2199) is left alone. t >= 1 returns the text.
inline std::string count_up(const std::string &text, double t)
{
    if (t >= 1.0) {
        return text;
    }
    if (t < 0.0) {
        t = 0.0;
    }
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            out.push_back(c);
            ++i;
            continue;
        }
        std::size_t j = i;
        unsigned long long value = 0;
        bool commas = false;
        int digits = 0;
        while (j < text.size()) {
            if (text[j] >= '0' && text[j] <= '9') {
                value = value * 10ULL + static_cast<unsigned long long>(text[j] - '0');
                ++digits;
                ++j;
            } else if (text[j] == ',' && j + 1 < text.size() && text[j + 1] >= '0' && text[j + 1] <= '9') {
                commas = true;
                ++j;
            } else {
                break;
            }
        }
        if (!commas && digits == 4 && value >= 1900 && value <= 2199) {
            out.append(text, i, j - i);
            i = j;
            continue;
        }
        const unsigned long long shown = static_cast<unsigned long long>(std::llround(static_cast<double>(value) * t));
        const std::string plain = std::to_string(shown);
        if (commas) {
            for (std::size_t k = 0; k < plain.size(); ++k) {
                if (k > 0 && (plain.size() - k) % 3 == 0) {
                    out.push_back(',');
                }
                out.push_back(plain[k]);
            }
        } else {
            out += plain;
        }
        i = j;
    }
    return out;
}

} // namespace look_fx

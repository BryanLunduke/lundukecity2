// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The Funds readout rolls to a new balance instead of jumping, and the
// change floats away beside it: red and falling for spending, green and
// rising for income, with a short gold glint when income lands. Only the
// display moves; the treasury is never touched.

#include "look_fx.hpp"

#include <cstdint>
#include <vector>

struct FundsTicker {
    static constexpr int kRollMs = 360;
    static constexpr int kFloatMs = 1100;
    static constexpr int kGlintMs = 320;
    // A change this soon after one of the same sign joins its float, so a
    // road drag reads as one growing "-$" figure instead of a stack.
    static constexpr int kMergeMs = 450;
    static constexpr std::size_t kMaxFloats = 3;

    struct Float {
        std::int64_t delta = 0;
        int ms = 0;
    };

    std::int64_t from = 0;
    std::int64_t target = 0;
    int roll_ms = -1;
    int glint_ms = -1;
    bool known = false;
    unsigned generation = 0;
    std::vector<Float> floats;

    void snap(std::int64_t funds)
    {
        from = funds;
        target = funds;
        roll_ms = -1;
        glint_ms = -1;
        floats.clear();
    }

    // Call every frame with the treasury. A new city (generation) or
    // animate == false shows the balance at once.
    void observe(std::int64_t funds, unsigned city_generation, bool animate)
    {
        if (!known || generation != city_generation || !animate) {
            snap(funds);
            known = true;
            generation = city_generation;
            return;
        }
        if (funds == target) {
            return;
        }
        const std::int64_t delta = funds - target;
        from = shown();
        target = funds;
        roll_ms = 0;
        glint_ms = -1;
        if (!floats.empty() && floats.back().ms < kMergeMs && (floats.back().delta > 0) == (delta > 0)) {
            floats.back().delta += delta;
            floats.back().ms = floats.back().ms < 120 ? floats.back().ms : 120;
        } else {
            floats.push_back(Float{delta, 0});
            if (floats.size() > kMaxFloats) {
                floats.erase(floats.begin());
            }
        }
    }

    void advance(int ms)
    {
        if (ms < 0) {
            ms = 0;
        }
        if (roll_ms >= 0) {
            roll_ms += ms;
            if (roll_ms >= kRollMs) {
                roll_ms = -1;
                if (target > from) {
                    glint_ms = 0;
                }
                from = target;
            }
        } else if (glint_ms >= 0) {
            glint_ms += ms;
            if (glint_ms >= kGlintMs) {
                glint_ms = -1;
            }
        }
        for (Float &one : floats) {
            one.ms += ms;
        }
        while (!floats.empty() && floats.front().ms >= kFloatMs) {
            floats.erase(floats.begin());
        }
    }

    std::int64_t shown() const
    {
        if (roll_ms < 0) {
            return target;
        }
        const double t = look_fx::ease_out_cubic(static_cast<double>(roll_ms) / kRollMs);
        return from + static_cast<std::int64_t>(static_cast<double>(target - from) * t);
    }

    // -1 rolling down, 1 rolling up, 0 at rest.
    int trend() const
    {
        if (roll_ms < 0 || target == from) {
            return 0;
        }
        return target > from ? 1 : -1;
    }

    // 1 at the start of the glint, fading to 0.
    double glint() const
    {
        return glint_ms < 0 ? 0.0 : 1.0 - static_cast<double>(glint_ms) / kGlintMs;
    }

    bool active() const
    {
        return roll_ms >= 0 || glint_ms >= 0 || !floats.empty();
    }
};

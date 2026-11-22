// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The demand meters ease to a new reading and flash when it changes. Only
// the display moves; the simulator's demand values are never touched.

#include "look_fx.hpp"

#include <algorithm>
#include <cmath>

struct DemandTicker {
    static constexpr int kPulseMs = 320;
    static constexpr double kStep = 0.02;

    double from[3] = {};
    double target[3] = {};
    int pulse_ms[3] = {-1, -1, -1};
    bool known = false;
    unsigned generation = 0;

    void snap(double res, double com, double ind)
    {
        const double values[3] = {res, com, ind};
        for (int i = 0; i < 3; ++i) {
            from[i] = values[i];
            target[i] = values[i];
            pulse_ms[i] = -1;
        }
    }

    void observe(double res, double com, double ind, unsigned city_generation, bool animate)
    {
        if (!known || generation != city_generation || !animate) {
            snap(res, com, ind);
            known = true;
            generation = city_generation;
            return;
        }
        const double values[3] = {res, com, ind};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(values[i] - target[i]) < kStep) {
                continue;
            }
            from[i] = shown(i);
            target[i] = values[i];
            pulse_ms[i] = 0;
        }
    }

    void advance(int ms)
    {
        if (ms < 0) {
            ms = 0;
        }
        for (int i = 0; i < 3; ++i) {
            if (pulse_ms[i] < 0) {
                continue;
            }
            pulse_ms[i] += ms;
            if (pulse_ms[i] >= kPulseMs) {
                pulse_ms[i] = -1;
                from[i] = target[i];
            }
        }
    }

    double shown(int i) const
    {
        if (i < 0 || i > 2) {
            return 0.0;
        }
        if (pulse_ms[i] < 0) {
            return target[i];
        }
        const double t = look_fx::ease_out_cubic(static_cast<double>(pulse_ms[i]) / kPulseMs);
        return from[i] + (target[i] - from[i]) * t;
    }

    // 1 at the start of a change, fading to 0.
    double pulse(int i) const
    {
        if (i < 0 || i > 2 || pulse_ms[i] < 0) {
            return 0.0;
        }
        return 1.0 - static_cast<double>(pulse_ms[i]) / kPulseMs;
    }

    int trend(int i) const
    {
        if (i < 0 || i > 2 || pulse_ms[i] < 0) {
            return 0;
        }
        if (target[i] > from[i] + kStep) {
            return 1;
        }
        if (target[i] < from[i] - kStep) {
            return -1;
        }
        return 0;
    }

    bool active() const
    {
        return pulse_ms[0] >= 0 || pulse_ms[1] >= 0 || pulse_ms[2] >= 0;
    }
};

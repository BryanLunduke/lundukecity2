// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Fast speed (G10).
//
// The shell used to call CitySession::tick once every 100ms and, on a slow
// frame, throw away any time past 100ms. Each tick is one simulator phase.
// Speed 3 simulates on every phase, and a month is 16 phases times 4
// cityTime steps, so Fast was about 6.4 seconds per month (roughly three
// months in 20 seconds). That is the engine's own phase clock, not a
// rendering stall: a profile of the Fast path spends the time inside
// simTick, and dropping frames made it slower still.
//
// The shell now keeps up to half a second of elapsed time and, at Fast,
// runs four phases per 100ms. That is about 40 phases a second, 1.6 seconds
// per month, and about 20 seconds per year. Pause, Slow, and Medium stay at
// one phase per 100ms. CitySession::tick itself is still one phase, so
// tests that call tick() directly keep the old step.

inline int sim_bursts_for_speed(int speed)
{
    if (speed >= 3) {
        return 4;
    }
    return 1;
}

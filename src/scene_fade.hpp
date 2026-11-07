// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// A 140 ms, three-step ordered dither. Pixels are replaced with one VGA
// grey. Nothing is blended, so SVGA and Spooky stay on the palette.

struct SceneFade {
    static constexpr int kTotalMs = 140;
    // -1 is idle. 0, 1, and 2 are the three steps.
    int step = -1;
    int elapsed_ms = 0;
    bool hold = false;

    void start()
    {
        step = 0;
        elapsed_ms = 0;
        hold = false;
    }

    void skip()
    {
        step = -1;
        elapsed_ms = 0;
        hold = false;
    }

    bool active() const
    {
        return step >= 0;
    }

    void advance(int ms)
    {
        if (step < 0 || hold) {
            return;
        }
        if (ms < 0) {
            ms = 0;
        }
        elapsed_ms += ms;
        if (elapsed_ms >= kTotalMs) {
            skip();
            return;
        }
        step = elapsed_ms * 3 / kTotalMs;
        if (step > 2) {
            step = 2;
        }
    }

    // True when this pixel should be replaced for the current step.
    bool darken(int x, int y) const
    {
        if (step < 0) {
            return false;
        }
        static const int bayer[4][4] = {
            {0, 8, 2, 10},
            {12, 4, 14, 6},
            {3, 11, 1, 9},
            {15, 7, 13, 5},
        };
        const int rank = bayer[y & 3][x & 3];
        const int gate = 4 + step * 4;
        return rank >= gate;
    }
};

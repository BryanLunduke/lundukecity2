// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// One color for each history series, in legend order. Light colors are for a
// light theme and dark colors for a dark theme. Hues stay apart (residential
// green, commercial blue, industrial amber, cash flow magenta, crime red,
// pollution brown). Dash lengths are in pixels; 0, 0 is a solid line. Each
// series has its own dash so a pair that collapses under a color-vision
// deficiency can still be matched to its swatch.
struct GraphSeriesStyle {
    const char *name;
    int light_r;
    int light_g;
    int light_b;
    int dark_r;
    int dark_g;
    int dark_b;
    double dash_on;
    double dash_off;
};

inline constexpr int kGraphSeriesCount = 7;

inline constexpr GraphSeriesStyle kGraphSeries[kGraphSeriesCount] = {
    {"Population", 32, 32, 32, 245, 245, 245, 0.0, 0.0},
    {"Residential", 0, 142, 72, 56, 214, 136, 12.0, 5.0},
    {"Commercial", 16, 92, 204, 88, 164, 255, 2.0, 3.0},
    {"Industrial", 216, 122, 0, 255, 178, 40, 7.0, 4.0},
    {"Cash flow", 156, 48, 168, 232, 128, 224, 4.0, 3.0},
    {"Crime", 198, 36, 52, 255, 96, 104, 5.0, 2.0},
    // The light brown stays the one that clears the palette distance test.
    // The dash is longer than the old 2-on-4-off spec so a flat pollution
    // history reads as a line instead of a dotted black midline.
    {"Pollution", 112, 84, 60, 160, 148, 124, 8.0, 3.0},
};

inline double graph_channel(int value)
{
    return static_cast<double>(value) / 255.0;
}

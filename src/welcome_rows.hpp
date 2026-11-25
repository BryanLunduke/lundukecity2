// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The launch screen's plain choices (under the Continue card, when there
// is a last city). Pure data, so a test can check it without SDL.
// Release nov24 #7: Learn to Play shares the first row with New city.

#include "hotspots.hpp"

#include <array>

struct WelcomeCell {
    const char *label;
    int id;
};

struct WelcomeRow {
    WelcomeCell first;
    WelcomeCell second; // id 0: the row has one full-width button
};

inline constexpr int kWelcomeRowCount = 5;

inline const std::array<WelcomeRow, kWelcomeRowCount> &welcome_rows()
{
    static const std::array<WelcomeRow, kWelcomeRowCount> rows = {{
        {{"_Start a new city", kWelcomeNew}, {"Lea_rn to Play", kWelcomeLearn}},
        {{"_Load a saved city", kWelcomeLoad}, {nullptr, 0}},
        {{"Select a _scenario", kWelcomeScenario}, {nullptr, 0}},
        {{"How to _play", kWelcomeHowTo}, {nullptr, 0}},
        {{"_Quit", kWelcomeQuit}, {nullptr, 0}},
    }};
    return rows;
}

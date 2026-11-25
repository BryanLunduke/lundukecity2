// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The launch screen's plain choices (under the Continue card, when there
// is a last city). Pure data, so a test can check it without SDL.
// Release nov24 #7: Learn to Play shares the first row with New city.
// Release nov24 #16: Settings shares the How to play row.

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
        {{"How to _play", kWelcomeHowTo}, {"Se_ttings", kWelcomeSettings}},
        {{"_Quit", kWelcomeQuit}, {nullptr, 0}},
    }};
    return rows;
}

// Where a D-pad or stick press moves the Welcome focus (Deck hardware
// notes, Nov 25). Up and Down change rows, Left and Right stay in the row,
// so Learn to Play is right of Start a new city and Settings right of How
// to play. A one-button row is reached from either column, and leaving it
// lands on the left button. The Continue card, when there is a last city,
// is a row of its own above the others. Nothing past an edge: the same id.
inline int welcome_step(int id, int dx, int dy, bool has_continue)
{
    const auto &rows = welcome_rows();
    int row = -2;
    int col = 0;
    if (has_continue && id == kWelcomeContinue) {
        row = -1;
    }
    for (int r = 0; r < kWelcomeRowCount && row == -2; ++r) {
        if (rows[static_cast<std::size_t>(r)].first.id == id) {
            row = r;
            col = 0;
        } else if (rows[static_cast<std::size_t>(r)].second.id == id) {
            row = r;
            col = 1;
        }
    }
    if (row == -2) {
        return id;
    }
    auto cell = [&](int r, int c) -> int {
        if (r < 0) {
            return kWelcomeContinue;
        }
        const WelcomeRow &at = rows[static_cast<std::size_t>(r)];
        return c == 1 && at.second.id != 0 ? at.second.id : at.first.id;
    };
    if (dy != 0) {
        const int top = has_continue ? -1 : 0;
        const int next = row + (dy < 0 ? -1 : 1);
        if (next < top || next >= kWelcomeRowCount) {
            return id;
        }
        return cell(next, col);
    }
    if (dx != 0 && row >= 0) {
        const WelcomeRow &at = rows[static_cast<std::size_t>(row)];
        if (dx > 0 && col == 0 && at.second.id != 0) {
            return at.second.id;
        }
        if (dx < 0 && col == 1) {
            return at.first.id;
        }
    }
    return id;
}

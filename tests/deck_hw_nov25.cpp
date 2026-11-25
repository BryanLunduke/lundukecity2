// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck hardware notes (Nov 25): fixes from a pass on a real Steam Deck
// that are pure functions. The live checks are in the ui_input test.

#include "pad_feel.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// #1/#5: the left stick reads as a D-pad press in windows and menus.
void stick_as_dpad()
{
    using pad_feel::StickDir;
    using pad_feel::stick_nav_dir;
    check(stick_nav_dir(0.0f, 0.0f, StickDir::None) == StickDir::None, "a resting stick is no press");
    check(stick_nav_dir(0.2f, -0.3f, StickDir::None) == StickDir::None, "a light push is no press");
    check(stick_nav_dir(0.0f, 0.9f, StickDir::None) == StickDir::Down, "down is down");
    check(stick_nav_dir(0.0f, -0.9f, StickDir::None) == StickDir::Up, "up is up");
    check(stick_nav_dir(-0.9f, 0.1f, StickDir::None) == StickDir::Left, "left is left");
    check(stick_nav_dir(0.9f, -0.2f, StickDir::None) == StickDir::Right, "right is right");
    check(stick_nav_dir(0.7f, 0.6f, StickDir::None) == StickDir::Right, "a diagonal takes the stronger axis");
    // Held until the axis falls under the release line, so it does not chatter.
    check(stick_nav_dir(0.0f, 0.4f, StickDir::Down) == StickDir::Down, "a held push stays held above the release line");
    check(stick_nav_dir(0.0f, 0.4f, StickDir::None) == StickDir::None, "the same push is not a new press");
    check(stick_nav_dir(0.0f, 0.2f, StickDir::Down) == StickDir::None, "under the release line it lets go");
    check(stick_nav_dir(0.8f, 0.4f, StickDir::Down) == StickDir::Right, "a turn to another axis is a new press");
}

} // namespace

int main()
{
    stick_as_dpad();
    if (failures != 0) {
        std::fprintf(stderr, "deck_hw_nov25: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("deck_hw_nov25 ok");
    return 0;
}

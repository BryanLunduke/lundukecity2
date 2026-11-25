// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck nov24: Steam Deck review fixes that are pure functions.

#include "micropolis.h"
#include "map_fit.hpp"
#include "tutorial_goals.hpp"
#include "controls_page.hpp"

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

bool has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

// #1: the handheld quick bar reserves the bottom of the view the same way
// the minimap reserves the right, so the last rows scroll up above it.
void bottom_reserve()
{
    const int view = 800;
    const int map = 100 * 16;
    const int reserve = 96;
    const int box = map_letterbox_clear(view, map, reserve);
    check(box == 0, "a map taller than the view has no letterbox");
    check(map_scroll_limit(view, map, box, reserve) == map + reserve - view, "bottom scroll reaches past the bar");
    check(map_scroll_limit(view, map, box, 0) == map - view, "no reserve keeps the old limit");
}

// #3: Learn to Play speaks in buttons with a controller.
void pad_steps()
{
    tutorial::PadWords w;
    w.confirm = "A";
    w.query = "X";
    w.pause = "Y";
    w.prev = "L1";
    w.next = "R1";
    w.menu = "\u2630";
    const std::string roads = tutorial::pad_hint(tutorial::Roads, w);
    check(has(roads, "R1") && has(roads, "hold A") && has(roads, "12 tiles"), "step 1 names R1 and A");
    const std::string inspect = tutorial::pad_hint(tutorial::InspectBudget, w);
    check(has(inspect, "Press X") && has(inspect, "press A") && !has(inspect, "Q") && !has(inspect, "click"),
          "step 6 names X and A, no Q or click");
    const std::string run = tutorial::pad_hint(tutorial::Population, w);
    check(has(run, "Press Y") && !has(run, "Space") && !has(run, "click"), "step 7 names Y, not Space");
    check(tutorial::pad_hint(tutorial::Power, w).empty(), "step 2 reads the same");
    for (int step = 0; step < tutorial::kStepCount; ++step) {
        const std::string banner = std::string("Step 6 of 7: Inspect and Budget. ") + tutorial::hint(step);
        const std::string swapped = tutorial::pad_text(banner, w);
        check(!has(swapped, "press Q") && !has(swapped, "Press Space") && !has(swapped, "click"),
              "no step keeps key or click words");
        check(has(swapped, "Step 6 of 7: Inspect and Budget. "), "the banner prefix stays");
    }
    tutorial::PadWords unbound;
    check(tutorial::pad_text(tutorial::hint(tutorial::Population), unbound) == tutorial::hint(tutorial::Population),
          "unbound buttons keep the desktop text");
}

// #4: How to Play has a Controller topic and button words with a pad.
void how_to_play_pad()
{
    ControlsPadNames deck;
    deck.left = "L1";
    deck.right = "R1";
    deck.menu = "\u2630";
    const std::string desk = how_to_play_text();
    check(desk.find("Controller") == std::string::npos, "desktop How to Play has no Controller topic");
    check(has(desk, "Right-click"), "desktop keeps the mouse wording");
    const std::string pad = how_to_play_text(&deck);
    check(has(pad, "Controller\n") && has(pad, "L1 and R1 pick a tool"), "pad How to Play has a Controller topic");
    check(has(pad, "Press X for Inspect") && !has(pad, "Right-click") && !has(pad, "press Q"),
          "pad Finding your way names buttons");
    const std::vector<HowToPlaySection> sections = how_to_play_sections(&deck);
    check(sections.back().heading == "Touch" && sections[sections.size() - 2].heading == "Controller",
          "Controller sits before Touch");
}

} // namespace

int main()
{
    bottom_reserve();
    pad_steps();
    how_to_play_pad();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("deck_nov24 ok");
    return 0;
}

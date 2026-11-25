// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck nov24: Steam Deck review fixes that are pure functions.

#include "micropolis.h"
#include "map_fit.hpp"
#include "tutorial_goals.hpp"
#include "controls_page.hpp"
#include "pad_feel.hpp"
#include "pad_glyphs.hpp"
#include "tool_groups.hpp"

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

// #5: stick or trigger noise does not take over from the trackpad.
void axis_noise()
{
    check(!pad_feel::axis_wakes(false, 1200.0f / 32767.0f, 0.0f), "stick noise does not wake the pad");
    check(!pad_feel::axis_wakes(false, 0.12f, 0.12f), "a resting pair under the dead zone does not wake");
    check(pad_feel::axis_wakes(false, 0.5f, 0.0f), "a real push wakes the pad");
    check(pad_feel::axis_wakes(false, 0.15f, 0.15f), "a diagonal past the dead zone wakes");
    check(!pad_feel::axis_wakes(true, 0.2f, 0.0f), "a light trigger touch does not wake");
    check(pad_feel::axis_wakes(true, 0.8f, 0.0f), "a trigger pull wakes");
}

// #14: an external Xbox pad on a Deck keeps LB, RB and Start.
void deck_names()
{
    check(pad_uses_deck_names(true, false, 0, 0, 0, nullptr, 0), "a Deck with no pad open yet uses Deck names");
    check(pad_uses_deck_names(true, true, 0x28de, 0x1205, SDL_GAMEPAD_TYPE_STANDARD, "Steam Deck", 0),
          "the built-in controller uses Deck names");
    check(pad_uses_deck_names(true, true, 0x28de, 0x11ff, SDL_GAMEPAD_TYPE_XBOX360, "Steam Virtual Gamepad", 0),
          "the lone Steam virtual pad is the Deck");
    check(!pad_uses_deck_names(true, true, 0x045e, 0x0b12, SDL_GAMEPAD_TYPE_XBOXONE, "Xbox Series X Controller", 1),
          "a plugged-in Xbox pad keeps Xbox names");
    check(!pad_uses_deck_names(true, true, 0x28de, 0x11ff, SDL_GAMEPAD_TYPE_XBOXONE, "Xbox Series X Controller", 1),
          "an Xbox pad through Steam Input beside the Deck keeps Xbox names");
    check(!pad_uses_deck_names(false, true, 0x28de, 0x1205, SDL_GAMEPAD_TYPE_STANDARD, "Steam Deck", 0),
          "a desktop session never uses Deck names");
    const FaceGlyphs xbox = face_glyphs(false, false, SDL_GAMEPAD_TYPE_XBOXONE);
    check(std::string(xbox.left) == "LB" && std::string(xbox.menu) == "Start", "Xbox names are LB and Start");
}

// #6: the handheld layout opens cities at 32 px tiles.
void opening_zoom()
{
    check(opening_map_tile(true, 16) == 32, "a Deck opens a 16 px save at 32 px");
    check(opening_map_tile(true, 48) == 48, "a closer saved zoom is kept");
    check(opening_map_tile(false, 16) == 16, "the desktop keeps 16 px");
}

// #19: a touch on a narrow quick-bar slot opens its section first.
void quick_sections()
{
    const tool_groups::Group &first = tool_groups::groups()[0];
    const tool_groups::Group &second = tool_groups::groups()[1];
    check(tool_groups::same_section(first.first, first.first + first.count - 1), "tools of one section match");
    check(!tool_groups::same_section(first.first, second.first), "tools of two sections differ");
    const int count = 27;
    check(tool_groups::same_section(count - 1, count - 1), "Inspect is its own section");
    check(!tool_groups::same_section(count - 1, first.first), "Inspect is not in Build");
    const tool_groups::QuickBarLayout touch = tool_groups::quick_bar_layout(count, first.first, 56.0, 1280.0 - 24.0);
    check(touch.big >= 56.0, "the open section gets full-size 56 px slots");
}

} // namespace

int main()
{
    bottom_reserve();
    pad_steps();
    how_to_play_pad();
    axis_noise();
    deck_names();
    opening_zoom();
    quick_sections();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("deck_nov24 ok");
    return 0;
}

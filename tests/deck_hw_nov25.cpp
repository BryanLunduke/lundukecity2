// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck hardware notes (Nov 25): fixes from a pass on a real Steam Deck
// that are pure functions. The live checks are in the ui_input test.

#include "pad_feel.hpp"
#include "welcome_rows.hpp"
#include "controls_page.hpp"
#include "dialog_layout.hpp"
#include "pad_glyphs.hpp"

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

// #2: the Welcome buttons are a grid. Left and Right stay in the row.
void welcome_grid()
{
    check(welcome_step(kWelcomeNew, 1, 0, false) == kWelcomeLearn, "Right from New city is Learn to Play");
    check(welcome_step(kWelcomeLearn, -1, 0, false) == kWelcomeNew, "Left from Learn to Play is New city");
    check(welcome_step(kWelcomeNew, -1, 0, false) == kWelcomeNew, "Left at the row's start stays");
    check(welcome_step(kWelcomeLearn, 1, 0, false) == kWelcomeLearn, "Right at the row's end stays");
    check(welcome_step(kWelcomeLoad, 1, 0, false) == kWelcomeLoad, "Right on a one-button row stays");
    check(welcome_step(kWelcomeLoad, -1, 0, false) == kWelcomeLoad, "Left on a one-button row stays");
    check(welcome_step(kWelcomeNew, 0, 1, false) == kWelcomeLoad, "Down from New city is Load");
    check(welcome_step(kWelcomeLearn, 0, 1, false) == kWelcomeLoad, "Down from Learn to Play is Load");
    check(welcome_step(kWelcomeScenario, 0, 1, false) == kWelcomeHowTo, "Down into a two-button row lands left");
    check(welcome_step(kWelcomeHowTo, 1, 0, false) == kWelcomeSettings, "Right from How to play is Settings");
    check(welcome_step(kWelcomeSettings, 0, 1, false) == kWelcomeQuit, "Down from Settings is Quit");
    check(welcome_step(kWelcomeQuit, 0, 1, false) == kWelcomeQuit, "Down at the bottom stays");
    check(welcome_step(kWelcomeNew, 0, -1, false) == kWelcomeNew, "Up at the top stays without Continue");
    check(welcome_step(kWelcomeLearn, 0, -1, true) == kWelcomeContinue, "Up from the first row is Continue");
    check(welcome_step(kWelcomeContinue, 0, 1, true) == kWelcomeNew, "Down from Continue is New city");
    check(welcome_step(kWelcomeContinue, 1, 0, true) == kWelcomeContinue, "Right on Continue stays");
}

// Item 6: the gamepad Controls page names the zoom triggers per pad.
bool lists(const std::vector<ControlsSection> &sections, const std::string &key, const std::string &action)
{
    for (const ControlsSection &section : sections) {
        for (const ControlsRow &row : section.rows) {
            if (row.key == key && row.action.find(action) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

void controls_triggers()
{
    for (int kind = 0; kind < 3; ++kind) {
        const bool playstation = kind == 1;
        const bool deck = kind == 2;
        const FaceGlyphs glyphs = face_glyphs(playstation, deck, SDL_GAMEPAD_TYPE_UNKNOWN);
        ControlsPadNames pad;
        pad.l2 = glyphs.l2;
        pad.r2 = glyphs.r2;
        const auto sections = controls_page_sections(0, true, pad);
        const std::string l2 = kind == 0 ? "LT" : "L2";
        const std::string r2 = kind == 0 ? "RT" : "R2";
        check(lists(sections, r2, "Zoom in"), "Controls lists R2 zoom in");
        check(lists(sections, l2, "Zoom out"), "Controls lists L2 zoom out");
        check(!lists(sections, "Triggers", ""), "Controls no longer says only Triggers");
    }
}

// Item 8: a title the hint does not fit beside keeps the full band.
void title_band_width()
{
    // 420-wide panel, a 260-wide title and a 200-wide hint: no room for both.
    const TitleBand crowded = title_band(420, 6, 260, 200, true);
    check(!crowded.show_hint, "hint hidden when it does not fit beside the title");
    check(crowded.title_wrap >= 260, "title keeps the full width when the hint is hidden");
    const TitleBand roomy = title_band(520, 6, 160, 200, true);
    check(roomy.show_hint, "hint shown when it fits");
    check(roomy.title_wrap >= 160, "title still fits beside the hint");
    const TitleBand none = title_band(420, 6, 380, 0, false);
    check(!none.show_hint && none.title_wrap >= 380, "no hint, full width");
}

} // namespace

int main()
{
    stick_as_dpad();
    welcome_grid();
    controls_triggers();
    title_band_width();
    if (failures != 0) {
        std::fprintf(stderr, "deck_hw_nov25: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("deck_hw_nov25 ok");
    return 0;
}

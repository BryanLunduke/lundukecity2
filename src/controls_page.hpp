// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "tool_keys.hpp"

#include <string>

inline char controls_letter(char key)
{
    if (key >= 'a' && key <= 'z') {
        return static_cast<char>(key - 'a' + 'A');
    }
    return key;
}

// Help → Controls is generated from the bindings the shell actually uses.
// A Deck session leads with the gamepad page. D-pad left and right turn
// the page. The shell scrolls a page that is taller than the window.

// Y runs the city. The menu button then opens Options, unless that button
// is already named Options (a PlayStation pad), which would read twice.
inline std::string pause_run_hint(const std::string &run_button, const std::string &menu_button)
{
    if (menu_button == "Options") {
        return "Game paused. Press " + run_button + ", or Options, to run the city.";
    }
    return "Game paused. Press " + run_button + ", or " + menu_button + " then Options, to run the city.";
}

inline int controls_page_count()
{
    return 3;
}

// One screen on first launch: zones, power, roads, the demand meters, and the budget.
inline std::string how_to_play_text()
{
    return "Place residential, commercial, and industrial zones. That is where the city grows.\n"
           "\n"
           "Connect them with roads, and run power lines from a coal or nuclear plant to every zone.\n"
           "\n"
           "The R, C, and I meters show demand. A bar above the middle means the city wants more of that zone.\n"
           "\n"
           "Each January the budget opens. Set the tax and the road, police, and fire funding, then collect taxes. "
           "Auto budget does this for you.\n"
           "\n"
           "Police and fire stations need that funding, or they stop covering the blocks around them.\n"
           "\n"
           "A scenario names a goal and a deadline. Query explains a tile. The minimap jumps across the city.\n"
           "\n"
           "On a touchscreen, a tap builds one tile. A drag pans the map. Hold still, then drag, to lay a line. "
           "Two fingers pan or pinch to zoom.";
}

inline std::string controls_page_indicator(int page)
{
    int index = page % 3;
    if (index < 0) {
        index += 3;
    }
    return std::to_string(index + 1) + "/3";
}

inline int controls_page_index(int page, bool deck)
{
    int index = page % 3;
    if (index < 0) {
        index += 3;
    }
    if (!deck) {
        return index;
    }
    static const int order[] = {1, 0, 2};
    return order[index];
}

inline const char *controls_page_title(int page, bool deck)
{
    switch (controls_page_index(page, deck)) {
    case 1:
        return "Gamepad";
    case 2:
        return "Touch";
    default:
        return "Keyboard and mouse";
    }
}

inline bool controls_page_has_tools(int page, bool deck)
{
    return controls_page_index(page, deck) == 0;
}

struct ControlsKeyText {
    std::string pause = "Space";
    std::string slow = "Ctrl+1";
    std::string medium = "Ctrl+2";
    std::string fast = "Ctrl+3";
    std::string neu = "Ctrl+N";
    std::string load = "Ctrl+O";
    std::string save = "Ctrl+S";
    std::string save_as = "Ctrl+Shift+S";
    std::string quit = "Ctrl+Q";
    std::string zoom_in = "Ctrl+=";
    std::string zoom_out = "Ctrl+-";
    std::string budget = "Ctrl+B";
    std::string graphs = "Ctrl+G";
    std::string evaluation = "Ctrl+E";
    std::string fullscreen = "F11";
    std::string tool[17];

    ControlsKeyText()
    {
        static const char letters[] = {'B', 'R', 'T', 'W', 'G', 'H', 'C', 'I', 'O', 'F', 'D', 'P', 'E', 'U', 'A', 'V', 'Q'};
        for (int i = 0; i < 17; ++i) {
            tool[i] = std::string(1, letters[i]);
        }
    }
};

inline std::string controls_tool_column(int begin, int end, const ControlsKeyText *keys = nullptr)
{
    static const char *names[] = {"Bulldozer", "Roads", "Rail", "Power lines", "Park", "Residential",
                                  "Commercial", "Industrial", "Police", "Fire dept", "Stadium", "Seaport",
                                  "Coal power", "Nuclear", "Airport", "Special", "Query"};
    std::string text;
    if (begin < 0) {
        begin = 0;
    }
    if (end > 17) {
        end = 17;
    }
    for (int i = begin; i < end; ++i) {
        if (keys != nullptr && !keys->tool[i].empty()) {
            text += keys->tool[i];
        } else {
            text.push_back(controls_letter(tool_hotkey(i)));
        }
        text += "  ";
        text += names[i];
        if (i + 1 < end) {
            text += "\n";
        }
    }
    return text;
}

// Names come from Shell::glyph so a Deck says L1 and a PlayStation pad says
// its own face symbols. The back grips and the Guide button are not mapped,
// so this page does not mention them.
struct ControlsPadNames {
    const char *confirm = "A";
    const char *cancel = "B";
    const char *west = "X";
    const char *north = "Y";
    const char *left = "LB";
    const char *right = "RB";
    const char *menu = "Start";
    const char *view = "Back";
    const char *l3 = "LS";
    const char *r3 = "RS";
};

inline std::string controls_page_body(int page, bool deck, const ControlsPadNames &pad = {},
                                      const ControlsKeyText &keys = {})
{
    switch (controls_page_index(page, deck)) {
    case 1: {
        std::string body;
        body += pad.confirm;
        body += " confirms, builds, or opens the highlighted row.\n";
        body += pad.cancel;
        body += " cancels or closes. On a field, ";
        body += pad.cancel;
        body += " deletes a letter. When nothing is left to delete, ";
        body += pad.cancel;
        body += " leaves the field, and the next ";
        body += pad.cancel;
        body += " closes the window.\n";
        body += pad.view;
        body += " does the same as ";
        body += pad.cancel;
        body += ".\n";
        body += pad.west;
        body += " is Query on the map, and deletes a letter while a field is active.\n";
        body += pad.north;
        body += " pauses and resumes the previous speed. Hold ";
        body += pad.north;
        body += " to cycle slow, medium, and fast. On the on-screen keyboard it types a space. On the land page it draws new terrain.\n";
        body += pad.menu;
        body += " opens the menu. In a window it confirms OK when that window has one. On a field or a keyboard key, ";
        body += pad.menu;
        body += " confirms that page.\n";
        body += "Hold ";
        body += pad.confirm;
        body += " and move the D-pad or the left stick to drag a line of the current tool.\n";
        body += "The left stick moves the cursor. The D-pad moves one step, then repeats while held. It scrolls a list, adjusts a slider, or pans the map.\n";
        body += pad.left;
        body += " and ";
        body += pad.right;
        body += " change tools when no window is open, and take a larger step on a slider.\n";
        body += pad.l3;
        body += " toggles the map overlay. ";
        body += pad.r3;
        body += " centers the view on the cursor.\n";
        body += "The right stick pans. Each trigger press zooms one step.\n";
        body += pad.cancel;
        body += " closes a map overlay.";
        return body;
    }
    case 2:
        return std::string("Tap a tool, a menu, or a button to use it. Buttons and menu rows are sized for a finger.\n"
               "\n"
               "A tap on the map builds one tile when the finger lifts. A drag pans. Hold still for a moment, then drag, to lay a line with the current tool.\n"
               "\n"
               "A second finger pans or pinches to zoom. Drag a slider, the minimap, or a list with one finger.\n"
               "\n"
               "The on-screen keyboard types into the name and seed fields. Random fills a city name and does not repeat the last one.\n"
               "\n"
               "Confirming the page does not type the highlighted key.");
    default:
        break;
    }
    return "Enter confirms. Esc closes a window, and closes a map overlay when no window is open.\n" + keys.pause +
           " pauses. Ctrl+P pauses. " + keys.slow + " slow, " + keys.medium + " medium, " + keys.fast + " fast.\n" +
           keys.neu + " new city. " + keys.load + " load. " + keys.save + " save. " + keys.save_as + " save as. " +
           keys.quit + " quit.\n"
           "Arrow keys pan. Hold Ctrl and press an arrow to move a build cursor. Enter builds with it. Esc clears it.\n" +
           keys.zoom_in + " and " + keys.zoom_out +
           " zoom. " + keys.fullscreen +
           " or Alt+Enter toggles fullscreen.\n"
           "The wheel scrolls the map. Hold Ctrl and roll the wheel to zoom. Settings can make the wheel zoom by itself.\n"
           "Left drag builds. Right or middle drag pans. A short right-click switches to Query.\n"
           "In Graphs, Left and Right read the values under the chart.\n"
           "Tool keys:";
}

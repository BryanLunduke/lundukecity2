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

inline int controls_page_count()
{
    return 3;
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

inline std::string controls_tool_column(int begin, int end)
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
        text.push_back(controls_letter(tool_hotkey(i)));
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

inline std::string controls_page_body(int page, bool deck, const ControlsPadNames &pad = {})
{
    switch (controls_page_index(page, deck)) {
    case 1: {
        std::string body;
        body += pad.confirm;
        body += " confirms, builds, or opens the highlighted row.\n";
        body += pad.cancel;
        body += " cancels or closes. On a field, ";
        body += pad.cancel;
        body += " deletes a letter. On an empty field, ";
        body += pad.cancel;
        body += " closes the window.\n";
        body += pad.view;
        body += " does the same as ";
        body += pad.cancel;
        body += ".\n";
        body += pad.west;
        body += " is Query on the map, and deletes a letter while a field is active.\n";
        body += pad.north;
        body += " cycles pause, slow, medium, and fast. On the on-screen keyboard it types a space. On the land page it draws new terrain.\n";
        body += pad.menu;
        body += " opens the menu. On a field or a keyboard key, ";
        body += pad.menu;
        body += " confirms that page.\n";
        body += "The left stick moves the cursor. The D-pad moves the highlight, scrolls a list or this page, or pans the map.\n";
        body += pad.left;
        body += " and ";
        body += pad.right;
        body += " change tools when no window is open.\n";
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
        return std::string("Tap a tool, a menu, or a button to use it.\n"
               "A tap on the map builds when the finger lifts. Moving past a short distance starts a drag, and the drag builds as it moves.\n"
               "A second finger before that drag starts cancels it, and nothing is built. A second finger during a drag ends the stroke and keeps the tiles already placed, then pans or pinches to zoom.\n"
               "Drag the minimap to jump across the city.\n"
               "Drag a list to scroll it.\n"
               "The on-screen keyboard types into the name and seed fields.\n"
               "Random fills a city name and does not repeat the last one.\n") +
               pad.menu + " confirms the page instead of typing the highlighted key.";
    default:
        break;
    }
    return "Enter confirms. Esc closes a window, and closes a map overlay when no window is open.\n"
           "Space pauses. Ctrl+P pauses. Ctrl+1 slow, Ctrl+2 medium, Ctrl+3 fast.\n"
           "Ctrl+N new city. Ctrl+O load. Ctrl+S save. Ctrl+Shift+S save as. Ctrl+Q quit.\n"
           "Arrow keys pan. Ctrl+= and Ctrl+- zoom. F11 or Alt+Enter toggles fullscreen.\n"
           "The wheel scrolls the map. Hold Ctrl and roll the wheel to zoom. Settings can make the wheel zoom by itself.\n"
           "Left drag builds. Right or middle drag pans. A short right-click switches to Query.\n"
           "Tool keys, with no Ctrl or Alt:";
}

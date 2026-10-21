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
// the page; the text stays inside the dialog.

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

inline std::string controls_page_body(int page, bool deck)
{
    switch (controls_page_index(page, deck)) {
    case 1:
        return "A confirms, builds, or opens the highlighted row.\n"
               "B cancels, closes, or deletes a letter. On an empty field, B closes the window.\n"
               "X is Query on the map, and deletes a letter while a field is active.\n"
               "Y pauses the city. On the keyboard it types a space. On the land page it draws new terrain.\n"
               "Start opens the menu. While a field is active, Start confirms that page.\n"
               "D-pad moves the highlight, scrolls a list, or pans the map.\n"
               "LB and RB change tools when no window is open.\n"
               "The right stick pans. The triggers zoom.\n"
               "Left and right change this page.";
    case 2:
        return "Tap a tool, a menu, or a button to use it.\n"
               "A tap on the map builds. A short hold starts a drag without building on the first touch.\n"
               "A second finger cancels that stroke and pans or pinches to zoom.\n"
               "Drag the minimap to jump across the city.\n"
               "Drag a list to scroll it.\n"
               "The on-screen keyboard types into the name and seed fields.\n"
               "Random fills a city name.";
    default:
        break;
    }
    std::string text =
        "Enter confirms. Esc closes.\n"
        "Space pauses. Ctrl+P pauses. Ctrl+1 slow, Ctrl+2 medium, Ctrl+3 fast.\n"
        "Ctrl+N new city. Ctrl+O load. Ctrl+S save. Ctrl+Shift+S save as. Ctrl+Q quit.\n"
        "Arrow keys pan. Ctrl+= and Ctrl+- zoom. The wheel scrolls. Hold Ctrl and roll the wheel to zoom.\n"
        "F11 or Alt+Enter toggles fullscreen. Options and Settings have the same switch.\n"
        "Tool keys, with no Ctrl or Alt:\n";
    static const char *names[] = {"Bulldozer", "Roads", "Rail", "Power lines", "Park", "Residential",
                                  "Commercial", "Industrial", "Police", "Fire dept", "Stadium", "Seaport",
                                  "Coal power", "Nuclear", "Airport", "Special", "Query"};
    for (int i = 0; i < 17; ++i) {
        const char key = tool_hotkey(i);
        text.push_back(controls_letter(key));
        text += " ";
        text += names[i];
        text += "\n";
    }
    return text;
}

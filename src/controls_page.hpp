// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "tool_keys.hpp"

#include <string>
#include <vector>

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
    const bool run = !run_button.empty();
    const bool menu = !menu_button.empty();
    if (!run && !menu) {
        return "Game paused.";
    }
    if (!run) {
        return menu_button == "Options" ? "Game paused. Press Options to run the city."
                                        : "Game paused. Press " + menu_button + " then Options, to run the city.";
    }
    if (!menu || menu_button == "Options") {
        return menu ? "Game paused. Press " + run_button + ", or Options, to run the city."
                    : "Game paused. Press " + run_button + " to run the city.";
    }
    return "Game paused. Press " + run_button + ", or " + menu_button + " then Options, to run the city.";
}

inline int controls_page_count()
{
    return 3;
}

struct HowToPlaySection {
    const char *heading;
    const char *body;
};

// One screen on first launch: zones, power, roads, the demand meters, and the
// budget. Each topic is a short heading over a sentence or two.
inline std::vector<HowToPlaySection> how_to_play_sections()
{
    return {
        {"Zones and roads",
         "Place residential, commercial, and industrial zones. That is where the city grows. Connect them with roads."},
        {"Power", "Build a coal or nuclear plant, and run power lines from it to every zone."},
        {"Demand", "The R, C, and I meters show demand. A bar above the middle means the city wants more of that zone."},
        {"Budget",
         "Each January the budget opens. Set the tax and the road, police, and fire funding, then collect taxes. "
         "Auto budget does this for you. Police and fire stations need that funding, or they stop covering the "
         "blocks around them."},
        {"Finding your way",
         "A scenario names a goal and a deadline. Right-click the map, or press Q, to switch to Inspect, then click "
         "a tile for its card: power, road, and why it is or is not growing. The minimap jumps across the city."},
        {"Touch",
         "A tap builds one tile. A drag pans the map. Hold still, then drag, to lay a line. Two fingers pan or pinch "
         "to zoom."},
    };
}

inline std::string how_to_play_text()
{
    std::string text;
    for (const HowToPlaySection &section : how_to_play_sections()) {
        if (!text.empty()) {
            text += "\n\n";
        }
        text += section.heading;
        text += "\n";
        text += section.body;
    }
    return text;
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
    std::string tool[kToolHotkeyCount];

    ControlsKeyText()
    {
        for (int i = 0; i < kToolHotkeyCount; ++i) {
            const char key = tool_hotkey(i);
            if (key != 0) {
                tool[i] = std::string(1, static_cast<char>(key - 'a' + 'A'));
            }
        }
    }
};

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

// One row of the Controls window: the key or button on the left, what it
// does on the right. A section is a heading over its rows.
struct ControlsRow {
    std::string key;
    std::string action;
};

struct ControlsSection {
    std::string heading;
    std::vector<ControlsRow> rows;
    // Which column of the window the section sits in: 0 left, 1 middle or
    // right. Side 2 is a column of its own when the window is wide enough,
    // otherwise a full-width band below, split into a few side-by-side runs.
    int side = 0;
};

inline const char *controls_tool_name(int index)
{
    static const char *names[] = {"Bulldozer",     "Roads",          "Rail",       "Power lines",   "Residential",
                                  "Commercial",    "Industrial",     "Data Center", "Park",         "Big Park",
                                  "Fountain",      "Fire Station",      "Police Station",      "Prison",       "Coal Plant",
                                  "Nuclear Plant",       "Micro Nuclear Plant",  "Zoo",         "Amusement Park", "Casino",
                                  "Stadium",       "Marina",         "Library",     "Mayor's House", "Airport",
                                  "Seaport",       "Inspect (a tile's card)"};
    static_assert(sizeof(names) / sizeof(names[0]) == kToolHotkeyCount, "one name per tool");
    if (index < 0 || index >= kToolHotkeyCount) {
        return "";
    }
    return names[index];
}

// The key shown for a tool: the remapped key when there is one.
inline std::string controls_tool_key(int index, const ControlsKeyText &keys)
{
    if (index >= 0 && index < kToolHotkeyCount && !keys.tool[index].empty()) {
        return keys.tool[index];
    }
    const char key = tool_hotkey(index);
    return key == 0 ? std::string() : std::string(1, controls_letter(key));
}

inline std::vector<ControlsSection> controls_page_sections(int page, bool deck, const ControlsPadNames &pad = {},
                                                           const ControlsKeyText &keys = {})
{
    std::vector<ControlsSection> sections;
    const std::string a = pad.confirm;
    const std::string b = pad.cancel;
    switch (controls_page_index(page, deck)) {
    case 1:
        sections.push_back({"Buttons",
                            {
                                {a, "Confirm, build, or open the highlighted row"},
                                {b + " or " + pad.view, "Cancel or close. In a field, delete a letter first"},
                                {pad.west, "Switch to Inspect. In a field, delete a letter"},
                                {pad.north, "Pause or resume. Hold to cycle slow, medium, and fast"},
                                {pad.menu, "Open the menu. In a window, press OK"},
                                {std::string(pad.left) + " / " + pad.right, "Previous or next tool. Hold to jump a section. A bigger slider step"},
                                {pad.l3, "Show or hide the map overlay"},
                                {pad.r3, "Center the view on the cursor"},
                            }, 0});
        sections.push_back({"Moving around",
                            {
                                {"Left stick", "Move the cursor"},
                                {"D-pad", "Step and repeat: scroll a list, adjust a slider, or pan"},
                                {"Right stick", "Pan the map"},
                                {"Triggers", "Zoom one step per press"},
                                {"Hold " + a + " and move", "Drag a line with the current tool"},
                            }, 1});
        sections.push_back({"New City window",
                            {
                                {pad.north, "On the keyboard, type a space. On the land page, draw new terrain"},
                                {pad.menu, "Confirm the keyboard or the field"},
                            }, 1});
        return sections;
    case 2:
        sections.push_back({"Map",
                            {
                                {"Tap", "Build one tile when the finger lifts"},
                                {"Drag", "Pan the map"},
                                {"Hold, then drag", "Lay a line with the current tool"},
                                {"Two fingers", "Pan, or pinch to zoom"},
                            }, 0});
        sections.push_back({"Windows and lists",
                            {
                                {"Tap", "Use a tool, a menu, or a button"},
                                {"One-finger drag", "Move a slider, the minimap, or a list"},
                                {"Keyboard", "Types into the name and seed fields"},
                                {"Random", "Picks a new city name"},
                            }, 1});
        return sections;
    default:
        break;
    }
    sections.push_back({"Game",
                        {
                            {"Enter", "Confirm, or build at the cursor"},
                            {"Esc", "Close a window or overlay"},
                            {keys.pause, "Pause or resume"},
                            {"Ctrl+P", "Pause"},
                            {keys.slow, "Slow speed"},
                            {keys.medium, "Medium speed"},
                            {keys.fast, "Fast speed"},
                            {keys.fullscreen, "Fullscreen"},
                            {"Alt+Enter", "Fullscreen"},
                        }, 0});
    sections.push_back({"Map",
                        {
                            {"Arrow keys", "Pan"},
                            {"Ctrl+Arrow", "Move the build cursor"},
                            {keys.zoom_in, "Zoom in"},
                            {keys.zoom_out, "Zoom out"},
                            {"Wheel", "Scroll the map"},
                            {"Ctrl+Wheel", "Zoom"},
                            {"Left drag", "Build with the current tool"},
                            {"Right drag", "Pan"},
                            {"Middle drag", "Pan"},
                            {"Right-click", "Switch to Inspect"},
                        }, 1});
    sections.push_back({"Analysis",
                        {
                            {keys.budget, "Budget"},
                            {keys.graphs, "Graphs"},
                            {keys.evaluation, "Evaluation"},
                            {"Left / Right", "Read a graph"},
                        }, 1});
    sections.push_back({"City",
                        {
                            {keys.neu, "New city"},
                            {keys.load, "Load a city"},
                            {keys.save, "Save"},
                            {keys.save_as, "Save as"},
                            {keys.quit, "Quit"},
                        }, 0});
    ControlsSection tools{"Tool keys", {}, 2};
    for (int i = 0; i < kToolHotkeyCount; ++i) {
        const std::string key = controls_tool_key(i, keys);
        // A tool with no key (the Mayor's House by default) still gets a
        // row, so players can see it exists and that Remap can bind it.
        if (key.empty() || key == "None") {
            tools.rows.push_back({"\u2013", std::string(controls_tool_name(i)) + " (unbound)"});
            continue;
        }
        tools.rows.push_back({key, controls_tool_name(i)});
    }
    sections.push_back(tools);
    return sections;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

// Palette order is the toolbar order: Build, Zones, Parks, Services,
// Utilities, Special buildings, Ports (tool_groups.hpp), then Query. The
// handheld quick bar, L1/R1 cycling and the Controls page follow it too.
// Key bindings are saved by name (bindings.hpp), not by this index.
// engine_id values are the Micropolis EditingTool ordinals.
struct ToolDef {
    int engine_id;
    const char *name;
    int cost;
    const char *hint;
    // A kSpecialEngineId button that always places this special centre tile
    // (the Data Center, the Micro Nuclear Plant and every landmark). 0 for
    // the engine tools.
    int special_tile = 0;
};

extern const ToolDef kTools[];
extern const int kToolCount;
extern const int kDefaultToolIndex;
// Buttons in the toolbar grid: every tool but Query (the last entry).
extern const int kPaletteToolCount;

const ToolDef *tool_by_index(int index);
// Special-building table row a fixed button places, or -1 (an engine tool).
int tool_fixed_special(const ToolDef *tool);
// Click / press instruction for a palette entry. Fixed special buttons use
// their own name ("Data Center: click to zone"); the rest are the
// engine_id phrases below.
std::string tool_click_text(const ToolDef *tool);
std::string tool_press_text(const ToolDef *tool, const char *button);
// Short instruction for the selected tool, such as "Bulldozer: click to clear".
const char *tool_click_phrase(int engine_id);
// False for Query, the inspect mode with no tool picked: the status line
// shows no click hint and no tool name for it, so its right side is empty.
bool tool_has_status_hint(int engine_id);
// Same instruction with a pad or touch verb, such as "Query: A inspects".
// An empty button keeps the mouse "click to" wording.
std::string tool_press_phrase(int engine_id, const char *button);

// Footprint of a placeable tool, in tiles. width/height follow the engine's
// gToolSize[] (tool.cpp). Buildings larger than one tile use the cursor as
// the center; buildBuilding() then steps one tile up and left (mapH--; mapV--),
// which is cursor_to_left / cursor_to_top. Query is not placeable.
struct ToolFootprint {
    bool placeable = false;
    int width = 1;
    int height = 1;
    int cursor_to_left = 0;
    int cursor_to_top = 0;
};

ToolFootprint tool_footprint(int engine_id);

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "tools.hpp"

#include "micropolis.h"
#include "special_buildings.hpp"

#include <cassert>
#include <string>

const ToolDef kTools[] = {
    // Build
    {TOOL_BULLDOZER, "Bulldozer", 1, "Bulldozer: $1"},
    {TOOL_ROAD, "Roads", 10, "Roads: $10. Bridge: $50"},
    {TOOL_RAILROAD, "Rail", 20, "Rail: $20. Rail bridge: $100"},
    {TOOL_WIRE, "Power lines", 5, "Power lines: $5. Underwater wire: $25"},
    // Zones
    {TOOL_RESIDENTIAL, "Residential", 100, "Residential: $100"},
    {TOOL_COMMERCIAL, "Commercial", 100, "Commercial: $100"},
    {TOOL_INDUSTRIAL, "Industrial", 100, "Industrial: $100"},
    {kSpecialEngineId, "Data Center", 400, "Data Center: $400. Needs power to open. Then industrial + commercial, no pollution",
     SPECIAL_DATACENTER_LOT},
    // Parks
    {TOOL_PARK, "Park", 10, "Park: $10"},
    {kSpecialEngineId, "Big Park", 100, "Big Park: $100. Raises nearby land value", SPECIAL_PARK},
    {kSpecialEngineId, "Fountain", 20, "Fountain: $20. Raises nearby land value. Fountains close together add less", SPECIAL_FOUNTAIN},
    // Services
    {TOOL_FIRESTATION, "Fire Station", 500, "Fire Station: $500"},
    {TOOL_POLICESTATION, "Police Station", 500, "Police Station: $500"},
    {kSpecialEngineId, "Prison", 3000, "Prison: $3,000. Cuts crime nearby and 15% city-wide with power and a road. Lowers land value nearby", SPECIAL_PRISON},
    // Utilities
    {TOOL_COALPOWER, "Coal Plant", 3000, "Coal Plant: $3,000"},
    {TOOL_NUCLEARPOWER, "Nuclear Plant", 5000, "Nuclear Plant: $5,000"},
    {kSpecialEngineId, "Micro Nuclear Plant", 2000, "Micro Nuclear Plant: $2,000. 1/9 of a nuclear plant, clean, no meltdown",
     SPECIAL_MICRONUKE},
    // Special buildings
    {kSpecialEngineId, "Zoo", 100, "Zoo: $100. Raises land value in a wide area with power", SPECIAL_ZOO},
    {kSpecialEngineId, "Amusement Park", 100, "Amusement Park: $100. Raises land value with power. Up to $200 a year with power, a road and 2,000 residents nearby", SPECIAL_AMUSEMENT},
    {kSpecialEngineId, "Casino", 100, "Casino: $100. Raises land value and crime. Up to $300 a year with power, a road and 2,000 residents nearby", SPECIAL_CASINO},
    {TOOL_STADIUM, "Stadium", 5000, "Stadium: $5,000"},
    {kSpecialEngineId, "Marina", 1000, "Marina: $1,000. Raises land value widely. Up to $100 a year with power, a road and 2,000 residents nearby. The south side is the dock", SPECIAL_MARINA},
    {kSpecialEngineId, "Library", 100, "Library: $100. Raises land value and cuts crime nearby with power", SPECIAL_LIBRARY},
    {kSpecialEngineId, "Mayor's House", 100, "Mayor's House: $100. Lifts land value, a bit city-wide, with power", SPECIAL_MAYOR},
    // Ports
    {TOOL_AIRPORT, "Airport", 10000, "Airport: $10,000"},
    {TOOL_SEAPORT, "Seaport", 3000, "Seaport: $3,000"},
    {TOOL_QUERY, "Inspect", 0, "Inspect"},
};

const int kToolCount = static_cast<int>(sizeof(kTools) / sizeof(kTools[0]));
const int kDefaultToolIndex = 26; // Query. A first click reports the tile and does not build.
// Query has no toolbar button. It is the inspect mode with no build tool
// picked: the hover card shows what is under the pointer, Q and the pad
// shoulder buttons still reach it, and nothing is built.
const int kPaletteToolCount = kToolCount - 1;

const ToolDef *tool_by_index(int index)
{
    if (index < 0 || index >= kToolCount) {
        return &kTools[0];
    }
    return &kTools[index];
}

int tool_fixed_special(const ToolDef *tool)
{
    if (tool == nullptr || tool->engine_id != kSpecialEngineId || tool->special_tile == 0) {
        return -1;
    }
    return special_index_for_tile(tool->special_tile);
}

namespace {

const char *fixed_special_verb(const ToolDef *tool, bool press)
{
    const bool zone = tool->special_tile == SPECIAL_DATACENTER_LOT;
    if (press) {
        return zone ? "zones" : "builds";
    }
    return zone ? "click to zone" : "click to build";
}

} // namespace

std::string tool_click_text(const ToolDef *tool)
{
    if (tool_fixed_special(tool) >= 0) {
        return std::string(tool->name) + ": " + fixed_special_verb(tool, false);
    }
    return tool_click_phrase(tool != nullptr ? tool->engine_id : TOOL_QUERY);
}

std::string tool_press_text(const ToolDef *tool, const char *button)
{
    if (tool_fixed_special(tool) >= 0) {
        if (button == nullptr || button[0] == '\0') {
            return tool_click_text(tool);
        }
        return std::string(tool->name) + ": " + button + " " + fixed_special_verb(tool, true);
    }
    return tool_press_phrase(tool != nullptr ? tool->engine_id : TOOL_QUERY, button);
}

// The names match the toolbar (kTools), so the mouse and the pad call a
// tool the same thing (review nov18b #13).
const char *tool_click_phrase(int engine_id)
{
    switch (engine_id) {
    case TOOL_BULLDOZER:
        return "Bulldozer: click to clear";
    case TOOL_ROAD:
        return "Roads: click to lay";
    case TOOL_RAILROAD:
        return "Rail: click to lay";
    case TOOL_WIRE:
        return "Power lines: click to lay";
    case TOOL_PARK:
        return "Park: click to plant";
    case TOOL_RESIDENTIAL:
        return "Residential: click to zone";
    case TOOL_COMMERCIAL:
        return "Commercial: click to zone";
    case TOOL_INDUSTRIAL:
        return "Industrial: click to zone";
    case TOOL_POLICESTATION:
        return "Police Station: click to build";
    case TOOL_FIRESTATION:
        return "Fire Station: click to build";
    case TOOL_STADIUM:
        return "Stadium: click to build";
    case TOOL_SEAPORT:
        return "Seaport: click to build";
    case TOOL_COALPOWER:
        return "Coal Plant: click to build";
    case TOOL_NUCLEARPOWER:
        return "Nuclear Plant: click to build";
    case TOOL_AIRPORT:
        return "Airport: click to build";
    case kSpecialEngineId:
        return "Special: click to place";
    case TOOL_QUERY:
        return "Inspect: click a tile";
    default:
        return "Click to build";
    }
}

bool tool_has_status_hint(int engine_id)
{
    return engine_id != TOOL_QUERY;
}

std::string tool_press_phrase(int engine_id, const char *button)
{
    if (button == nullptr || button[0] == '\0') {
        return tool_click_phrase(engine_id);
    }
    const char *name = "Tool";
    const char *verb = "uses";
    switch (engine_id) {
    case TOOL_BULLDOZER:
        name = "Bulldozer";
        verb = "clears";
        break;
    case TOOL_ROAD:
        name = "Roads";
        verb = "lays";
        break;
    case TOOL_RAILROAD:
        name = "Rail";
        verb = "lays";
        break;
    case TOOL_WIRE:
        name = "Power lines";
        verb = "lays";
        break;
    case TOOL_PARK:
        name = "Park";
        verb = "plants";
        break;
    case TOOL_RESIDENTIAL:
        name = "Residential";
        verb = "zones";
        break;
    case TOOL_COMMERCIAL:
        name = "Commercial";
        verb = "zones";
        break;
    case TOOL_INDUSTRIAL:
        name = "Industrial";
        verb = "zones";
        break;
    case TOOL_POLICESTATION:
        name = "Police Station";
        verb = "builds";
        break;
    case TOOL_FIRESTATION:
        name = "Fire Station";
        verb = "builds";
        break;
    case TOOL_STADIUM:
        name = "Stadium";
        verb = "builds";
        break;
    case TOOL_SEAPORT:
        name = "Seaport";
        verb = "builds";
        break;
    case TOOL_COALPOWER:
        name = "Coal Plant";
        verb = "builds";
        break;
    case TOOL_NUCLEARPOWER:
        name = "Nuclear Plant";
        verb = "builds";
        break;
    case TOOL_AIRPORT:
        name = "Airport";
        verb = "builds";
        break;
    case kSpecialEngineId:
        name = "Special";
        verb = "places";
        break;
    case TOOL_QUERY:
        name = "Inspect";
        verb = "inspects";
        break;
    default:
        return tool_click_phrase(engine_id);
    }
    return std::string(name) + ": " + button + " " + verb;
}

ToolFootprint tool_footprint(int engine_id)
{
    // Same sizes as static gToolSize[] in the engine (indexed by EditingTool):
    //   res/com/ind/fire/police = 3, stadium/seaport/coal/nuclear = 4,
    //   airport = 6, query and the line tools = 1.
    // Multi-tile buildings are anchored on the cursor, then buildBuilding()
    // moves to the top-left with mapH--; mapV--.
    switch (engine_id) {
    case TOOL_RESIDENTIAL:
    case TOOL_COMMERCIAL:
    case TOOL_INDUSTRIAL:
    case TOOL_FIRESTATION:
    case TOOL_POLICESTATION:
        return {true, 3, 3, 1, 1};
    case TOOL_STADIUM:
    case TOOL_SEAPORT:
    case TOOL_COALPOWER:
    case TOOL_NUCLEARPOWER:
        return {true, 4, 4, 1, 1};
    case TOOL_AIRPORT:
        return {true, 6, 6, 1, 1};
    case kSpecialEngineId:
        return {true, 3, 3, 1, 1};
    case TOOL_WIRE:
    case TOOL_BULLDOZER:
    case TOOL_RAILROAD:
    case TOOL_ROAD:
    case TOOL_PARK:
    case TOOL_NETWORK:
    case TOOL_WATER:
    case TOOL_LAND:
    case TOOL_FOREST:
        return {true, 1, 1, 0, 0};
    case TOOL_QUERY:
    default:
        return {false, 1, 1, 0, 0};
    }
}

namespace {

struct ToolIdCheck {
    ToolIdCheck()
    {
        assert(TOOL_BULLDOZER == kTools[0].engine_id);
        assert(TOOL_WIRE == kTools[3].engine_id);
        assert(SPECIAL_DATACENTER_LOT == kTools[7].special_tile);
        assert(SPECIAL_FOUNTAIN == kTools[10].special_tile);
        assert(SPECIAL_PRISON == kTools[13].special_tile);
        assert(SPECIAL_MICRONUKE == kTools[16].special_tile);
        assert(TOOL_STADIUM == kTools[20].engine_id);
        assert(SPECIAL_MAYOR == kTools[23].special_tile);
        assert(TOOL_SEAPORT == kTools[25].engine_id);
        assert(TOOL_QUERY == kTools[26].engine_id);
        assert(kToolCount == 27);
    }
};

const ToolIdCheck kToolIdCheck;

} // namespace

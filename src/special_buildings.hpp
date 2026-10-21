// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

class Micropolis;

// Special buildings are one row in this table. Placement, query, income,
// land value, the dropdown, and the map art all walk the table. A new
// building needs a free center-tile id (1020-1022 are the ids the classic
// tileset left unused), a toolbar icon, and a map sprite.

struct SpecialBuilding {
    int center_tile = 0;
    const char *name = "";
    // 40px toolbar / dropdown icon, under tool-icons/.
    const char *icon = "";
    // 48px map sprite (3x3 tiles), under special-tiles/.
    const char *map_sprite = "";
    int cost = 100;
    // Added to the treasury each January. 0 means no payout.
    int annual_income = 0;
    int width = 3;
    int height = 3;
    // Chebyshev radius, in tiles, of the land-value bump.
    int land_radius = 6;
    // Added to developed land-value cells (the engine scale is 1..250).
    // The SNES manuals say the bump is sharp and local, and do not publish
    // the formula. 48 inside 6 tiles is that reading.
    int land_bonus = 48;
    const char *effect = "";
};

// Shared non-center tile. The center tile id says which building it is.
inline constexpr int kSpecialBodyTile = 1023;
inline constexpr int kSpecialEngineId = -1;

int special_building_count();
const SpecialBuilding &special_building(int index);
// -1 when the tile is not a center id from the table.
int special_index_for_tile(int tile);

int special_annual_income(const Micropolis *sim);
void boost_special_land_value(Micropolis *sim);

// Click is the center tile, matching the other 3x3 buildings.
// On failure, error is a short status-bar sentence and the map is unchanged.
bool place_special_building(Micropolis *sim, int index, int tile_x, int tile_y, std::string &error);

// Removes the whole 3x3 and charges $1, the bulldozer price. False when
// the tile is not part of a special building.
bool bulldoze_special_at(Micropolis *sim, int tile_x, int tile_y);

// Empty when the tile is not part of a special building.
std::string special_query_at(const Micropolis *sim, int tile_x, int tile_y);

// Status line for the selected building, including its cost.
std::string special_status_text(int index);

// True when the 3x3 around a center tile is still the building we placed.
bool special_building_intact(const Micropolis *sim, int center_x, int center_y);

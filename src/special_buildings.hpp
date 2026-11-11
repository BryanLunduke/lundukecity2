// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <string>

class Micropolis;

// Special buildings are one row in this table. Placement, query, income,
// land value, the dropdown, and the map art all walk the table. A new
// building needs a center-tile id the simulator will not grow, a toolbar
// icon, and a map sprite. 1020-1022 are the classic free ids. Later
// landmarks reuse unused trash ids 53-55. Prison, Marina, and Fountain use
// 870-872, the gap after the tiny-explosion tiles. 1019 stays the
// unknown-tile check. The park animation at FOUNTAIN (840) is not a
// placeable building.

struct SpecialBuilding {
    int center_tile = 0;
    const char *name = "";
    // 40px toolbar / dropdown icon, under tool-icons/. Stem only: the
    // Spooky and SVGA files add -spooky and -svga, and Big Park's stem
    // stays bigpark with no hyphen.
    const char *icon = "";
    // Map sprite under special-tiles/, one file per look. A 3x3 sheet is
    // 48px, a 4x4 sheet is 64px, and a 1x1 sheet is 16px.
    const char *map_sprite = "";
    int cost = 100;
    // Added to the treasury each January. 0 means no payout.
    int annual_income = 0;
    int width = 3;
    int height = 3;
    // Chebyshev radius, in tiles, of the land-value change.
    int land_radius = 6;
    // Added to developed land-value cells (the engine scale is 1..250).
    // The SNES manuals say the bump is sharp and local, and do not publish
    // the formula. 48 inside 6 tiles is that reading. A negative value
    // lowers developed cells and does not go below 1.
    int land_bonus = 48;
    const char *effect = "";
    // 0 means this building does not change the crime map.
    int crime_radius = 0;
    // Added on the crime map after police coverage is subtracted.
    // A station nearby lowers a positive bonus (coverage / 8 comes off).
    // A negative bonus is a reduction and is not cut by a police station.
    int crime_bonus = 0;
    // 0 means the building can be bought at any population.
    int unlock_population = 0;
    // Buildings that share a cap_group count toward cap_limit together.
    // -1 means no cap.
    int cap_group = -1;
    int cap_limit = 0;
    // Tiles from the top-left to the click. 3x3 specials and the 4x4
    // stadium use 1. A single park-like tile uses 0.
    int anchor_x = 1;
    int anchor_y = 1;
    // Placement fails unless a road (the engine's roadTest, so rail counts)
    // touches the footprint.
    bool needs_road = false;
    // Placement fails unless a conductive tile touches the footprint. The
    // crime reduction waits until the power scan actually powers the center.
    bool needs_power = false;
    // The south row may cover shoreline. Open water must still sit
    // immediately south of the footprint. Bulldoze puts that shoreline
    // back, including after a disaster has erased the footprint.
    bool needs_south_water = false;
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
void boost_special_crime(Micropolis *sim);

// The census the status line shows. 0 when sim is null.
int special_population(const Micropolis *sim);

// Empty when this building may be placed. Otherwise the sentence the
// menu and the status line show (unlock or cap).
std::string special_block_reason(const Micropolis *sim, int index);

// Second line of the specials menu: the lock sentence, or "$100".
std::string special_menu_detail(const Micropolis *sim, int index);

// Click is the anchor tile. On failure, error is a short status-bar sentence
// and the map is unchanged.
bool place_special_building(Micropolis *sim, int index, int tile_x, int tile_y, std::string &error);

// Removes the whole footprint and charges $1, the bulldozer price. False when
// the tile is not part of a special building.
bool bulldoze_special_at(Micropolis *sim, int tile_x, int tile_y);

// Empty when the tile is not part of a special building.
std::string special_query_at(const Micropolis *sim, int tile_x, int tile_y);

// Status line for the selected building, including its cost.
std::string special_status_text(int index);
// Same line, plus the unlock or cap sentence when the city cannot buy it.
std::string special_status_text(const Micropolis *sim, int index);

// True when the footprint around a center tile is still the building we placed.
bool special_building_intact(const Micropolis *sim, int center_x, int center_y);

// word_at returns the raw map word. Off-map cells return a negative word.
using SpecialWordFn = int (*)(int x, int y, const void *ctx);

// Empty when the footprint at (left, top) meets the road, power, and water
// rules. The site-clear checks stay with the caller.
std::string special_requirement_at(int index, int left, int top, SpecialWordFn word_at, const void *ctx);

// Trees, rubble, and a park with the bulldoze bit. Roads, zones, water,
// fire, and the animated park fountain stay until the player clears them.
// Placement and the ghost preview both use this, so they cannot disagree.
bool special_auto_clearable(int raw);

// The lightning bolt replaces an unpowered special's centre tile while the
// zone blink is on. Body tiles and a powered centre stay on the sheet.
bool special_center_needs_bolt(int raw, int index, int cell, bool blink_on);

// True when (x, y) is inside a special. cell is the row-major sheet index.
bool special_sheet_at(int x, int y, SpecialWordFn word_at, const void *ctx, int &index, int &cell);

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "special_buildings.hpp"

#include "graph_legend.hpp"
#include "micropolis.h"

#include <string>
#include <vector>

bool special_auto_clearable(int raw)
{
    const int tile = raw & LOMASK;
    if (tile == DIRT) {
        return true;
    }
    if (tile >= RIVER && tile <= WATER_HIGH) {
        return false;
    }
    if (tile >= FLOOD && tile <= RADTILE) {
        return false;
    }
    if (tile >= FIRE && tile <= LASTFIRE) {
        return false;
    }
    if ((raw & ZONEBIT) != 0) {
        return false;
    }
    if (tile >= ROADBASE) {
        return false;
    }
    return (raw & BULLBIT) != 0;
}

bool special_center_needs_bolt(int raw, int index, int cell, bool blink_on)
{
    if (!blink_on || index < 0 || index >= special_building_count()) {
        return false;
    }
    if ((raw & ZONEBIT) == 0 || (raw & PWRBIT) != 0) {
        return false;
    }
    const SpecialBuilding &building = special_building(index);
    // An open-air landmark (Big Park, Fountain) never takes power, so it
    // never flashes for it. Every other special conducts like a zone.
    if (building.open_air) {
        return false;
    }
    const int center = building.anchor_y * building.width + building.anchor_x;
    return cell == center;
}

static_assert(SPECIAL_ZOO == 1020, "zoo center tile");
static_assert(SPECIAL_PARK == 1021, "big park center tile");
static_assert(SPECIAL_LIBRARY == 1022, "library center tile");
static_assert(SPECIAL_BODY == 1023, "special body tile");
static_assert(SPECIAL_PRISON == 870, "prison center tile");
static_assert(SPECIAL_MARINA == 871, "marina center tile");
static_assert(SPECIAL_FOUNTAIN == 872, "placeable fountain tile");
static_assert(SPECIAL_FOUNTAIN != FOUNTAIN, "park fountain animation stays on tile 840");
static_assert(kSpecialBodyTile == SPECIAL_BODY, "body tile constant");
static_assert(SPECIAL_DATACENTER_LOT == 873, "vacant data center lot tile");
static_assert(SPECIAL_MICRONUKE == 874, "micro nuclear plant tile");
static_assert(SPECIAL_DATACENTER == 875, "built data center tile");
static_assert(SPECIAL_DATACENTER_LOT > LASTTINYEXP + 2 && SPECIAL_DATACENTER < TINYEXPLAST,
              "data center and micro plant ids sit in the free gap the bulldozer does not treat as rubble");

namespace {

// City 2 rules, written up in the pull request. Zoo, Big Park, and Library
// stay at annual_income 0. Mayor's House, Casino, and Amusement Park follow
// the detailed gift FAQ's prices and the manual's benefits, with casino
// crime kept local (other guides, not the map-wide +1). Center ids 53-55
// are the unused trash slots; the map word cannot grow past 1023.
// Prison, Marina, and Fountain are a later first pass. Their center ids
// are the unused gap after the tiny explosions. Anchor 1 matches the
// stadium click (one tile down and right of the top-left). The fountain
// click is the tile itself, like a park. Big Park and Fountain are the
// open-air landmarks: no power. Everything else uses electricity, conducts
// like a zone, and needs a powered grid.
constexpr SpecialBuilding kBuildings[] = {
    {1020, "Zoo", "zoo", "zoo", 100, 0, 3, 3, 6, 48, "Raises nearby land value"},
    {1021, "Big Park", "bigpark", "big-park", 100, 0, 3, 3, 6, 48, "Raises nearby land value", 0, 0, 0, -1, 0, 1, 1,
     false, false, false, true},
    {1022, "Library", "library", "library", 100, 0, 3, 3, 6, 48, "Raises nearby land value"},
    {53, "Mayor's House", "mayor", "mayor", 100, 0, 3, 3, 6, 48,
     "Raises nearby land value. Best near residential", 0, 0, 2000, 1, 1},
    {54, "Casino", "casino", "casino", 100, 300, 3, 3, 6, 96,
     "Sharply raises nearby land value and local crime. Pays up to $300 a year with power, a road and 2,000 residents "
     "within 16 tiles. Nearby attractions share them. A police station nearby matters",
     8, 100, 0, 2, 3},
    // Review nov18b #2: the same +48 as the other $100 landmarks; its
    // income is what sets it apart.
    {55, "Amusement Park", "amusement", "amusement", 100, 200, 3, 3, 6, 48,
     "Raises nearby land value while powered. Pays up to $200 a year with power, a road and 2,000 residents within 16 "
     "tiles. Nearby attractions share them", 0, 0, 0, 2, 3},
    // Prison: the land-value dip (-16 in 6 tiles) is smaller than the crime
    // cut (-40 in 8 tiles). Crime is 128 - land value + density - police,
    // so a -48 dip used to add 48 crime and outweigh a -24 cut.
    {SPECIAL_PRISON, "Prison", "prison", "prison", 3000, 0, 4, 4, 6, -16,
     "Cuts crime across the neighbourhood while it has power and a road. Slightly lowers nearby land value", 8, -40,
     0, -1, 0, 1, 1, true, true, false},
    // Marina: ten times a Big Park's price buys a wider, stronger lift
    // (+64 within 10 tiles, about 2.6 times the area) and up to $100 a year
    // from the residents it serves, so it pays itself back in a decade.
    {SPECIAL_MARINA, "Marina", "marina", "marina", 1000, 100, 3, 3, 10, 64,
     "Raises land value across a wide area. Pays up to $100 a year with power, a road and 2,000 residents within 16 "
     "tiles. "
     "Nearby attractions share them. The south side is the dock", 0, 0, 0, -1, 0, 1, 1, false, false, true},
    {SPECIAL_FOUNTAIN, "Fountain", "fountain", "fountain", 20, 0, 1, 1, 4, 16, "Raises nearby land value", 0, 0, 0, -1,
     0, 0, 0, false, false, false, true},
    // Rows below are not landmarks; the landmarks are the rows above. The Data
    // Center is a zone, not a landmark: no land-value bump, no income.
    // The engine turns the lot into the built row once it has power and
    // counts a powered Data Center as one commercial plus one industrial
    // zone (DATACENTER_ZONE_DENSITY each). Neither building pollutes. The
    // micro plant is 1/9 of a nuclear plant and cannot melt down.
    {SPECIAL_DATACENTER_LOT, "Data Center", "datacenter", "datacenter-vacant", 400, 0, 3, 3, 0, 0,
     "Vacant lot. Opens once it has power, then counts as 1 industrial + 1 commercial zone. No pollution", 0, 0, 0,
     -1, 0, 1, 1, false, false, false, false, false},
    // $2,000 for 222 tiles is $9.01 a powered tile: about twice coal
    // ($3,000 / 700 = $4.29) and 3.6 times nuclear ($2.50). Coal stays the
    // cheap, dirty start; the micro is the clean premium with no meltdown.
    {SPECIAL_MICRONUKE, "Micro Nuclear Plant", "micronuclear", "micro-nuclear", 2000, 0, 1, 1, 0, 0,
     "Supplies 1/9 the power of a nuclear plant. Cannot melt down", 0, 0, 0, -1, 0, 0, 0, false, false, false, false,
     false},
    {SPECIAL_DATACENTER, "Data Center", "datacenter", "datacenter", 400, 0, 3, 3, 0, 0,
     "Counts as 1 industrial + 1 commercial zone while powered. No pollution", 0, 0, 0, -1, 0, 1, 1, false, false,
     false, false, false},
};

constexpr int kBuildingRows = static_cast<int>(sizeof(kBuildings) / sizeof(kBuildings[0]));
static_assert(kBuildingRows <= kSpecialSpriteSlots, "MapView caches one sheet per row; raise kSpecialSpriteSlots");

constexpr int count_menu_rows()
{
    int rows = 0;
    while (rows < kBuildingRows && kBuildings[rows].in_menu) {
        ++rows;
    }
    return rows;
}
constexpr bool menu_rows_lead()
{
    for (int i = count_menu_rows(); i < kBuildingRows; ++i) {
        if (kBuildings[i].in_menu) {
            return false;
        }
    }
    return true;
}
static_assert(menu_rows_lead(), "the landmark rows come first");

int row_for_tile(int tile)
{
    for (int i = 0; i < kBuildingRows; ++i) {
        if (kBuildings[i].center_tile == tile) {
            return i;
        }
    }
    return -1;
}

bool in_bounds(int x, int y)
{
    return x >= 0 && y >= 0 && x < WORLD_W && y < WORLD_H;
}

int tile_of(const Micropolis *sim, int x, int y)
{
    if (sim == nullptr || !in_bounds(x, y)) {
        return -1;
    }
    return static_cast<int>(sim->map[x][y] & LOMASK);
}

int sim_word(int x, int y, const void *ctx)
{
    const Micropolis *sim = static_cast<const Micropolis *>(ctx);
    if (sim == nullptr || !in_bounds(x, y)) {
        return -1;
    }
    return static_cast<int>(sim->map[x][y]);
}

bool is_center_at(const Micropolis *sim, int x, int y, int index)
{
    if (index < 0 || index >= special_building_count()) {
        return false;
    }
    const int raw = in_bounds(x, y) ? static_cast<int>(sim->map[x][y]) : 0;
    return (raw & ZONEBIT) != 0 && (raw & LOMASK) == kBuildings[index].center_tile;
}

bool is_water_tile(int tile)
{
    if (tile >= RIVER && tile <= WATER_HIGH) {
        return true;
    }
    return tile >= FLOOD && tile <= LASTFLOOD;
}

bool water_word(int raw)
{
    if (raw < 0) {
        return false;
    }
    return is_water_tile(raw & LOMASK);
}

// Same acceptance as Micropolis::roadTest: roads and rail, not a bare wire.
bool road_word(int raw)
{
    if (raw < 0) {
        return false;
    }
    const int tile = raw & LOMASK;
    if (tile < ROADBASE || tile > LASTRAIL) {
        return false;
    }
    if (tile >= POWERBASE && tile < LASTPOWER) {
        return false;
    }
    return true;
}

int search_reach()
{
    int reach = 1;
    for (int i = 0; i < special_building_count(); ++i) {
        const SpecialBuilding &building = kBuildings[i];
        const int right = building.width - 1 - building.anchor_x;
        const int down = building.height - 1 - building.anchor_y;
        if (building.anchor_x > reach) {
            reach = building.anchor_x;
        }
        if (building.anchor_y > reach) {
            reach = building.anchor_y;
        }
        if (right > reach) {
            reach = right;
        }
        if (down > reach) {
            reach = down;
        }
    }
    return reach;
}

struct Located {
    int index = -1;
    int center_x = 0;
    int center_y = 0;
    int left = 0;
    int top = 0;
};

bool locate_at(int x, int y, SpecialWordFn word_at, const void *ctx, Located &found)
{
    if (word_at == nullptr) {
        return false;
    }
    const int reach = search_reach();
    int best_dist = 1000;
    bool hit = false;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const int sx = x + dx;
            const int sy = y + dy;
            const int raw = word_at(sx, sy, ctx);
            if (raw < 0 || (raw & ZONEBIT) == 0) {
                continue;
            }
            const int index = special_index_for_tile(raw & LOMASK);
            if (index < 0) {
                continue;
            }
            const SpecialBuilding &building = kBuildings[index];
            const int left = sx - building.anchor_x;
            const int top = sy - building.anchor_y;
            if (x < left || y < top || x >= left + building.width || y >= top + building.height) {
                continue;
            }
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            const int dist = adx > ady ? adx : ady;
            if (hit && dist >= best_dist) {
                continue;
            }
            hit = true;
            best_dist = dist;
            found.index = index;
            found.center_x = sx;
            found.center_y = sy;
            found.left = left;
            found.top = top;
        }
    }
    return hit;
}

bool ring_has(int left, int top, int width, int height, SpecialWordFn word_at, const void *ctx, bool (*test)(int))
{
    for (int y = top - 1; y <= top + height; ++y) {
        for (int x = left - 1; x <= left + width; ++x) {
            if (x >= left && x < left + width && y >= top && y < top + height) {
                continue;
            }
            if (test(word_at(x, y, ctx))) {
                return true;
            }
        }
    }
    return false;
}

bool south_water(int left, int top, int width, int height, SpecialWordFn word_at, const void *ctx)
{
    const int y = top + height;
    for (int x = left; x < left + width; ++x) {
        if (water_word(word_at(x, y, ctx))) {
            return true;
        }
    }
    return false;
}

bool remnant_tile(int raw);

// The marina's painted docks are the south row. That row may replace
// shoreline. The rows above it stay on land.
bool dock_row(const SpecialBuilding &building, int dy)
{
    return building.needs_south_water && building.height > 0 && dy == building.height - 1;
}

void forget_dock_at(Micropolis *sim, int x, int y)
{
    std::vector<Micropolis::MarinaDock> &list = sim->marinaDock;
    for (std::size_t i = 0; i < list.size();) {
        if (list[i].x == x && list[i].y == y) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void remember_dock(Micropolis *sim, int x, int y, unsigned short tile)
{
    forget_dock_at(sim, x, y);
    Micropolis::MarinaDock note;
    note.x = static_cast<unsigned short>(x);
    note.y = static_cast<unsigned short>(y);
    note.tile = tile;
    sim->marinaDock.push_back(note);
}

// Puts the shoreline word back and drops the note. False when this cell
// was land (or was never recorded).
bool restore_dock(Micropolis *sim, int x, int y)
{
    std::vector<Micropolis::MarinaDock> &list = sim->marinaDock;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].x != x || list[i].y != y) {
            continue;
        }
        sim->map[x][y] = list[i].tile;
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

void settle_cell(Micropolis *sim, int x, int y, bool rubble_rest)
{
    if (!in_bounds(x, y)) {
        return;
    }
    if (restore_dock(sim, x, y)) {
        return;
    }
    if (!rubble_rest && !remnant_tile(static_cast<int>(sim->map[x][y]))) {
        return;
    }
    sim->map[x][y] = static_cast<unsigned short>(RUBBLE | BULLBIT);
}

// Shoreline art on the water row just south of a marina. Water edge tiles
// carry a strip of beach on every side that faced land when the map was
// made. A marina built on the beach left that strip between its docks and
// the open water. This is the engine's smoothRiver table (west, south, east,
// north), with the docks above counted as water, so
// open water now meets the docks. The edge variant (+1) is kept so the
// water does not reshuffle. Bulldozing runs it again with the real tile
// above, which puts the beach back.
// The engine's test: anything but dirt and woods counts as water.
bool shore_water_at(const Micropolis *sim, int x, int y)
{
    if (!in_bounds(x, y)) {
        return false;
    }
    const int tile = static_cast<int>(sim->map[x][y] & LOMASK);
    return tile != DIRT && !(tile >= WOODS_LOW && tile <= WOODS_HIGH);
}

void reedge_below(Micropolis *sim, int left, int top, int width, int height, bool docks_above)
{
    static const int kEdgeTable[16] = {13, 13, 17, 15, 5, RIVER, 19, 17, 9, 11, RIVER, 13, 7, 9, 5, RIVER};
    const int y = top + height;
    for (int x = left; x < left + width; ++x) {
        if (!in_bounds(x, y)) {
            continue;
        }
        const int tile = static_cast<int>(sim->map[x][y] & LOMASK);
        if (tile < FIRSTRIVEDGE || tile > LASTRIVEDGE) {
            continue;
        }
        int bits = 0;
        bits = (bits << 1) | (shore_water_at(sim, x - 1, y) ? 1 : 0);
        bits = (bits << 1) | (shore_water_at(sim, x, y + 1) ? 1 : 0);
        bits = (bits << 1) | (shore_water_at(sim, x + 1, y) ? 1 : 0);
        // Above is the old footprint. After a bulldoze only real water there
        // (the restored shoreline) counts. Rubble is the beach again.
        bits = (bits << 1) | ((docks_above || (in_bounds(x, y - 1) && is_water_tile(tile_of(sim, x, y - 1)))) ? 1 : 0);
        int next = kEdgeTable[bits & 15];
        if (next == RIVER) {
            sim->map[x][y] = static_cast<unsigned short>(RIVER);
            continue;
        }
        if (((tile - FIRSTRIVEDGE) & 1) != 0) {
            ++next;
        }
        sim->map[x][y] = static_cast<unsigned short>(next | BULLBIT);
    }
}

bool live_center_at(const Micropolis *sim, int x, int y)
{
    if (!in_bounds(x, y)) {
        return false;
    }
    const int raw = static_cast<int>(sim->map[x][y]);
    return (raw & ZONEBIT) != 0 && special_index_for_tile(raw & LOMASK) >= 0;
}

bool remnant_tile(int raw)
{
    const int tile = raw & LOMASK;
    if (tile == kSpecialBodyTile || special_index_for_tile(tile) >= 0) {
        return true;
    }
    if (tile >= FIRE && tile <= LASTFIRE) {
        return true;
    }
    return tile >= RUBBLE && tile <= LASTRUBBLE;
}

bool find_center(const Micropolis *sim, int x, int y, int &cx, int &cy, int &index)
{
    Located found;
    if (!locate_at(x, y, sim_word, sim, found)) {
        return false;
    }
    cx = found.center_x;
    cy = found.center_y;
    index = found.index;
    return true;
}

bool orphan_body(const Micropolis *sim, int x, int y)
{
    if (tile_of(sim, x, y) != kSpecialBodyTile) {
        return false;
    }
    int cx = 0;
    int cy = 0;
    int index = -1;
    return !find_center(sim, x, y, cx, cy, index);
}

struct Remnant {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    int anchor_x = 1;
    int anchor_y = 1;
};

// The footprint that still holds the clicked remnant after its centre is gone.
// A window that contains another building's centre is rejected. Equal body
// counts keep the smaller window, so a burned 3x3 is not cleared as a 4x4.
bool orphan_footprint(const Micropolis *sim, int x, int y, Remnant &remnant)
{
    int best_bodies = -1;
    int best_center = -1;
    int best_area = 100000;
    bool hit = false;
    for (int index = 0; index < special_building_count(); ++index) {
        const SpecialBuilding &building = kBuildings[index];
        for (int top = y - (building.height - 1); top <= y; ++top) {
            for (int left = x - (building.width - 1); left <= x; ++left) {
                if (left < 0 || top < 0 || left + building.width > WORLD_W || top + building.height > WORLD_H) {
                    continue;
                }
                if (x < left || x >= left + building.width || y < top || y >= top + building.height) {
                    continue;
                }
                int bodies = 0;
                bool foreign = false;
                for (int dy = 0; dy < building.height && !foreign; ++dy) {
                    for (int dx = 0; dx < building.width; ++dx) {
                        const int sx = left + dx;
                        const int sy = top + dy;
                        if (live_center_at(sim, sx, sy)) {
                            foreign = true;
                            break;
                        }
                        if (tile_of(sim, sx, sy) != kSpecialBodyTile) {
                            continue;
                        }
                        if (!orphan_body(sim, sx, sy)) {
                            foreign = true;
                            break;
                        }
                        ++bodies;
                    }
                }
                if (foreign || bodies < 1) {
                    continue;
                }
                const int center_tile = tile_of(sim, left + building.anchor_x, top + building.anchor_y);
                int center_score = 0;
                if ((center_tile >= FIRE && center_tile <= LASTFIRE) ||
                    (center_tile >= RUBBLE && center_tile <= LASTRUBBLE) || special_index_for_tile(center_tile) >= 0) {
                    center_score = 1;
                }
                const int area = building.width * building.height;
                const bool better = bodies > best_bodies || (bodies == best_bodies && center_score > best_center) ||
                                    (bodies == best_bodies && center_score == best_center && area < best_area);
                if (!better) {
                    continue;
                }
                hit = true;
                best_bodies = bodies;
                best_center = center_score;
                best_area = area;
                remnant.left = left;
                remnant.top = top;
                remnant.width = building.width;
                remnant.height = building.height;
                remnant.anchor_x = building.anchor_x;
                remnant.anchor_y = building.anchor_y;
            }
        }
    }
    return hit;
}

void clear_remnant(Micropolis *sim, int left, int top, int width, int height)
{
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            settle_cell(sim, left + dx, top + dy, false);
        }
    }
}

bool crime_service_ready(const Micropolis *sim, int index, int center_x, int center_y)
{
    const SpecialBuilding &building = kBuildings[index];
    if (!building.needs_power && !building.needs_road) {
        return true;
    }
    const int left = center_x - building.anchor_x;
    const int top = center_y - building.anchor_y;
    if (building.needs_power) {
        if (!in_bounds(center_x, center_y) || (sim->map[center_x][center_y] & PWRBIT) == 0) {
            return false;
        }
    }
    if (building.needs_road && !ring_has(left, top, building.width, building.height, sim_word, sim, road_word)) {
        return false;
    }
    return true;
}

// A landmark that uses electricity only lifts land value (or draws a crowd
// and its crime) while the power scan powers its centre. An open-air one
// (Big Park, Fountain) always works. Review nov18b #2/#5.
bool landmark_running(const Micropolis *sim, int index, int center_x, int center_y)
{
    if (kBuildings[index].open_air) {
        return true;
    }
    return in_bounds(center_x, center_y) && (sim->map[center_x][center_y] & PWRBIT) != 0;
}

bool explosion_tile(int tile)
{
    return tile >= TINYEXP && tile <= LASTTINYEXP;
}

bool wrecked_tile(int tile)
{
    if (explosion_tile(tile)) {
        return true;
    }
    if (tile >= FIRE && tile <= LASTFIRE) {
        return true;
    }
    return tile >= RUBBLE && tile <= LASTRUBBLE;
}

bool dock_note_at(const Micropolis *sim, int x, int y)
{
    for (const Micropolis::MarinaDock &note : sim->marinaDock) {
        if (note.x == x && note.y == y) {
            return true;
        }
    }
    return false;
}

// A crash can replace every marina cell, including the centre, with an
// explosion or with the rubble that explosion becomes. Nothing with
// ZONEBIT or SPECIAL_BODY is left, so the usual search misses the lot.
// The dock notes still name the shoreline. One bulldoze of any wrecked
// cell in that footprint puts the shore back and clears the rest.
bool bulldoze_dock_wreck(Micropolis *sim, int tile_x, int tile_y)
{
    if (!wrecked_tile(tile_of(sim, tile_x, tile_y))) {
        return false;
    }
    int best_notes = 0;
    int best_wrecked = -1;
    int best_dist = 1000;
    bool hit = false;
    int best_left = 0;
    int best_top = 0;
    int best_width = 0;
    int best_height = 0;
    for (int index = 0; index < special_building_count(); ++index) {
        const SpecialBuilding &building = kBuildings[index];
        if (!building.needs_south_water) {
            continue;
        }
        for (int top = tile_y - (building.height - 1); top <= tile_y; ++top) {
            for (int left = tile_x - (building.width - 1); left <= tile_x; ++left) {
                if (left < 0 || top < 0 || left + building.width > WORLD_W || top + building.height > WORLD_H) {
                    continue;
                }
                int notes = 0;
                int wrecked = 0;
                int bodies = 0;
                bool foreign = false;
                for (int dy = 0; dy < building.height && !foreign; ++dy) {
                    for (int dx = 0; dx < building.width; ++dx) {
                        const int sx = left + dx;
                        const int sy = top + dy;
                        if (live_center_at(sim, sx, sy)) {
                            foreign = true;
                            break;
                        }
                        const int tile = tile_of(sim, sx, sy);
                        if (tile == kSpecialBodyTile) {
                            ++bodies;
                        }
                        if (wrecked_tile(tile) || tile == kSpecialBodyTile || special_index_for_tile(tile) >= 0) {
                            ++wrecked;
                        }
                        if (dock_row(building, dy) && dock_note_at(sim, sx, sy) &&
                            (tile == kSpecialBodyTile || wrecked_tile(tile))) {
                            ++notes;
                        }
                    }
                }
                // A standing body tile still identifies the lot. Leave that
                // to the ordinary remnant search so a shifted window cannot
                // claim a neighbour.
                if (foreign || notes < 1 || bodies > 0) {
                    continue;
                }
                const int cx = left + building.anchor_x;
                const int cy = top + building.anchor_y;
                int adx = tile_x - cx;
                int ady = tile_y - cy;
                if (adx < 0) {
                    adx = -adx;
                }
                if (ady < 0) {
                    ady = -ady;
                }
                const int dist = adx > ady ? adx : ady;
                const bool better = notes > best_notes || (notes == best_notes && wrecked > best_wrecked) ||
                                    (notes == best_notes && wrecked == best_wrecked && dist < best_dist);
                if (!better) {
                    continue;
                }
                hit = true;
                best_notes = notes;
                best_wrecked = wrecked;
                best_dist = dist;
                best_left = left;
                best_top = top;
                best_width = building.width;
                best_height = building.height;
            }
        }
    }
    if (!hit || sim->totalFunds < 1) {
        return false;
    }
    for (int dy = 0; dy < best_height; ++dy) {
        for (int dx = 0; dx < best_width; ++dx) {
            settle_cell(sim, best_left + dx, best_top + dy, true);
        }
    }
    reedge_below(sim, best_left, best_top, best_width, best_height, false);
    sim->spend(1);
    sim->invalidateMaps();
    return true;
}

} // namespace

int special_building_count()
{
    return kBuildingRows;
}

int special_menu_count()
{
    return count_menu_rows();
}

bool tile_is_power_plant(int center_tile)
{
    return center_tile == POWERPLANT || center_tile == NUCLEAR || center_tile == SPECIAL_MICRONUKE;
}

int special_datacenter_lot_index()
{
    return row_for_tile(SPECIAL_DATACENTER_LOT);
}

int special_datacenter_index()
{
    return row_for_tile(SPECIAL_DATACENTER);
}

int special_micro_nuclear_index()
{
    return row_for_tile(SPECIAL_MICRONUKE);
}

bool special_is_datacenter(int index)
{
    return index >= 0 && (index == special_datacenter_lot_index() || index == special_datacenter_index());
}

const SpecialBuilding &special_building(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return kBuildings[0];
    }
    return kBuildings[index];
}

bool special_tile_skips_power(int tile)
{
    const int index = special_index_for_tile(tile);
    return index >= 0 && special_building(index).open_air;
}

bool special_uses_power(int index)
{
    return index >= 0 && index < special_building_count() && !special_building(index).open_air;
}

int special_index_for_tile(int tile)
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (kBuildings[i].center_tile == tile) {
            return i;
        }
    }
    return -1;
}

bool special_building_intact(const Micropolis *sim, int center_x, int center_y)
{
    if (sim == nullptr) {
        return false;
    }
    const int index = special_index_for_tile(tile_of(sim, center_x, center_y));
    if (!is_center_at(sim, center_x, center_y, index)) {
        return false;
    }
    const SpecialBuilding &building = kBuildings[index];
    const int left = center_x - building.anchor_x;
    const int top = center_y - building.anchor_y;
    if (left < 0 || top < 0 || left + building.width > WORLD_W || top + building.height > WORLD_H) {
        return false;
    }
    for (int dy = 0; dy < building.height; ++dy) {
        for (int dx = 0; dx < building.width; ++dx) {
            if (dx == building.anchor_x && dy == building.anchor_y) {
                continue;
            }
            if (tile_of(sim, left + dx, top + dy) != kSpecialBodyTile) {
                return false;
            }
        }
    }
    return true;
}

std::string special_requirement_at(int index, int left, int top, SpecialWordFn word_at, const void *ctx)
{
    if (word_at == nullptr || index < 0 || index >= special_building_count()) {
        return "Cannot build here.";
    }
    const SpecialBuilding &building = kBuildings[index];
    if (building.needs_south_water && !south_water(left, top, building.width, building.height, word_at, ctx)) {
        return "Needs a shoreline to the south.";
    }
    // The Prison used to refuse ground without a road and a wire while the
    // Casino and Data Center placed there and waited. Every special now
    // places the same way; crime_service_ready still holds the Prison's
    // crime cut until it has power and a road.
    return {};
}

bool special_sheet_at(int x, int y, SpecialWordFn word_at, const void *ctx, int &index, int &cell)
{
    Located found;
    if (!locate_at(x, y, word_at, ctx, found)) {
        return false;
    }
    const SpecialBuilding &building = kBuildings[found.index];
    index = found.index;
    const int ox = x - found.left;
    const int oy = y - found.top;
    cell = oy * building.width + ox;
    return true;
}

static int count_group(const Micropolis *sim, int group)
{
    int total = 0;
    if (sim == nullptr || group < 0) {
        return 0;
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int raw = static_cast<int>(sim->map[x][y]);
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int index = special_index_for_tile(raw & LOMASK);
            if (index < 0) {
                continue;
            }
            if (kBuildings[index].cap_group == group) {
                ++total;
            }
        }
    }
    return total;
}

int special_population(const Micropolis *sim)
{
    if (sim == nullptr) {
        return 0;
    }
    const Quad res = sim->censusSnapshotValid ? sim->snapResPop : sim->resPop;
    const Quad com = sim->censusSnapshotValid ? sim->snapComPop : sim->comPop;
    const Quad ind = sim->censusSnapshotValid ? sim->snapIndPop : sim->indPop;
    const Quad dc = sim->censusSnapshotValid ? sim->snapDataCenterPop : sim->dataCenterPop;
    // Data Centers are jobs, not people: they cannot unlock Mayor's House.
    const long long people = cityPopulationOf(res, com, ind, dc);
    if (people < 0) {
        return 0;
    }
    if (people > 2000000000LL) {
        return 2000000000;
    }
    return static_cast<int>(people);
}

// The lock sentences come from the table, so a new threshold or cap reads
// right without touching the text.
std::string special_unlock_sentence(int population)
{
    std::string text = "Available at " + grouped_number(population) + " population";
    // The engine's city classes (Micropolis::doPopNum): Town from 2,000,
    // City from 10,000, Capital 50,000, Metropolis 100,000, Megalopolis 500,000.
    const char *klass = population >= 500000   ? "Megalopolis"
                        : population >= 100000 ? "Metropolis"
                        : population >= 50000  ? "Capital"
                        : population >= 10000  ? "City"
                        : population >= 2000   ? "Town"
                                               : nullptr;
    if (klass != nullptr) {
        text += std::string(" (") + klass + ")";
    }
    return text;
}

std::string special_cap_sentence(int index)
{
    if (index < 0 || index >= kBuildingRows || kBuildings[index].cap_group < 0 || kBuildings[index].cap_limit <= 0) {
        return {};
    }
    const SpecialBuilding &building = kBuildings[index];
    if (building.cap_limit == 1) {
        return std::string("Only one ") + building.name + " in a city.";
    }
    // Every row in the group, newest row first ("Amusement Parks and Casinos").
    std::string names;
    int listed = 0;
    for (int i = kBuildingRows - 1; i >= 0; --i) {
        if (kBuildings[i].cap_group != building.cap_group) {
            continue;
        }
        if (listed > 0) {
            names += " and ";
        }
        names += std::string(kBuildings[i].name) + "s";
        ++listed;
    }
    return names + (listed > 1 ? " are" : " is") + " limited to " + std::to_string(building.cap_limit) + ".";
}

std::string special_block_reason(const Micropolis *sim, int index)
{
    if (sim == nullptr || index < 0 || index >= special_building_count()) {
        return "Cannot build here.";
    }
    const SpecialBuilding &building = kBuildings[index];
    if (building.unlock_population > 0 && special_population(sim) < building.unlock_population) {
        return special_unlock_sentence(building.unlock_population);
    }
    if (building.cap_group >= 0 && building.cap_limit > 0 && count_group(sim, building.cap_group) >= building.cap_limit) {
        return special_cap_sentence(index);
    }
    return {};
}

std::string special_menu_detail(const Micropolis *sim, int index)
{
    const std::string block = special_block_reason(sim, index);
    if (!block.empty() && block != "Cannot build here.") {
        return block;
    }
    if (index < 0 || index >= special_building_count()) {
        return {};
    }
    return graph_money(kBuildings[index].cost);
}

namespace {

// People living in one residential zone, by its centre tile. 0 for any
// other tile. One residential census unit is 20 people (getPopulation).
long long zone_people(const Micropolis *sim, int x, int y)
{
    const int raw = static_cast<int>(sim->map[x][y]);
    if ((raw & ZONEBIT) == 0) {
        return 0;
    }
    const int tile = raw & LOMASK;
    long long units = 0;
    if (tile == FREEZ) {
        // An empty residential zone's single houses, one unit each
        // (Micropolis::doFreePop).
        for (int hx = x - 1; hx <= x + 1; ++hx) {
            for (int hy = y - 1; hy <= y + 1; ++hy) {
                const int house = tile_of(sim, hx, hy);
                if (house >= LHTHR && house <= HHTHR) {
                    ++units;
                }
            }
        }
    } else if (tile >= RZB && tile < HOSPITALBASE) {
        // Micropolis::getResZonePop: 16, 24, 32 or 40 by density.
        units = ((tile - RZB) / 9) % 4 * 8 + 16;
    }
    return units * 20;
}

struct IncomeSite {
    int x = 0;
    int y = 0;
    SpecialIncome income;
    double served = 0.0; // residents after sharing
};

int chebyshev(int ax, int ay, int bx, int by)
{
    const int dx = ax > bx ? ax - bx : bx - ax;
    const int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// Every income building on the map with its January payout. Residents are
// shared: a home inside the catchment of k working attractions counts 1/k
// toward each, so stacking attractions on one neighborhood splits its
// visitors instead of multiplying them. The total is then capped
// (special_income_cap) and every site scaled down by the same factor.
std::vector<IncomeSite> income_sites(const Micropolis *sim)
{
    std::vector<IncomeSite> sites;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int raw = static_cast<int>(sim->map[x][y]);
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int index = special_index_for_tile(raw & LOMASK);
            if (index < 0 || kBuildings[index].annual_income <= 0) {
                continue;
            }
            const SpecialBuilding &building = kBuildings[index];
            IncomeSite site;
            site.x = x;
            site.y = y;
            site.income.earns = true;
            site.income.full = building.annual_income;
            site.income.intact = special_building_intact(sim, x, y);
            site.income.powered = building.open_air || (raw & PWRBIT) != 0;
            site.income.road = ring_has(x - building.anchor_x, y - building.anchor_y, building.width,
                                        building.height, sim_word, sim, road_word);
            site.income.residents = special_residents_near(sim, x, y, kIncomeCatchmentRadius);
            sites.push_back(site);
        }
    }
    auto working = [](const IncomeSite &site) {
        return site.income.intact && site.income.powered && site.income.road;
    };
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const long long people = zone_people(sim, x, y);
            if (people <= 0) {
                continue;
            }
            int sharing = 0;
            for (const IncomeSite &site : sites) {
                if (working(site) && chebyshev(site.x, site.y, x, y) <= kIncomeCatchmentRadius) {
                    ++sharing;
                }
            }
            if (sharing == 0) {
                continue;
            }
            for (IncomeSite &site : sites) {
                if (working(site) && chebyshev(site.x, site.y, x, y) <= kIncomeCatchmentRadius) {
                    site.served += static_cast<double>(people) / sharing;
                    site.income.others = sharing - 1 > site.income.others ? sharing - 1 : site.income.others;
                }
            }
        }
    }
    long long total = 0;
    for (IncomeSite &site : sites) {
        if (!working(site)) {
            continue;
        }
        double served = site.served < kIncomeFullResidents ? site.served : kIncomeFullResidents;
        site.income.shared = static_cast<int>(site.served + 0.5);
        site.income.dollars = static_cast<int>(site.income.full * served / kIncomeFullResidents);
        total += site.income.dollars;
    }
    const int cap = special_income_cap(sim);
    if (total > cap) {
        for (IncomeSite &site : sites) {
            site.income.cap = cap;
            if (!working(site)) {
                continue;
            }
            site.income.capped = true;
            site.income.dollars = static_cast<int>(static_cast<long long>(site.income.dollars) * cap / total);
        }
    } else {
        for (IncomeSite &site : sites) {
            site.income.cap = cap;
        }
    }
    return sites;
}

} // namespace

int special_residents_near(const Micropolis *sim, int center_x, int center_y, int radius)
{
    if (sim == nullptr || radius < 0) {
        return 0;
    }
    long long people = 0;
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
        for (int y = center_y - radius; y <= center_y + radius; ++y) {
            if (in_bounds(x, y)) {
                people += zone_people(sim, x, y);
            }
        }
    }
    return people > 2000000000LL ? 2000000000 : static_cast<int>(people);
}

int special_income_cap(const Micropolis *sim)
{
    if (sim == nullptr) {
        return kIncomeCapFloor;
    }
    const long long share = static_cast<long long>(sim->taxFund) * kIncomeCapTaxPercent / 100;
    return share > kIncomeCapFloor ? static_cast<int>(share > 2000000000LL ? 2000000000LL : share) : kIncomeCapFloor;
}

SpecialIncome special_income_at(const Micropolis *sim, int center_x, int center_y)
{
    SpecialIncome income;
    if (sim == nullptr || !in_bounds(center_x, center_y)) {
        return income;
    }
    const int raw = static_cast<int>(sim->map[center_x][center_y]);
    const int index = (raw & ZONEBIT) != 0 ? special_index_for_tile(raw & LOMASK) : -1;
    if (index < 0 || kBuildings[index].annual_income <= 0) {
        return income;
    }
    for (const IncomeSite &site : income_sites(sim)) {
        if (site.x == center_x && site.y == center_y) {
            return site.income;
        }
    }
    return income;
}

int special_annual_income(const Micropolis *sim)
{
    if (sim == nullptr) {
        return 0;
    }
    long long total = 0;
    for (const IncomeSite &site : income_sites(sim)) {
        total += site.income.dollars;
    }
    return total > 2000000000LL ? 2000000000 : static_cast<int>(total);
}

void boost_special_land_value(Micropolis *sim)
{
    if (sim == nullptr) {
        return;
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int index = special_index_for_tile(tile_of(sim, x, y));
            if (index < 0 || !special_building_intact(sim, x, y)) {
                continue;
            }
            const SpecialBuilding &building = kBuildings[index];
            if (building.land_bonus == 0 || building.land_radius <= 0) {
                continue;
            }
            // A dip (the Prison) stays while the building stands; a lift
            // needs the building to be running.
            if (building.land_bonus > 0 && !landmark_running(sim, index, x, y)) {
                continue;
            }
            const int x0 = (x - building.land_radius) / sim->landValueMap.MAP_BLOCKSIZE;
            const int y0 = (y - building.land_radius) / sim->landValueMap.MAP_BLOCKSIZE;
            const int x1 = (x + building.land_radius) / sim->landValueMap.MAP_BLOCKSIZE;
            const int y1 = (y + building.land_radius) / sim->landValueMap.MAP_BLOCKSIZE;
            for (int by = y0; by <= y1; ++by) {
                for (int bx = x0; bx <= x1; ++bx) {
                    if (!sim->landValueMap.onMap(bx, by)) {
                        continue;
                    }
                    const int value = sim->landValueMap.get(bx, by);
                    if (value <= 0) {
                        continue;
                    }
                    int raised = value + building.land_bonus;
                    if (raised > 250) {
                        raised = 250;
                    }
                    if (building.land_bonus < 0 && raised < 1) {
                        raised = 1;
                    }
                    sim->landValueMap.set(bx, by, static_cast<Byte>(raised));
                }
            }
        }
    }
}

void boost_special_crime(Micropolis *sim)
{
    if (sim == nullptr) {
        return;
    }
    const int block = sim->crimeRateMap.MAP_BLOCKSIZE;
    if (block < 1) {
        return;
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int index = special_index_for_tile(tile_of(sim, x, y));
            if (index < 0 || !special_building_intact(sim, x, y)) {
                continue;
            }
            const SpecialBuilding &building = kBuildings[index];
            if (building.crime_bonus == 0 || building.crime_radius <= 0) {
                continue;
            }
            if (building.crime_bonus < 0 && !crime_service_ready(sim, index, x, y)) {
                continue;
            }
            if (building.crime_bonus > 0 && !landmark_running(sim, index, x, y)) {
                continue;
            }
            const int x0 = (x - building.crime_radius) / block;
            const int y0 = (y - building.crime_radius) / block;
            const int x1 = (x + building.crime_radius) / block;
            const int y1 = (y + building.crime_radius) / block;
            for (int by = y0; by <= y1; ++by) {
                for (int bx = x0; bx <= x1; ++bx) {
                    if (!sim->crimeRateMap.onMap(bx, by)) {
                        continue;
                    }
                    const int wx = bx * block + block / 2;
                    const int wy = by * block + block / 2;
                    int dx = wx - x;
                    int dy = wy - y;
                    if (dx < 0) {
                        dx = -dx;
                    }
                    if (dy < 0) {
                        dy = -dy;
                    }
                    const int dist = dx > dy ? dx : dy;
                    if (dist > building.crime_radius) {
                        continue;
                    }
                    int add = building.crime_bonus;
                    if (add > 0) {
                        int coverage = sim->policeStationMap.worldGet(wx, wy);
                        if (coverage < 0) {
                            coverage = 0;
                        }
                        add = building.crime_bonus - coverage / 8;
                        if (add < 0) {
                            add = 0;
                        }
                    }
                    int current = sim->crimeRateMap.worldGet(wx, wy);
                    if (current < 0) {
                        current = 0;
                    }
                    int raised = current + add;
                    if (raised > 250) {
                        raised = 250;
                    }
                    if (raised < 0) {
                        raised = 0;
                    }
                    sim->crimeRateMap.worldSet(wx, wy, static_cast<Byte>(raised));
                }
            }
        }
    }
}

bool place_special_building(Micropolis *sim, int index, int tile_x, int tile_y, std::string &error)
{
    error.clear();
    if (sim == nullptr || index < 0 || index >= special_building_count()) {
        error = "Cannot build here.";
        return false;
    }
    const SpecialBuilding &building = kBuildings[index];
    const int left = tile_x - building.anchor_x;
    const int top = tile_y - building.anchor_y;
    if (left < 0 || top < 0 || left + building.width > WORLD_W || top + building.height > WORLD_H) {
        error = "That would be off the map.";
        return false;
    }
    bool blocked_by_water = false;
    bool blocked_by_land = false;
    for (int dy = 0; dy < building.height; ++dy) {
        for (int dx = 0; dx < building.width; ++dx) {
            const int raw = static_cast<int>(sim->map[left + dx][top + dy]);
            const int tile = raw & LOMASK;
            if (tile == DIRT) {
                continue;
            }
            // Dock art covers shoreline. Water further north is still refused.
            if (dock_row(building, dy) && is_water_tile(tile)) {
                continue;
            }
            if (sim->autoBulldoze && special_auto_clearable(raw)) {
                continue;
            }
            if (is_water_tile(tile)) {
                blocked_by_water = true;
            } else {
                blocked_by_land = true;
            }
        }
    }
    if (blocked_by_water && !blocked_by_land) {
        error = "Can't build on water.";
        return false;
    }
    if (blocked_by_water || blocked_by_land) {
        error = "Bulldoze that area first.";
        return false;
    }
    const std::string required = special_requirement_at(index, left, top, sim_word, sim);
    if (!required.empty()) {
        error = required;
        return false;
    }
    const std::string blocked = special_block_reason(sim, index);
    if (!blocked.empty()) {
        error = blocked;
        return false;
    }
    if (sim->totalFunds < building.cost) {
        error = "Not enough funds to build that.";
        return false;
    }
    for (int dy = 0; dy < building.height; ++dy) {
        for (int dx = 0; dx < building.width; ++dx) {
            const int x = left + dx;
            const int y = top + dy;
            const int raw = static_cast<int>(sim->map[x][y]);
            const int under = raw & LOMASK;
            forget_dock_at(sim, x, y);
            if (dock_row(building, dy) && is_water_tile(under)) {
                remember_dock(sim, x, y, static_cast<unsigned short>(raw));
            }
            const bool center = dx == building.anchor_x && dy == building.anchor_y;
            const unsigned short tile = static_cast<unsigned short>(center ? building.center_tile : kSpecialBodyTile);
            unsigned short bits = static_cast<unsigned short>(BURNBIT | BULLBIT | (center ? ZONEBIT : 0));
            if (!building.open_air) {
                bits = static_cast<unsigned short>(bits | CONDBIT);
            }
            sim->map[x][y] = static_cast<unsigned short>(tile | bits);
            if (building.center_tile == SPECIAL_DATACENTER_LOT) {
                // The power grid is from the last scan, before this lot was
                // here. A lot opens for good the first time it has power, so
                // it waits for a scan that sees its own wiring.
                sim->powerGridMap.worldSet(x, y, 0);
            }
        }
    }
    if (building.needs_south_water) {
        reedge_below(sim, left, top, building.width, building.height, true);
    }
    sim->spend(building.cost);
    sim->invalidateMaps();
    return true;
}

bool bulldoze_special_at(Micropolis *sim, int tile_x, int tile_y)
{
    if (sim == nullptr) {
        return false;
    }
    int cx = 0;
    int cy = 0;
    int index = -1;
    if (find_center(sim, tile_x, tile_y, cx, cy, index)) {
        if (sim->totalFunds < 1) {
            return false;
        }
        const SpecialBuilding &building = kBuildings[index];
        const int left = cx - building.anchor_x;
        const int top = cy - building.anchor_y;
        for (int dy = 0; dy < building.height; ++dy) {
            for (int dx = 0; dx < building.width; ++dx) {
                settle_cell(sim, left + dx, top + dy, true);
            }
        }
        if (building.needs_south_water) {
            reedge_below(sim, left, top, building.width, building.height, false);
        }
        sim->spend(1);
        sim->invalidateMaps();
        return true;
    }
    // The centre tile can burn away first and leave the other tiles standing.
    // Bulldozing any of those leftovers, or the burned centre, clears the rest.
    const int clicked = tile_of(sim, tile_x, tile_y);
    const bool burned = (clicked >= FIRE && clicked <= LASTFIRE) || (clicked >= RUBBLE && clicked <= LASTRUBBLE);
    if (orphan_body(sim, tile_x, tile_y) || burned) {
        Remnant remnant;
        if (orphan_footprint(sim, tile_x, tile_y, remnant)) {
            if (burned && (tile_x != remnant.left + remnant.anchor_x || tile_y != remnant.top + remnant.anchor_y)) {
                return false;
            }
            if (sim->totalFunds < 1) {
                return false;
            }
            clear_remnant(sim, remnant.left, remnant.top, remnant.width, remnant.height);
            sim->spend(1);
            sim->invalidateMaps();
            return true;
        }
    }
    // Every body tile is gone. A marina that covered shoreline can still
    // be named by the dock notes, including while the explosion plays.
    return bulldoze_dock_wreck(sim, tile_x, tile_y);
}

bool special_skips_power_at(const Micropolis *sim, int tile_x, int tile_y)
{
    int cx = 0;
    int cy = 0;
    int index = -1;
    if (sim == nullptr || !find_center(sim, tile_x, tile_y, cx, cy, index)) {
        return false;
    }
    return kBuildings[index].open_air;
}

int wire_special_buildings(Micropolis *sim)
{
    if (sim == nullptr) {
        return 0;
    }
    int changed = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int raw = static_cast<int>(sim->map[x][y]);
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int index = special_index_for_tile(raw & LOMASK);
            if (index < 0 || !special_building_intact(sim, x, y)) {
                continue;
            }
            const SpecialBuilding &building = kBuildings[index];
            const int left = x - building.anchor_x;
            const int top = y - building.anchor_y;
            for (int dy = 0; dy < building.height; ++dy) {
                for (int dx = 0; dx < building.width; ++dx) {
                    unsigned short &cell = sim->map[left + dx][top + dy];
                    const unsigned short want = static_cast<unsigned short>(
                        building.open_air ? (cell & ~CONDBIT) : (cell | CONDBIT));
                    if (want != cell) {
                        cell = want;
                        ++changed;
                    }
                }
            }
        }
    }
    if (changed > 0) {
        sim->invalidateMaps();
    }
    return changed;
}

std::string special_query_at(const Micropolis *sim, int tile_x, int tile_y)
{
    int cx = 0;
    int cy = 0;
    int index = -1;
    if (!find_center(sim, tile_x, tile_y, cx, cy, index)) {
        return {};
    }
    const SpecialBuilding &building = kBuildings[index];
    std::string text = building.name;
    text += " — ";
    text += building.effect;
    if (!special_building_intact(sim, cx, cy)) {
        text += ". Damaged";
    }
    text += ". Cost ";
    text += graph_money(building.cost);
    return text;
}

bool special_at(const Micropolis *sim, int tile_x, int tile_y, int &index, bool &intact)
{
    int cx = 0;
    int cy = 0;
    index = -1;
    intact = false;
    if (sim == nullptr || !find_center(sim, tile_x, tile_y, cx, cy, index)) {
        index = -1;
        return false;
    }
    intact = special_building_intact(sim, cx, cy);
    return true;
}

std::string special_status_text(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return "Special buildings";
    }
    const SpecialBuilding &building = kBuildings[index];
    return std::string(building.name) + ": " + graph_money(building.cost);
}

std::string special_status_text(const Micropolis *sim, int index)
{
    std::string text = special_status_text(index);
    const std::string block = special_block_reason(sim, index);
    if (!block.empty() && block != "Cannot build here.") {
        text += ". ";
        text += block;
    }
    return text;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "special_buildings.hpp"

#include "micropolis.h"

#include <string>

static_assert(SPECIAL_ZOO == 1020, "zoo center tile");
static_assert(SPECIAL_PARK == 1021, "big park center tile");
static_assert(SPECIAL_LIBRARY == 1022, "library center tile");
static_assert(SPECIAL_BODY == 1023, "special body tile");
static_assert(kSpecialBodyTile == SPECIAL_BODY, "body tile constant");

namespace {

// City 2 rules, written up in the pull request. Zoo, Big Park, and Library
// stay at annual_income 0. Mayor's House, Casino, and Amusement Park follow
// the detailed gift FAQ's prices and the manual's benefits, with casino
// crime kept local (other guides, not the map-wide +1). Center ids 53-55
// are the unused trash slots; the map word cannot grow past 1023.
const SpecialBuilding kBuildings[] = {
    {1020, "Zoo", "zoo", "zoo", 100, 0, 3, 3, 6, 48, "Raises nearby land value"},
    {1021, "Big Park", "bigpark", "big-park", 100, 0, 3, 3, 6, 48, "Raises nearby land value"},
    {1022, "Library", "library", "library", 100, 0, 3, 3, 6, 48, "Raises nearby land value"},
    {53, "Mayor's House", "mayor", "mayor", 100, 0, 3, 3, 6, 48,
     "Raises nearby land value. Best near residential", 0, 0, 2000, 1, 1},
    {54, "Casino", "casino", "casino", 100, 300, 3, 3, 6, 96,
     "Sharply raises nearby land value, brings in $300 a year, and raises local crime. A police station nearby matters",
     8, 100, 0, 2, 3},
    {55, "Amusement Park", "amusement", "amusement", 100, 200, 3, 3, 6, 96,
     "Sharply raises nearby land value and brings in $200 a year. Best near residential", 0, 0, 0, 2, 3},
};

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

bool is_center_at(const Micropolis *sim, int x, int y, int index)
{
    if (index < 0 || index >= special_building_count()) {
        return false;
    }
    const int raw = in_bounds(x, y) ? static_cast<int>(sim->map[x][y]) : 0;
    return (raw & ZONEBIT) != 0 && (raw & LOMASK) == kBuildings[index].center_tile;
}

// Trees, rubble, and other small bulldozable land. Roads, zones, water,
// fire, and radiation stay put until the player bulldozes them on purpose.
bool auto_clearable(int raw)
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

bool is_water_tile(int tile)
{
    if (tile >= RIVER && tile <= WATER_HIGH) {
        return true;
    }
    return tile >= FLOOD && tile <= LASTFLOOD;
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
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int sx = x + dx;
            const int sy = y + dy;
            if (!in_bounds(sx, sy)) {
                continue;
            }
            const int raw = static_cast<int>(sim->map[sx][sy]);
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int found = special_index_for_tile(raw & LOMASK);
            if (found < 0) {
                continue;
            }
            // The clicked tile has to sit inside that building's 3x3.
            if (x < sx - 1 || x > sx + 1 || y < sy - 1 || y > sy + 1) {
                continue;
            }
            cx = sx;
            cy = sy;
            index = found;
            return true;
        }
    }
    return false;
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

// The 3x3 that still holds the clicked remnant after its centre is gone.
// A window that contains another building's centre is rejected.
bool orphan_footprint(const Micropolis *sim, int x, int y, int &left, int &top)
{
    int best = -1;
    int best_center = -1;
    int best_left = 0;
    int best_top = 0;
    for (int cy = y - 1; cy <= y + 1; ++cy) {
        for (int cx = x - 1; cx <= x + 1; ++cx) {
            const int l = cx - 1;
            const int t = cy - 1;
            if (l < 0 || t < 0 || l + 3 > WORLD_W || t + 3 > WORLD_H) {
                continue;
            }
            if (x < l || x >= l + 3 || y < t || y >= t + 3) {
                continue;
            }
            int bodies = 0;
            bool foreign = false;
            for (int dy = 0; dy < 3 && !foreign; ++dy) {
                for (int dx = 0; dx < 3; ++dx) {
                    const int sx = l + dx;
                    const int sy = t + dy;
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
            const int center_tile = tile_of(sim, cx, cy);
            int center_score = 0;
            if ((center_tile >= FIRE && center_tile <= LASTFIRE) || (center_tile >= RUBBLE && center_tile <= LASTRUBBLE) ||
                special_index_for_tile(center_tile) >= 0) {
                center_score = 1;
            }
            if (bodies > best || (bodies == best && center_score > best_center)) {
                best = bodies;
                best_center = center_score;
                best_left = l;
                best_top = t;
            }
        }
    }
    if (best < 1) {
        return false;
    }
    left = best_left;
    top = best_top;
    return true;
}

void clear_remnant(Micropolis *sim, int left, int top)
{
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            const int x = left + dx;
            const int y = top + dy;
            if (!in_bounds(x, y) || !remnant_tile(static_cast<int>(sim->map[x][y]))) {
                continue;
            }
            sim->map[x][y] = static_cast<unsigned short>(RUBBLE | BULLBIT);
        }
    }
}

} // namespace

int special_building_count()
{
    return static_cast<int>(sizeof(kBuildings) / sizeof(kBuildings[0]));
}

const SpecialBuilding &special_building(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return kBuildings[0];
    }
    return kBuildings[index];
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
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (tile_of(sim, center_x + dx, center_y + dy) != kSpecialBodyTile) {
                return false;
            }
        }
    }
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
    const long long people =
        (static_cast<long long>(res) + (static_cast<long long>(com) + static_cast<long long>(ind)) * 8LL) * 20LL;
    if (people < 0) {
        return 0;
    }
    if (people > 2000000000LL) {
        return 2000000000;
    }
    return static_cast<int>(people);
}

std::string special_block_reason(const Micropolis *sim, int index)
{
    if (sim == nullptr || index < 0 || index >= special_building_count()) {
        return "Cannot build here.";
    }
    const SpecialBuilding &building = kBuildings[index];
    if (building.unlock_population > 0 && special_population(sim) < building.unlock_population) {
        return "Available at 2,000 population (Town)";
    }
    if (building.cap_group >= 0 && building.cap_limit > 0 && count_group(sim, building.cap_group) >= building.cap_limit) {
        if (building.cap_limit == 1) {
            return "Only one Mayor's House in a city.";
        }
        return "Amusement Parks and Casinos are limited to 3.";
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
    return "$" + std::to_string(kBuildings[index].cost);
}

int special_annual_income(const Micropolis *sim)
{
    if (sim == nullptr) {
        return 0;
    }
    int total = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int index = special_index_for_tile(tile_of(sim, x, y));
            if (index < 0 || !special_building_intact(sim, x, y)) {
                continue;
            }
            total += kBuildings[index].annual_income;
        }
    }
    return total;
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
            if (building.crime_bonus <= 0 || building.crime_radius <= 0) {
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
                    int coverage = sim->policeStationMap.worldGet(wx, wy);
                    if (coverage < 0) {
                        coverage = 0;
                    }
                    int add = building.crime_bonus - coverage / 8;
                    if (add < 0) {
                        add = 0;
                    }
                    int current = sim->crimeRateMap.worldGet(wx, wy);
                    if (current < 0) {
                        current = 0;
                    }
                    int raised = current + add;
                    if (raised > 250) {
                        raised = 250;
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
    const int left = tile_x - 1;
    const int top = tile_y - 1;
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
            if (sim->autoBulldoze && auto_clearable(raw)) {
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
            const bool center = dx == 1 && dy == 1;
            const unsigned short tile = static_cast<unsigned short>(center ? building.center_tile : kSpecialBodyTile);
            const unsigned short bits = static_cast<unsigned short>(BURNBIT | BULLBIT | (center ? ZONEBIT : 0));
            sim->map[left + dx][top + dy] = static_cast<unsigned short>(tile | bits);
        }
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
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (!in_bounds(x, y)) {
                    continue;
                }
                sim->map[x][y] = static_cast<unsigned short>(RUBBLE | BULLBIT);
            }
        }
        sim->spend(1);
        sim->invalidateMaps();
        return true;
    }
    // The centre tile can burn away first and leave the other eight standing.
    // Bulldozing any of those leftovers, or the burned centre, clears the rest.
    const int clicked = tile_of(sim, tile_x, tile_y);
    const bool burned = (clicked >= FIRE && clicked <= LASTFIRE) || (clicked >= RUBBLE && clicked <= LASTRUBBLE);
    if (!orphan_body(sim, tile_x, tile_y) && !burned) {
        return false;
    }
    int left = 0;
    int top = 0;
    if (!orphan_footprint(sim, tile_x, tile_y, left, top)) {
        return false;
    }
    if (burned && (tile_x != left + 1 || tile_y != top + 1)) {
        return false;
    }
    if (sim->totalFunds < 1) {
        return false;
    }
    clear_remnant(sim, left, top);
    sim->spend(1);
    sim->invalidateMaps();
    return true;
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
    text += ". Cost $";
    text += std::to_string(building.cost);
    return text;
}

std::string special_status_text(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return "Special buildings";
    }
    const SpecialBuilding &building = kBuildings[index];
    return std::string(building.name) + ": $" + std::to_string(building.cost);
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

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
static_assert(SPECIAL_PRISON == 870, "prison center tile");
static_assert(SPECIAL_MARINA == 871, "marina center tile");
static_assert(SPECIAL_FOUNTAIN == 872, "placeable fountain tile");
static_assert(SPECIAL_FOUNTAIN != FOUNTAIN, "park fountain animation stays on tile 840");
static_assert(kSpecialBodyTile == SPECIAL_BODY, "body tile constant");

namespace {

// City 2 rules, written up in the pull request. Zoo, Big Park, and Library
// stay at annual_income 0. Mayor's House, Casino, and Amusement Park follow
// the detailed gift FAQ's prices and the manual's benefits, with casino
// crime kept local (other guides, not the map-wide +1). Center ids 53-55
// are the unused trash slots; the map word cannot grow past 1023.
// Prison, Marina, and Fountain are a later first pass. Their center ids
// are the unused gap after the tiny explosions. Anchor 1 matches the
// stadium click (one tile down and right of the top-left). The fountain
// click is the tile itself, like a park.
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
    {SPECIAL_PRISON, "Prison", "prison", "prison", 3000, 0, 4, 4, 6, -48,
     "Lowers nearby land value and reduces crime a little while it has power and a road", 6, -24, 0, -1, 0, 1, 1, true,
     true, false},
    {SPECIAL_MARINA, "Marina", "marina", "marina", 1000, 0, 3, 3, 6, 48,
     "Raises nearby land value. The south side is the dock", 0, 0, 0, -1, 0, 1, 1, false, false, true},
    {SPECIAL_FOUNTAIN, "Fountain", "fountain", "fountain", 20, 0, 1, 1, 4, 16, "Raises nearby land value", 0, 0, 0, -1,
     0, 0, 0, false, false, false},
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

bool power_word(int raw)
{
    if (raw < 0) {
        return false;
    }
    return (raw & CONDBIT) != 0;
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
    const bool road_ok = !building.needs_road || ring_has(left, top, building.width, building.height, word_at, ctx, road_word);
    const bool power_ok =
        !building.needs_power || ring_has(left, top, building.width, building.height, word_at, ctx, power_word);
    if (!power_ok && !road_ok) {
        return "Needs power and a road.";
    }
    if (!power_ok) {
        return "Needs power.";
    }
    if (!road_ok) {
        return "Needs a road.";
    }
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
            if (building.land_bonus == 0 || building.land_radius <= 0) {
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
            if (building.needs_power) {
                bits = static_cast<unsigned short>(bits | CONDBIT);
            }
            sim->map[x][y] = static_cast<unsigned short>(tile | bits);
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
        const SpecialBuilding &building = kBuildings[index];
        const int left = cx - building.anchor_x;
        const int top = cy - building.anchor_y;
        for (int dy = 0; dy < building.height; ++dy) {
            for (int dx = 0; dx < building.width; ++dx) {
                settle_cell(sim, left + dx, top + dy, true);
            }
        }
        sim->spend(1);
        sim->invalidateMaps();
        return true;
    }
    // The centre tile can burn away first and leave the other tiles standing.
    // Bulldozing any of those leftovers, or the burned centre, clears the rest.
    const int clicked = tile_of(sim, tile_x, tile_y);
    const bool burned = (clicked >= FIRE && clicked <= LASTFIRE) || (clicked >= RUBBLE && clicked <= LASTRUBBLE);
    if (!orphan_body(sim, tile_x, tile_y) && !burned) {
        return false;
    }
    Remnant remnant;
    if (!orphan_footprint(sim, tile_x, tile_y, remnant)) {
        return false;
    }
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

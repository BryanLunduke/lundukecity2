// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "city_events.hpp"

#include "micropolis.h"
#include "special_buildings.hpp"

#include <algorithm>
#include <cstring>

namespace city_events {

namespace {

const KindInfo kKinds[kKindCount] = {
    {Kind::Riot, "riot", "Disaster: Riot", "Start a riot?",
     "Fires break out in commercial buildings and crime rises across the city for a few months. This cannot be "
     "undone.",
     "Riots reported. \"Fiery, but mostly peaceful.\""},
    {Kind::AutonomousZone, "autonomous_zone", "Disaster: Autonomous Zone", "Let an Autonomous Zone form?",
     "A two-block zone is barricaded off. Nothing can be built or bulldozed inside. It grows every two months "
     "where police coverage is low and shrinks where it is high. This cannot be undone.",
     "An Autonomous Zone has formed!"},
    {Kind::LionEscape, "lion_escape", "Disaster: Lion Escape", "Let a lion out of the zoo?",
     "A lion leaves the zoo and roams the roads and open land, eating residents, until the police or its keepers "
     "catch it. This cannot be undone.",
     "A lion has escaped from the zoo!"},
    {Kind::PrisonEscape, "prison_escape", "Disaster: Prison Escape", "Start a prison break?",
     "Escaped inmates set fires near the prison and crime rises around it until the police restore order. This "
     "cannot be undone.",
     "Prison break! Escaped inmates are setting fires."},
};

// A small deterministic generator. Each use is seeded from the city time
// and a salt, so nothing here touches the engine's random sequence.
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int seed)
        : state(seed == 0 ? 0x9e3779b9u : seed)
    {
    }
    unsigned int next()
    {
        unsigned int x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<unsigned int>(n)); }
};

unsigned int seed_for(const Micropolis *sim, unsigned int salt)
{
    return static_cast<unsigned int>(sim->cityTime) * 2654435761u ^ (salt * 40503u + 0x5bd1e995u);
}

int tile_at(const Micropolis *sim, int x, int y)
{
    return sim->map[x][y] & LOMASK;
}

bool find_center(const Micropolis *sim, int center_tile, int &cx, int &cy)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if ((sim->map[x][y] & LOMASK) == static_cast<unsigned>(center_tile)) {
                cx = x;
                cy = y;
                return true;
            }
        }
    }
    return false;
}

bool lion_walkable(const Micropolis *sim, int x, int y)
{
    if (!Micropolis::testBounds(x, y)) {
        return false;
    }
    const int tile = tile_at(sim, x, y);
    return tile == DIRT || (tile >= ROADBASE && tile <= LASTROAD);
}

int police_at_block(const Micropolis *sim, int bx, int by)
{
    if (!sim->policeStationEffectMap.onMap(bx, by)) {
        return 0;
    }
    return sim->policeStationEffectMap.get(bx, by);
}

int police_at_tile(const Micropolis *sim, int x, int y)
{
    const int block = sim->policeStationEffectMap.MAP_BLOCKSIZE;
    return police_at_block(sim, x / block, y / block);
}

Note place_note(const std::string &text, int x, int y)
{
    Note note;
    note.text = text;
    note.x = x;
    note.y = y;
    note.has_place = Micropolis::testBounds(x, y);
    return note;
}

Note plain_note(const std::string &text)
{
    Note note;
    note.text = text;
    return note;
}

// ---- Riot ---------------------------------------------------------------

bool start_riot(Micropolis *sim, std::vector<Note> &notes)
{
    // Developed commercial zones first; empty commercial lots only when
    // there is nothing else.
    std::vector<std::pair<int, int>> developed;
    std::vector<std::pair<int, int>> empty;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const MapValue z = sim->map[x][y];
            const int tile = z & LOMASK;
            if ((z & ZONEBIT) == 0 || tile < COMBASE || tile > COMLAST) {
                continue;
            }
            (tile == COMCLR ? empty : developed).emplace_back(x, y);
        }
    }
    std::vector<std::pair<int, int>> &pool = developed.empty() ? empty : developed;
    Rng rng(seed_for(sim, 11));
    int lit = 0;
    int first_x = WORLD_W / 2;
    int first_y = WORLD_H / 2;
    for (int n = 0; n < kRiotFires && !pool.empty(); ++n) {
        const int pick = rng.below(static_cast<int>(pool.size()));
        const auto zone = pool[static_cast<std::size_t>(pick)];
        pool.erase(pool.begin() + pick);
        // One of the eight non-centre tiles of the 3x3 zone.
        static const int kRing[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
        const int start = rng.below(8);
        for (int k = 0; k < 8; ++k) {
            const int fx = zone.first + kRing[(start + k) % 8][0];
            const int fy = zone.second + kRing[(start + k) % 8][1];
            if (sim->eventFireAt(fx, fy)) {
                if (lit == 0) {
                    first_x = fx;
                    first_y = fy;
                }
                ++lit;
                break;
            }
        }
    }
    CityEvents &e = sim->cityEvents;
    e.riotPeak = kRiotCrimePeak;
    e.riotSteps = kRiotSteps;
    e.riotTotal = kRiotSteps;
    notes.push_back(place_note(info(Kind::Riot).notice, first_x, first_y));
    return true;
}

// ---- Autonomous Zone: road blocks ---------------------------------------

bool is_street(int tile)
{
    return (tile >= ROADBASE && tile <= LASTROAD) || (tile >= RAILBASE && tile <= LASTRAIL) || tile == RAILHPOWERV ||
           tile == RAILVPOWERH;
}

bool is_water(int tile)
{
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

bool street_at(const Micropolis *sim, int x, int y)
{
    return Micropolis::testBounds(x, y) && is_street(tile_at(sim, x, y));
}

// Land that belongs to a road block: not road, rail or water.
bool block_land(const Micropolis *sim, int x, int y)
{
    if (!Micropolis::testBounds(x, y)) {
        return false;
    }
    const int tile = tile_at(sim, x, y);
    return !is_street(tile) && !is_water(tile);
}

bool built_tile(int tile)
{
    // Buildings only (zones, service buildings, landmarks), not roads,
    // power lines, parks or trees: a zone forms in a neighbourhood.
    return (tile >= RESBASE && tile < LASTZONE) || tile == SPECIAL_BODY;
}

int tile_index(int x, int y)
{
    return x * WORLD_H + y;
}

// The footprint of the building whose centre (ZONEBIT) tile is (x, y).
bool center_footprint(const Micropolis *sim, int x, int y, int &left, int &top, int &width, int &height)
{
    const MapValue raw = sim->map[x][y];
    if ((raw & ZONEBIT) == 0) {
        return false;
    }
    const int center = raw & LOMASK;
    const int special = special_index_for_tile(center);
    if (special >= 0) {
        const SpecialBuilding &building = special_building(special);
        left = x - building.anchor_x;
        top = y - building.anchor_y;
        width = building.width;
        height = building.height;
        return width > 0 && height > 0;
    }
    int span = 3;
    if (center >= AIRPORTBASE && center < COALBASE) {
        span = 6;
    } else if ((center >= PORTBASE && center < AIRPORTBASE) || (center >= COALBASE && center < FIRESTBASE) ||
               (center >= STADIUMBASE && center < INDBASE2)) {
        span = 4;
    }
    left = x - 1;
    top = y - 1;
    width = span;
    height = span;
    return true;
}

// Every road block on the map, labelled. label[tile_index] is the block
// number or -1 on streets and water.
struct RoadBlocks {
    struct Block {
        int seed_x = 0;
        int seed_y = 0;
        int tiles = 0;
        int built = 0;
        int police = 0; // sum over tiles
        bool edge = false;
        bool capped = false; // a piece of an open area cut to the cap
        long sum_x = 0;
        long sum_y = 0;
    };
    std::vector<int> label;
    std::vector<Block> blocks;
};

RoadBlocks label_road_blocks(const Micropolis *sim)
{
    RoadBlocks out;
    out.label.assign(WORLD_W * WORLD_H, -1);
    std::vector<int> component(WORLD_W * WORLD_H, -1);
    std::vector<int> owner(WORLD_W * WORLD_H, -1);
    std::vector<std::pair<int, int>> tiles;
    std::vector<std::pair<int, int>> queue;
    int components = 0;
    static const int kSide[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    // Scan row by row so a block's first tile is its top-left-most.
    for (int sy = 0; sy < WORLD_H; ++sy) {
        for (int sx = 0; sx < WORLD_W; ++sx) {
            if (component[tile_index(sx, sy)] >= 0 || !block_land(sim, sx, sy)) {
                continue;
            }
            const int id = components++;
            tiles.clear();
            queue.clear();
            queue.emplace_back(sx, sy);
            component[tile_index(sx, sy)] = id;
            for (std::size_t head = 0; head < queue.size(); ++head) {
                const auto at = queue[head];
                tiles.push_back(at);
                for (const auto &side : kSide) {
                    const int nx = at.first + side[0];
                    const int ny = at.second + side[1];
                    if (block_land(sim, nx, ny) && component[tile_index(nx, ny)] < 0) {
                        component[tile_index(nx, ny)] = id;
                        queue.emplace_back(nx, ny);
                    }
                }
            }
            // Scan order inside the block too.
            std::sort(tiles.begin(), tiles.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                return a.second != b.second ? a.second < b.second : a.first < b.first;
            });
            if (static_cast<int>(tiles.size()) <= kZoneBlockMaxTiles) {
                const int number = static_cast<int>(out.blocks.size());
                out.blocks.emplace_back();
                for (const auto &t : tiles) {
                    out.label[tile_index(t.first, t.second)] = number;
                }
                continue;
            }
            // Too big: cut along the kZoneCapCell grid, buildings whole
            // with the square their centre sits in.
            const int cells_w = (WORLD_W + kZoneCapCell - 1) / kZoneCapCell;
            for (const auto &t : tiles) {
                owner[tile_index(t.first, t.second)] = (t.second / kZoneCapCell) * cells_w + t.first / kZoneCapCell;
            }
            for (const auto &t : tiles) {
                int left = 0;
                int top = 0;
                int width = 0;
                int height = 0;
                if (!center_footprint(sim, t.first, t.second, left, top, width, height)) {
                    continue;
                }
                const int cell = (t.second / kZoneCapCell) * cells_w + t.first / kZoneCapCell;
                for (int x = left; x < left + width; ++x) {
                    for (int y = top; y < top + height; ++y) {
                        if (Micropolis::testBounds(x, y) && component[tile_index(x, y)] == id) {
                            owner[tile_index(x, y)] = cell;
                        }
                    }
                }
            }
            // Each connected piece of one square is a block.
            for (const auto &t : tiles) {
                if (out.label[tile_index(t.first, t.second)] >= 0) {
                    continue;
                }
                const int number = static_cast<int>(out.blocks.size());
                out.blocks.emplace_back();
                out.blocks.back().capped = true;
                const int cell = owner[tile_index(t.first, t.second)];
                queue.clear();
                queue.push_back(t);
                out.label[tile_index(t.first, t.second)] = number;
                for (std::size_t head = 0; head < queue.size(); ++head) {
                    const auto at = queue[head];
                    for (const auto &side : kSide) {
                        const int nx = at.first + side[0];
                        const int ny = at.second + side[1];
                        if (Micropolis::testBounds(nx, ny) && component[tile_index(nx, ny)] == id &&
                            owner[tile_index(nx, ny)] == cell && out.label[tile_index(nx, ny)] < 0) {
                            out.label[tile_index(nx, ny)] = number;
                            queue.emplace_back(nx, ny);
                        }
                    }
                }
            }
        }
    }
    // Totals, and each block's first tile in scan order.
    std::vector<bool> seeded(out.blocks.size(), false);
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            const int number = out.label[tile_index(x, y)];
            if (number < 0) {
                continue;
            }
            RoadBlocks::Block &block = out.blocks[static_cast<std::size_t>(number)];
            if (!seeded[static_cast<std::size_t>(number)]) {
                seeded[static_cast<std::size_t>(number)] = true;
                block.seed_x = x;
                block.seed_y = y;
            }
            ++block.tiles;
            block.built += built_tile(tile_at(sim, x, y)) ? 1 : 0;
            block.police += police_at_tile(sim, x, y);
            block.edge = block.edge || x == 0 || y == 0 || x == WORLD_W - 1 || y == WORLD_H - 1;
            block.sum_x += x;
            block.sum_y += y;
        }
    }
    return out;
}

// The block across from land tile (x, y) in direction side: the next
// tile if it is land, or the land past up to kZoneStreetWidth street
// tiles. -1 when there is none (water, map edge, a wider road).
int block_across(const Micropolis *sim, const RoadBlocks &map, int x, int y, int dx, int dy)
{
    for (int k = 1; k <= kZoneStreetWidth + 1; ++k) {
        const int nx = x + dx * k;
        const int ny = y + dy * k;
        if (!Micropolis::testBounds(nx, ny)) {
            return -1;
        }
        const int number = map.label[tile_index(nx, ny)];
        if (number >= 0) {
            return number;
        }
        if (!is_street(tile_at(sim, nx, ny))) {
            return -1;
        }
    }
    return -1;
}

bool zone_block_tile(const CityEvents &e, int x, int y)
{
    if (!Micropolis::testBounds(x, y)) {
        return false;
    }
    const unsigned char v = e.zoneTile[x][y];
    return v != 0 && v != CityEvents::kZoneStreet;
}

bool zone_tile(const CityEvents &e, int x, int y)
{
    return Micropolis::testBounds(x, y) && e.zoneTile[x][y] != 0;
}

// The streets between the zone's blocks join it: a run of up to
// kZoneStreetWidth street tiles with zone land at both ends, then any
// crossing those runs meet from both sides.
void mark_zone_streets(Micropolis *sim)
{
    CityEvents &e = sim->cityEvents;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (e.zoneTile[x][y] == CityEvents::kZoneStreet) {
                e.zoneTile[x][y] = 0;
            }
        }
    }
    if (e.zoneCount <= 0) {
        return;
    }
    auto run_ends_in_zone = [&](int x, int y, int dx, int dy) {
        // From street tile (x, y): the first non-street tile each way.
        int length = 1;
        for (int way = -1; way <= 1; way += 2) {
            int k = 1;
            for (;; ++k) {
                const int nx = x + dx * way * k;
                const int ny = y + dy * way * k;
                if (!Micropolis::testBounds(nx, ny)) {
                    return false;
                }
                if (!is_street(tile_at(sim, nx, ny))) {
                    if (!zone_block_tile(e, nx, ny)) {
                        return false;
                    }
                    break;
                }
                if (k >= kZoneStreetWidth) {
                    return false;
                }
            }
            length += k - 1;
        }
        return length <= kZoneStreetWidth;
    };
    std::vector<std::pair<int, int>> marks;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (e.zoneTile[x][y] == 0 && is_street(tile_at(sim, x, y)) &&
                (run_ends_in_zone(x, y, 1, 0) || run_ends_in_zone(x, y, 0, 1))) {
                marks.emplace_back(x, y);
            }
        }
    }
    for (const auto &m : marks) {
        e.zoneTile[m.first][m.second] = CityEvents::kZoneStreet;
    }
    marks.clear();
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (e.zoneTile[x][y] != 0 || !is_street(tile_at(sim, x, y))) {
                continue;
            }
            const bool across = zone_tile(e, x - 1, y) && zone_tile(e, x + 1, y);
            const bool down = zone_tile(e, x, y - 1) && zone_tile(e, x, y + 1);
            if (across || down) {
                marks.emplace_back(x, y);
            }
        }
    }
    for (const auto &m : marks) {
        e.zoneTile[m.first][m.second] = CityEvents::kZoneStreet;
    }
}

int block_police_average(const RoadBlocks::Block &block)
{
    return block.tiles > 0 ? block.police / block.tiles : 0;
}

int zone_police_average(const Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    long total = 0;
    long tiles = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (zone_block_tile(e, x, y)) {
                total += police_at_tile(sim, x, y);
                ++tiles;
            }
        }
    }
    return tiles > 0 ? static_cast<int>(total / tiles) : 0;
}

// The middle tile of zone block number (1-based), for notices.
void zone_block_middle(const Micropolis *sim, int number, int &mx, int &my)
{
    const CityEvents &e = sim->cityEvents;
    long sx = 0;
    long sy = 0;
    long count = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (e.zoneTile[x][y] == number) {
                sx += x;
                sy += y;
                ++count;
            }
        }
    }
    mx = count > 0 ? static_cast<int>(sx / count) : WORLD_W / 2;
    my = count > 0 ? static_cast<int>(sy / count) : WORLD_H / 2;
}

// Adds road block `number` of `map` to the zone as its next block. Tiles
// already in the zone stay with their block. False when nothing was added.
bool add_zone_block(Micropolis *sim, const RoadBlocks &map, int number)
{
    CityEvents &e = sim->cityEvents;
    if (e.zoneCount >= kZoneMaxBlocks || number < 0 || number >= static_cast<int>(map.blocks.size())) {
        return false;
    }
    const unsigned char mark = static_cast<unsigned char>(e.zoneCount + 1);
    int added = 0;
    int first_x = -1;
    int first_y = -1;
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            if (map.label[tile_index(x, y)] != number || zone_block_tile(e, x, y)) {
                continue;
            }
            e.zoneTile[x][y] = mark;
            if (added++ == 0) {
                first_x = x;
                first_y = y;
            }
        }
    }
    if (added == 0) {
        return false;
    }
    e.zoneBlock[e.zoneCount][0] = static_cast<unsigned char>(first_x);
    e.zoneBlock[e.zoneCount][1] = static_cast<unsigned char>(first_y);
    ++e.zoneCount;
    return true;
}

// The blocks next to the given ones (touching, or across a street).
std::vector<int> neighbour_blocks(const Micropolis *sim, const RoadBlocks &map, const std::vector<bool> &member)
{
    std::vector<bool> seen(map.blocks.size(), false);
    std::vector<int> out;
    static const int kSide[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int number = map.label[tile_index(x, y)];
            if (number < 0 || !member[static_cast<std::size_t>(number)]) {
                continue;
            }
            for (const auto &side : kSide) {
                const int other = block_across(sim, map, x, y, side[0], side[1]);
                if (other >= 0 && !member[static_cast<std::size_t>(other)] && !seen[static_cast<std::size_t>(other)]) {
                    seen[static_cast<std::size_t>(other)] = true;
                    out.push_back(other);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Of these blocks, the least policed; a tie goes to the more built, then
// to the first in scan order. -1 when there are none.
int least_policed(const RoadBlocks &map, const std::vector<int> &choices)
{
    int pick = -1;
    for (const int number : choices) {
        if (pick < 0) {
            pick = number;
            continue;
        }
        const RoadBlocks::Block &a = map.blocks[static_cast<std::size_t>(number)];
        const RoadBlocks::Block &b = map.blocks[static_cast<std::size_t>(pick)];
        const int pa = block_police_average(a);
        const int pb = block_police_average(b);
        if (pa < pb || (pa == pb && a.built > b.built)) {
            pick = number;
        }
    }
    return pick;
}

bool built_up(const RoadBlocks::Block &block)
{
    return !block.edge && !block.capped && block.built >= kZoneMinBuilt && block.built * 100 >= block.tiles * kZoneBuiltPercent;
}

bool pick_zone_pair(const Micropolis *sim, const RoadBlocks &map, int &first, int &second)
{
    // The built-up pair of neighbouring blocks with the least police (tile
    // average over both); a tie goes to the more built pair, then to the
    // first in scan order.
    first = -1;
    second = -1;
    long best_police = 0;
    int best_built = -1;
    const int count = static_cast<int>(map.blocks.size());
    std::vector<bool> member(map.blocks.size(), false);
    for (int a = 0; a < count; ++a) {
        const RoadBlocks::Block &block_a = map.blocks[static_cast<std::size_t>(a)];
        if (!built_up(block_a)) {
            continue;
        }
        member[static_cast<std::size_t>(a)] = true;
        const std::vector<int> around = neighbour_blocks(sim, map, member);
        member[static_cast<std::size_t>(a)] = false;
        for (const int b : around) {
            const RoadBlocks::Block &block_b = map.blocks[static_cast<std::size_t>(b)];
            if (b < a || !built_up(block_b)) {
                continue;
            }
            const long police = (static_cast<long>(block_a.police) + block_b.police) / (block_a.tiles + block_b.tiles);
            const int built = block_a.built + block_b.built;
            if (first < 0 || police < best_police || (police == best_police && built > best_built)) {
                best_police = police;
                best_built = built;
                // The less policed block of the two goes first.
                const bool swap = block_police_average(block_b) < block_police_average(block_a);
                first = swap ? b : a;
                second = swap ? a : b;
            }
        }
    }
    return first >= 0;
}

bool place_zone(Micropolis *sim, const RoadBlocks &map, int first, int second, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    std::memset(e.zoneTile, 0, sizeof(e.zoneTile));
    e.zoneCount = 0;
    if (!add_zone_block(sim, map, first) || !add_zone_block(sim, map, second)) {
        std::memset(e.zoneTile, 0, sizeof(e.zoneTile));
        e.zoneCount = 0;
        return false;
    }
    mark_zone_streets(sim);
    e.zoneClock = kZoneReviewSteps;
    sim->invalidateMaps();
    const RoadBlocks::Block &a = map.blocks[static_cast<std::size_t>(first)];
    const RoadBlocks::Block &b = map.blocks[static_cast<std::size_t>(second)];
    const long tiles = a.tiles + b.tiles;
    notes.push_back(place_note(info(Kind::AutonomousZone).notice, static_cast<int>((a.sum_x + b.sum_x) / tiles),
                               static_cast<int>((a.sum_y + b.sum_y) / tiles)));
    return true;
}

void review_zone(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    const int police = zone_police_average(sim);
    if (police < kZoneGrowBelow) {
        if (e.zoneCount >= kZoneMaxBlocks) {
            return;
        }
        const RoadBlocks map = label_road_blocks(sim);
        // Blocks the zone already holds (any of their tiles).
        std::vector<bool> member(map.blocks.size(), false);
        for (int x = 0; x < WORLD_W; ++x) {
            for (int y = 0; y < WORLD_H; ++y) {
                const int number = map.label[tile_index(x, y)];
                if (number >= 0 && zone_block_tile(e, x, y)) {
                    member[static_cast<std::size_t>(number)] = true;
                }
            }
        }
        const int pick = least_policed(map, neighbour_blocks(sim, map, member));
        if (pick >= 0 && add_zone_block(sim, map, pick)) {
            mark_zone_streets(sim);
            sim->invalidateMaps();
            const RoadBlocks::Block &block = map.blocks[static_cast<std::size_t>(pick)];
            notes.push_back(place_note("The Autonomous Zone has grown. Police coverage there is too low.",
                                       static_cast<int>(block.sum_x / block.tiles),
                                       static_cast<int>(block.sum_y / block.tiles)));
        }
        return;
    }
    if (police >= kZoneShrinkAt) {
        // Give up the block it took last.
        const int gone = e.zoneCount;
        int gone_x = 0;
        int gone_y = 0;
        zone_block_middle(sim, gone, gone_x, gone_y);
        for (int x = 0; x < WORLD_W; ++x) {
            for (int y = 0; y < WORLD_H; ++y) {
                if (e.zoneTile[x][y] == gone) {
                    e.zoneTile[x][y] = 0;
                }
            }
        }
        --e.zoneCount;
        mark_zone_streets(sim);
        sim->invalidateMaps();
        if (e.zoneCount == 0) {
            e.zoneClock = 0;
            notes.push_back(place_note("The Autonomous Zone has ended. Police have restored order.", gone_x, gone_y));
        } else {
            notes.push_back(place_note("The Autonomous Zone is shrinking under police pressure.", gone_x, gone_y));
        }
    }
}

// ---- Lion Escape --------------------------------------------------------

bool lion_start_tile(const Micropolis *sim, int zx, int zy, int &sx, int &sy)
{
    // The ring just outside the zoo's 3x3 footprint, roads first.
    for (int pass = 0; pass < 2; ++pass) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != 2) {
                    continue;
                }
                const int x = zx + dx;
                const int y = zy + dy;
                if (!lion_walkable(sim, x, y)) {
                    continue;
                }
                const int tile = tile_at(sim, x, y);
                const bool road = tile >= ROADBASE && tile <= LASTROAD;
                if (pass == 0 && !road) {
                    continue;
                }
                sx = x;
                sy = y;
                return true;
            }
        }
    }
    return false;
}

void lion_end(Micropolis *sim, std::vector<Note> &notes, const char *text)
{
    CityEvents &e = sim->cityEvents;
    const int x = e.lionX / 16;
    const int y = e.lionY / 16;
    e.lionActive = 0;
    notes.push_back(place_note(text, x, y));
}

void lion_walk(Micropolis *sim)
{
    CityEvents &e = sim->cityEvents;
    static const int kStep[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int moved = 0; moved < kLionPixelsPerFrame; ++moved) {
        if (e.lionX % 16 == 8 && e.lionY % 16 == 8) {
            // At a tile centre: pick the next tile.
            const int tx = e.lionX / 16;
            const int ty = e.lionY / 16;
            int options[4];
            int count = 0;
            for (int d = 0; d < 4; ++d) {
                if (d == ((e.lionDir + 2) & 3)) {
                    continue;
                }
                if (lion_walkable(sim, tx + kStep[d][0], ty + kStep[d][1])) {
                    options[count++] = d;
                }
            }
            Rng rng(e.lionRng);
            if (count == 0) {
                const int back = (e.lionDir + 2) & 3;
                if (!lion_walkable(sim, tx + kStep[back][0], ty + kStep[back][1])) {
                    e.lionRng = rng.next();
                    return; // Boxed in: it waits.
                }
                e.lionDir = back;
            } else {
                bool straight = false;
                for (int i = 0; i < count; ++i) {
                    straight = straight || options[i] == e.lionDir;
                }
                if (!straight || rng.below(3) == 0) {
                    e.lionDir = options[rng.below(count)];
                }
            }
            e.lionRng = rng.next();
        }
        e.lionX += kStep[e.lionDir][0];
        e.lionY += kStep[e.lionDir][1];
    }
}

void lion_step(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    ++e.lionSteps;
    const int tx = e.lionX / 16;
    const int ty = e.lionY / 16;
    if (e.lionSteps >= kLionCatchAfterSteps && police_at_tile(sim, tx, ty) >= kLionCatchCoverage) {
        lion_end(sim, notes, "Police caught the lion and returned it to the zoo.");
        return;
    }
    if (e.lionSteps >= kLionMaxSteps) {
        lion_end(sim, notes, "Zookeepers caught the lion and took it home.");
        return;
    }
    if (--e.lionMealClock <= 0) {
        e.lionMealClock = kLionMealSteps;
        const int eaten = sim->eventEatResidents(tx, ty, kLionMealRadius);
        if (eaten > 0) {
            e.lionEaten += eaten;
            if (e.lionEaten == eaten) {
                notes.push_back(place_note("The lion is eating residents! Send the police.", tx, ty));
            }
        }
    }
}

// ---- Prison Escape ------------------------------------------------------

bool start_prison(Micropolis *sim, std::vector<Note> &notes)
{
    int px = 0;
    int py = 0;
    if (!find_center(sim, SPECIAL_PRISON, px, py)) {
        return false;
    }
    std::vector<std::pair<int, int>> spots;
    for (int dy = -kPrisonFireRadius; dy <= kPrisonFireRadius; ++dy) {
        for (int dx = -kPrisonFireRadius; dx <= kPrisonFireRadius; ++dx) {
            // Not the prison itself (4x4, anchor 1,1: -1..2).
            if (dx >= -1 && dx <= 2 && dy >= -1 && dy <= 2) {
                continue;
            }
            if (dx * dx + dy * dy > kPrisonFireRadius * kPrisonFireRadius) {
                continue;
            }
            const int x = px + dx;
            const int y = py + dy;
            if (!Micropolis::testBounds(x, y)) {
                continue;
            }
            const MapValue z = sim->map[x][y];
            const int tile = z & LOMASK;
            // Buildings only: not trees, rubble, roads or rail.
            if ((z & ZONEBIT) == 0 && (z & BURNBIT) != 0 &&
                (tile == SPECIAL_BODY || (tile >= RESBASE && tile < LASTZONE))) {
                spots.emplace_back(x, y);
            }
        }
    }
    Rng rng(seed_for(sim, 29));
    for (int n = 0; n < kPrisonFires && !spots.empty(); ++n) {
        const int pick = rng.below(static_cast<int>(spots.size()));
        const auto spot = spots[static_cast<std::size_t>(pick)];
        spots.erase(spots.begin() + pick);
        sim->eventFireAt(spot.first, spot.second);
    }
    CityEvents &e = sim->cityEvents;
    e.prisonX = px;
    e.prisonY = py;
    e.prisonCrime = kPrisonCrimePeak;
    e.prisonSteps = kPrisonMaxSteps;
    notes.push_back(place_note(info(Kind::PrisonEscape).notice, px, py));
    return true;
}

void prison_step(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    const int fade = 2 + police_at_tile(sim, e.prisonX, e.prisonY) / 40;
    e.prisonCrime = std::max(0, e.prisonCrime - fade);
    --e.prisonSteps;
    if (e.prisonCrime == 0 || e.prisonSteps <= 0) {
        e.prisonCrime = 0;
        e.prisonSteps = 0;
        notes.push_back(place_note("The escaped inmates are back behind bars.", e.prisonX, e.prisonY));
    }
}

void step(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    if (e.riotSteps > 0) {
        --e.riotSteps;
        if (e.riotSteps == 0) {
            e.riotPeak = 0;
            e.riotTotal = 0;
        }
    }
    if (e.zoneCount > 0 && --e.zoneClock <= 0) {
        e.zoneClock = kZoneReviewSteps;
        review_zone(sim, notes);
    }
    if (e.lionActive != 0) {
        lion_step(sim, notes);
    }
    if (e.prisonSteps > 0) {
        prison_step(sim, notes);
    }
}

} // namespace

const KindInfo &info(Kind kind)
{
    return kKinds[static_cast<int>(kind)];
}

bool kind_from_key(const std::string &key, Kind &kind)
{
    for (const KindInfo &row : kKinds) {
        if (key == row.key) {
            kind = row.kind;
            return true;
        }
    }
    return false;
}

std::string unavailable_reason(const Micropolis *sim, Kind kind)
{
    const CityEvents &e = sim->cityEvents;
    int x = 0;
    int y = 0;
    switch (kind) {
    case Kind::Riot:
        if (e.riotSteps > 0) {
            return "A riot is already under way.";
        }
        return {};
    case Kind::AutonomousZone:
        if (e.zoneCount > 0) {
            return "An Autonomous Zone already stands.";
        }
        {
            const RoadBlocks map = label_road_blocks(sim);
            int first = -1;
            int second = -1;
            if (!pick_zone_pair(sim, map, first, second)) {
                return "An Autonomous Zone needs a built-up neighborhood.";
            }
        }
        return {};
    case Kind::LionEscape:
        if (e.lionActive != 0) {
            return "The lion is already loose.";
        }
        if (!find_center(sim, SPECIAL_ZOO, x, y)) {
            return "Lion Escape needs a Zoo. Build one first.";
        }
        {
            int sx = 0;
            int sy = 0;
            if (!lion_start_tile(sim, x, y, sx, sy)) {
                return "The zoo has no road or open land around it for a lion to escape onto.";
            }
        }
        return {};
    case Kind::PrisonEscape:
        if (e.prisonSteps > 0) {
            return "The last prison break is still going on.";
        }
        if (!find_center(sim, SPECIAL_PRISON, x, y)) {
            return "Prison Escape needs a Prison. Build one first.";
        }
        return {};
    }
    return {};
}

bool start(Micropolis *sim, Kind kind, std::vector<Note> &notes)
{
    const std::string why = unavailable_reason(sim, kind);
    if (!why.empty()) {
        notes.push_back(plain_note(why));
        return false;
    }
    sim->cityEvents.lastStepTime = sim->cityTime;
    switch (kind) {
    case Kind::Riot:
        if (!start_riot(sim, notes)) {
            return false;
        }
        // Show on the crime map now; the engine's next scan keeps it.
        apply_crime(sim);
        return true;
    case Kind::AutonomousZone: {
        const RoadBlocks map = label_road_blocks(sim);
        int first = -1;
        int second = -1;
        if (!pick_zone_pair(sim, map, first, second) || !place_zone(sim, map, first, second, notes)) {
            return false;
        }
        apply_land(sim);
        return true;
    }
    case Kind::LionEscape: {
        int zx = 0;
        int zy = 0;
        int sx = 0;
        int sy = 0;
        find_center(sim, SPECIAL_ZOO, zx, zy);
        lion_start_tile(sim, zx, zy, sx, sy);
        CityEvents &e = sim->cityEvents;
        e.lionActive = 1;
        e.lionX = sx * 16 + 8;
        e.lionY = sy * 16 + 8;
        // Facing away from the zoo.
        e.lionDir = sy < zy - 1 ? 0 : (sx > zx + 1 ? 1 : (sy > zy + 1 ? 2 : 3));
        e.lionSteps = 0;
        e.lionMealClock = kLionMealSteps;
        e.lionEaten = 0;
        e.lionHomeX = zx;
        e.lionHomeY = zy;
        e.lionRng = seed_for(sim, 47);
        notes.push_back(place_note(info(Kind::LionEscape).notice, sx, sy));
        return true;
    }
    case Kind::PrisonEscape:
        if (!start_prison(sim, notes)) {
            return false;
        }
        apply_crime(sim);
        return true;
    }
    return false;
}

bool start_zone_at(Micropolis *sim, int x, int y, std::vector<Note> &notes)
{
    if (sim->cityEvents.zoneCount > 0 || !Micropolis::testBounds(x, y)) {
        return false;
    }
    const RoadBlocks map = label_road_blocks(sim);
    const int first = map.label[tile_index(x, y)];
    if (first < 0) {
        return false;
    }
    std::vector<bool> member(map.blocks.size(), false);
    member[static_cast<std::size_t>(first)] = true;
    const int second = least_policed(map, neighbour_blocks(sim, map, member));
    if (second < 0) {
        return false;
    }
    sim->cityEvents.lastStepTime = sim->cityTime;
    if (!place_zone(sim, map, first, second, notes)) {
        return false;
    }
    // Show on the land value map now; the engine's next scan keeps it.
    apply_land(sim);
    return true;
}

void frame(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    if (!any_active(sim)) {
        e.lastStepTime = sim->cityTime;
        return;
    }
    if (e.lionActive != 0) {
        lion_walk(sim);
    }
    if (e.lastStepTime != sim->cityTime) {
        e.lastStepTime = sim->cityTime;
        step(sim, notes);
    }
}

int riot_crime_now(const Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    if (e.riotSteps <= 0 || e.riotTotal <= 0) {
        return 0;
    }
    // Full strength for the first half, then a straight fade. The engine
    // scans crime only every few months at the faster speeds, so a fade
    // from the first day would mostly be missed.
    const int half = std::max(1, e.riotTotal / 2);
    if (e.riotSteps >= half) {
        return e.riotPeak;
    }
    return (e.riotPeak * e.riotSteps + half - 1) / half;
}

void apply_crime(Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    const int riot = riot_crime_now(sim);
    const int prison = e.prisonSteps > 0 ? e.prisonCrime : 0;
    if (riot <= 0 && prison <= 0) {
        return;
    }
    const int block = sim->crimeRateMap.MAP_BLOCKSIZE;
    for (int x = 0; x < WORLD_W; x += block) {
        for (int y = 0; y < WORLD_H; y += block) {
            if (sim->landValueMap.worldGet(x, y) <= 0) {
                continue;
            }
            int add = riot;
            if (prison > 0) {
                const int dx = x + block / 2 - e.prisonX;
                const int dy = y + block / 2 - e.prisonY;
                if (dx * dx + dy * dy <= kPrisonCrimeRadius * kPrisonCrimeRadius) {
                    add += prison;
                }
            }
            if (add > 0) {
                const int z = std::min(250, sim->crimeRateMap.worldGet(x, y) + add);
                sim->crimeRateMap.worldSet(x, y, static_cast<Byte>(z));
            }
        }
    }
}

void apply_land(Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    if (e.zoneCount <= 0) {
        return;
    }
    // Chebyshev distance from every tile to the zone, out to the band.
    const int unreached = 1 << 30;
    std::vector<int> distance(WORLD_W * WORLD_H, unreached);
    std::vector<std::pair<int, int>> ring;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (e.zoneTile[x][y] != 0) {
                distance[static_cast<std::size_t>(tile_index(x, y))] = 0;
                ring.emplace_back(x, y);
            }
        }
    }
    for (int d = 1; d <= kZoneLandBand && !ring.empty(); ++d) {
        std::vector<std::pair<int, int>> next;
        for (const auto &at : ring) {
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const int nx = at.first + dx;
                    const int ny = at.second + dy;
                    if (Micropolis::testBounds(nx, ny) &&
                        distance[static_cast<std::size_t>(tile_index(nx, ny))] == unreached) {
                        distance[static_cast<std::size_t>(tile_index(nx, ny))] = d;
                        next.emplace_back(nx, ny);
                    }
                }
            }
        }
        ring.swap(next);
    }
    const int block = sim->landValueMap.MAP_BLOCKSIZE;
    for (int x = 0; x < WORLD_W; x += block) {
        for (int y = 0; y < WORLD_H; y += block) {
            const int value = sim->landValueMap.worldGet(x, y);
            if (value <= 0) {
                continue;
            }
            // Distance in tiles from this cell to the nearest zone tile.
            int best = 1 << 30;
            for (int cx = x; cx < x + block && cx < WORLD_W; ++cx) {
                for (int cy = y; cy < y + block && cy < WORLD_H; ++cy) {
                    best = std::min(best, distance[static_cast<std::size_t>(tile_index(cx, cy))]);
                }
            }
            if (best > kZoneLandBand) {
                continue;
            }
            const int cut = kZoneLandInside - kZoneLandInside * best / (kZoneLandBand + 1);
            sim->landValueMap.worldSet(x, y, static_cast<Byte>(std::max(1, value - cut)));
        }
    }
}

void clear(Micropolis *sim)
{
    std::memset(&sim->cityEvents, 0, sizeof(sim->cityEvents));
    sim->cityEvents.lastStepTime = sim->cityTime;
}

bool any_active(const Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    return e.riotSteps > 0 || e.zoneCount > 0 || e.lionActive != 0 || e.prisonSteps > 0;
}

bool in_zone(const Micropolis *sim, int x, int y)
{
    if (!Micropolis::testBounds(x, y)) {
        return false;
    }
    return sim->cityEvents.zoneCount > 0 && sim->cityEvents.zoneTile[x][y] != 0;
}

bool footprint_in_zone(const Micropolis *sim, int left, int top, int w, int h)
{
    if (sim->cityEvents.zoneCount <= 0) {
        return false;
    }
    for (int x = left; x < left + w; ++x) {
        for (int y = top; y < top + h; ++y) {
            if (in_zone(sim, x, y)) {
                return true;
            }
        }
    }
    return false;
}

bool edit_blocked(const Micropolis *sim, int tool, int x, int y)
{
    if (sim->cityEvents.zoneCount <= 0 || tool == TOOL_QUERY) {
        return false;
    }
    // Footprints as the engine places them: the click is one tile in from
    // the top-left corner of a 3x3, 4x4 or 6x6 building.
    int size = 1;
    switch (tool) {
    case TOOL_RESIDENTIAL:
    case TOOL_COMMERCIAL:
    case TOOL_INDUSTRIAL:
    case TOOL_FIRESTATION:
    case TOOL_POLICESTATION:
        size = 3;
        break;
    case TOOL_STADIUM:
    case TOOL_SEAPORT:
    case TOOL_COALPOWER:
    case TOOL_NUCLEARPOWER:
        size = 4;
        break;
    case TOOL_AIRPORT:
        size = 6;
        break;
    default:
        size = 1;
        break;
    }
    if (size == 1) {
        return in_zone(sim, x, y);
    }
    return footprint_in_zone(sim, x - 1, y - 1, size, size);
}

std::vector<std::pair<int, int>> zone_blocks(const Micropolis *sim)
{
    std::vector<std::pair<int, int>> out;
    const CityEvents &e = sim->cityEvents;
    for (int i = 0; i < e.zoneCount; ++i) {
        out.emplace_back(e.zoneBlock[i][0], e.zoneBlock[i][1]);
    }
    return out;
}

LionView lion(const Micropolis *sim)
{
    LionView view;
    const CityEvents &e = sim->cityEvents;
    if (e.lionActive == 0) {
        return view;
    }
    view.active = true;
    view.x = e.lionX;
    view.y = e.lionY;
    view.dir = e.lionDir & 3;
    view.frame = ((e.lionX + e.lionY) / 4) & 1;
    return view;
}

std::vector<std::pair<int, int>> road_block_at(const Micropolis *sim, int x, int y)
{
    std::vector<std::pair<int, int>> out;
    if (!Micropolis::testBounds(x, y)) {
        return out;
    }
    const RoadBlocks map = label_road_blocks(sim);
    const int number = map.label[tile_index(x, y)];
    if (number < 0) {
        return out;
    }
    for (int ty = 0; ty < WORLD_H; ++ty) {
        for (int tx = 0; tx < WORLD_W; ++tx) {
            if (map.label[tile_index(tx, ty)] == number) {
                out.emplace_back(tx, ty);
            }
        }
    }
    return out;
}

std::vector<Barricade> barricades(const Micropolis *sim)
{
    std::vector<Barricade> out;
    const CityEvents &e = sim->cityEvents;
    if (e.zoneCount <= 0) {
        return out;
    }
    static const int kSide[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    auto outside = [&](int x, int y) { return Micropolis::testBounds(x, y) && e.zoneTile[x][y] == 0; };
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            if (e.zoneTile[x][y] == 0) {
                continue;
            }
            for (int side = 0; side < 4; ++side) {
                const int nx = x + kSide[side][0];
                const int ny = y + kSide[side][1];
                if (!outside(nx, ny)) {
                    continue; // In the zone, or the map edge.
                }
                if (street_at(sim, nx, ny)) {
                    // On the street, against the curb.
                    out.push_back(Barricade{nx, ny, (side + 2) & 3});
                } else {
                    out.push_back(Barricade{x, y, side});
                }
            }
            // Corner posts where two street-side lines meet at an outside
            // corner: north and west of this tile are both streets outside
            // the zone, and so is the tile between them.
            static const int kCorner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int c = 0; c < 4; ++c) {
                const int cx = x + kCorner[c][0];
                const int cy = y + kCorner[c][1];
                if (outside(x + kCorner[c][0], y) && outside(x, y + kCorner[c][1]) && outside(cx, cy) &&
                    street_at(sim, x + kCorner[c][0], y) && street_at(sim, x, y + kCorner[c][1]) &&
                    street_at(sim, cx, cy)) {
                    // The post goes in the diagonal tile's corner facing
                    // this one (NW diagonal -> its SE corner, and so on).
                    out.push_back(Barricade{cx, cy, 4 + ((c + 2) & 3)});
                }
            }
        }
    }
    return out;
}

} // namespace city_events

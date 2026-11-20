// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "city_events.hpp"

#include "micropolis.h"

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

constexpr int kBlocksW = (WORLD_W + kZoneBlockTiles - 1) / kZoneBlockTiles;
constexpr int kBlocksH = (WORLD_H + kZoneBlockTiles - 1) / kZoneBlockTiles;

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
    return police_at_block(sim, x / kZoneBlockTiles, y / kZoneBlockTiles);
}

bool zone_has(const CityEvents &e, int bx, int by)
{
    for (int i = 0; i < e.zoneCount; ++i) {
        if (e.zoneBlock[i][0] == bx && e.zoneBlock[i][1] == by) {
            return true;
        }
    }
    return false;
}

int developed_in_block(const Micropolis *sim, int bx, int by)
{
    int count = 0;
    for (int x = bx * kZoneBlockTiles; x < std::min(WORLD_W, (bx + 1) * kZoneBlockTiles); ++x) {
        for (int y = by * kZoneBlockTiles; y < std::min(WORLD_H, (by + 1) * kZoneBlockTiles); ++y) {
            const int tile = tile_at(sim, x, y);
            // Buildings only (zones, service buildings, landmarks), not
            // roads or power lines: a zone forms in a neighbourhood.
            if ((tile >= RESBASE && tile < LASTZONE) || tile == SPECIAL_BODY) {
                ++count;
            }
        }
    }
    return count;
}

int zone_police_average(const Micropolis *sim)
{
    const CityEvents &e = sim->cityEvents;
    if (e.zoneCount <= 0) {
        return 0;
    }
    int total = 0;
    for (int i = 0; i < e.zoneCount; ++i) {
        total += police_at_block(sim, e.zoneBlock[i][0], e.zoneBlock[i][1]);
    }
    return total / e.zoneCount;
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

// ---- Autonomous Zone ----------------------------------------------------

bool place_zone(Micropolis *sim, int bx, int by, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    e.zoneCount = 0;
    for (int i = 0; i < kZoneStartBlocks; ++i) {
        e.zoneBlock[i][0] = static_cast<unsigned char>(bx + i);
        e.zoneBlock[i][1] = static_cast<unsigned char>(by);
        ++e.zoneCount;
    }
    e.zoneClock = kZoneReviewSteps;
    sim->invalidateMaps();
    notes.push_back(place_note(info(Kind::AutonomousZone).notice, bx * kZoneBlockTiles + kZoneBlockTiles,
                               by * kZoneBlockTiles + kZoneBlockTiles / 2));
    return true;
}

bool pick_zone_spot(const Micropolis *sim, int &best_x, int &best_y)
{
    // The developed pair of side-by-side blocks with the least police; a
    // tie goes to the more built-up pair, then to the first in the scan.
    // Not against the map edge, so the barricades show on every side (the
    // map's last row of blocks is also only half a block).
    int best_police = 1 << 30;
    int best_built = -1;
    best_x = -1;
    best_y = -1;
    for (int by = 1; (by + 2) * kZoneBlockTiles <= WORLD_H; ++by) {
        for (int bx = 1; (bx + 3) * kZoneBlockTiles <= WORLD_W; ++bx) {
            const int built = developed_in_block(sim, bx, by) + developed_in_block(sim, bx + 1, by);
            if (built < 36) {
                continue;
            }
            const int police = police_at_block(sim, bx, by) + police_at_block(sim, bx + 1, by);
            if (police < best_police || (police == best_police && built > best_built)) {
                best_police = police;
                best_built = built;
                best_x = bx;
                best_y = by;
            }
        }
    }
    return best_x >= 0;
}

void review_zone(Micropolis *sim, std::vector<Note> &notes)
{
    CityEvents &e = sim->cityEvents;
    const int police = zone_police_average(sim);
    if (police < kZoneGrowBelow) {
        if (e.zoneCount >= kZoneMaxBlocks) {
            return;
        }
        int pick_x = -1;
        int pick_y = -1;
        int pick_police = 1 << 30;
        int pick_built = -1;
        for (int i = 0; i < e.zoneCount; ++i) {
            static const int kSide[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            for (const auto &side : kSide) {
                const int nx = e.zoneBlock[i][0] + side[0];
                const int ny = e.zoneBlock[i][1] + side[1];
                if (nx < 0 || ny < 0 || nx >= kBlocksW || ny >= kBlocksH || zone_has(e, nx, ny)) {
                    continue;
                }
                const int p = police_at_block(sim, nx, ny);
                const int built = developed_in_block(sim, nx, ny);
                if (p < pick_police || (p == pick_police && built > pick_built)) {
                    pick_police = p;
                    pick_built = built;
                    pick_x = nx;
                    pick_y = ny;
                }
            }
        }
        if (pick_x >= 0) {
            e.zoneBlock[e.zoneCount][0] = static_cast<unsigned char>(pick_x);
            e.zoneBlock[e.zoneCount][1] = static_cast<unsigned char>(pick_y);
            ++e.zoneCount;
            sim->invalidateMaps();
            notes.push_back(place_note("The Autonomous Zone has grown. Police coverage there is too low.",
                                       pick_x * kZoneBlockTiles + kZoneBlockTiles / 2,
                                       pick_y * kZoneBlockTiles + kZoneBlockTiles / 2));
        }
        return;
    }
    if (police >= kZoneShrinkAt) {
        // Give up the best-policed block; a tie gives up the newest.
        int drop = 0;
        int drop_police = -1;
        for (int i = 0; i < e.zoneCount; ++i) {
            const int p = police_at_block(sim, e.zoneBlock[i][0], e.zoneBlock[i][1]);
            if (p >= drop_police) {
                drop_police = p;
                drop = i;
            }
        }
        const int gone_x = e.zoneBlock[drop][0] * kZoneBlockTiles + kZoneBlockTiles / 2;
        const int gone_y = e.zoneBlock[drop][1] * kZoneBlockTiles + kZoneBlockTiles / 2;
        for (int i = drop; i + 1 < e.zoneCount; ++i) {
            e.zoneBlock[i][0] = e.zoneBlock[i + 1][0];
            e.zoneBlock[i][1] = e.zoneBlock[i + 1][1];
        }
        --e.zoneCount;
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
        if (!pick_zone_spot(sim, x, y)) {
            return "An Autonomous Zone needs a built-up neighborhood.";
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
        int bx = 0;
        int by = 0;
        pick_zone_spot(sim, bx, by);
        if (!place_zone(sim, bx, by, notes)) {
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

bool start_zone_at(Micropolis *sim, int block_x, int block_y, std::vector<Note> &notes)
{
    if (sim->cityEvents.zoneCount > 0 || block_x < 0 || block_y < 0 || block_x + kZoneStartBlocks > kBlocksW ||
        block_y >= kBlocksH) {
        return false;
    }
    sim->cityEvents.lastStepTime = sim->cityTime;
    if (!place_zone(sim, block_x, block_y, notes)) {
        return false;
    }
    // Show on the land value map now; the engine's next scan keeps it.
    apply_land(sim);
    return true;
}

void random_event(Micropolis *sim, std::vector<Note> &notes)
{
    Rng rng(seed_for(sim, 101));
    const int roll = rng.below(8);
    if (roll >= kKindCount) {
        return; // Still nothing happens, half the time.
    }
    const Kind kind = static_cast<Kind>(roll);
    if (!unavailable_reason(sim, kind).empty()) {
        return; // No zoo, no prison, or one already going: nothing.
    }
    start(sim, kind, notes);
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
    const int block = sim->landValueMap.MAP_BLOCKSIZE;
    for (int x = 0; x < WORLD_W; x += block) {
        for (int y = 0; y < WORLD_H; y += block) {
            const int value = sim->landValueMap.worldGet(x, y);
            if (value <= 0) {
                continue;
            }
            // Distance in tiles from this 2x2 cell to the nearest zone tile.
            int best = 1 << 30;
            for (int i = 0; i < e.zoneCount; ++i) {
                const int left = e.zoneBlock[i][0] * kZoneBlockTiles;
                const int top = e.zoneBlock[i][1] * kZoneBlockTiles;
                const int right = left + kZoneBlockTiles - 1;
                const int bottom = top + kZoneBlockTiles - 1;
                int d = 1 << 30;
                for (int cx = x; cx < x + block; ++cx) {
                    for (int cy = y; cy < y + block; ++cy) {
                        const int ex = cx < left ? left - cx : (cx > right ? cx - right : 0);
                        const int ey = cy < top ? top - cy : (cy > bottom ? cy - bottom : 0);
                        d = std::min(d, std::max(ex, ey));
                    }
                }
                best = std::min(best, d);
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
    return zone_has(sim->cityEvents, x / kZoneBlockTiles, y / kZoneBlockTiles);
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

std::vector<Barricade> barricades(const Micropolis *sim)
{
    std::vector<Barricade> out;
    const CityEvents &e = sim->cityEvents;
    static const int kSide[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (int i = 0; i < e.zoneCount; ++i) {
        const int bx = e.zoneBlock[i][0];
        const int by = e.zoneBlock[i][1];
        for (int side = 0; side < 4; ++side) {
            if (zone_has(e, bx + kSide[side][0], by + kSide[side][1])) {
                continue;
            }
            for (int k = 0; k < kZoneBlockTiles; ++k) {
                int x = bx * kZoneBlockTiles;
                int y = by * kZoneBlockTiles;
                if (side == 0 || side == 2) {
                    x += k;
                    y += side == 0 ? 0 : kZoneBlockTiles - 1;
                } else {
                    y += k;
                    x += side == 3 ? 0 : kZoneBlockTiles - 1;
                }
                if (Micropolis::testBounds(x, y)) {
                    out.push_back(Barricade{x, y, side});
                }
            }
        }
    }
    return out;
}

} // namespace city_events

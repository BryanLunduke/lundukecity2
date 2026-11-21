// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Headless checks for the vendored-engine edits: voteProblems() must not
// read past the problem table, and destroy() must release sprites.

#include "micropolis.h"
#include "text.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <malloc.h>
#include <string>
#include <vector>

#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33)
#define LC_HAVE_MALLINFO2 1
#endif
#endif

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

} // namespace

int lunduke_city_test_vote_problems()
{
    Micropolis sim;
    // problemVotes[PROBNUM] is problemOrder[0] in this class layout.
    // The old loop bound wrote that slot.
    if (&sim.problemVotes[PROBNUM] != sim.problemOrder) {
        return fail(1, "problemOrder does not follow problemVotes");
    }

    short table[PROBNUM + 1];
    for (int i = 0; i < PROBNUM; ++i) {
        table[i] = 0;
    }
    table[CVP_CRIME] = 1000;
    // The old bound consulted this extra element. A fixed loop never reads it.
    table[PROBNUM] = 1000;

    sim.problemOrder[0] = 1234;
    sim.voteProblems(table);

    int sum = 0;
    for (int i = 0; i < PROBNUM; ++i) {
        sum += sim.problemVotes[i];
    }
    // One guaranteed crime vote every PROBNUM steps, and the loop stops
    // at 600 attempts, so crime receives 60 votes and nothing else does.
    if (sim.problemVotes[CVP_CRIME] != 60 || sum != 60) {
        std::fprintf(stderr, "crime votes %d sum %d\n", sim.problemVotes[CVP_CRIME], sum);
        return fail(2, "problem votes did not stay inside the table");
    }
    if (sim.problemOrder[0] != 1234) {
        std::fprintf(stderr, "problemOrder[0] is %d\n", sim.problemOrder[0]);
        return fail(3, "voteProblems wrote past problemVotes into problemOrder");
    }
    return 0;
}

int lunduke_city_test_destroy_sprites()
{
    {
        Micropolis sim;
        // newSprite() recycles freeSprites, so both sprites have to be
        // created before one is retired onto the free list.
        sim.makeSprite(SPRITE_BUS, 8, 8);
        sim.makeSprite(SPRITE_SHIP, 24, 24);
        SimSprite *bus = sim.globalSprites[SPRITE_BUS];
        if (bus == nullptr || sim.globalSprites[SPRITE_SHIP] == nullptr) {
            return fail(4, "bus and ship sprites were not created");
        }
        sim.destroySprite(bus);
        if (sim.freeSprites == nullptr || sim.spriteList == nullptr) {
            return fail(5, "expected a sprite on the free list and the active list");
        }
        sim.destroy();
        if (sim.spriteList != nullptr || sim.freeSprites != nullptr) {
            return fail(6, "destroy() left sprite lists allocated");
        }
        for (int i = 0; i < SPRITE_COUNT; ++i) {
            if (sim.globalSprites[i] != nullptr) {
                return fail(7, "destroy() left a global sprite pointer");
            }
        }
        sim.destroy();
    }

    auto exercise = []() {
        Micropolis sim;
        sim.makeSprite(SPRITE_AIRPLANE, 16, 16);
        sim.makeSprite(SPRITE_MONSTER, 48, 48);
        SimSprite *plane = sim.globalSprites[SPRITE_AIRPLANE];
        if (plane != nullptr) {
            sim.destroySprite(plane);
        }
    };
    for (int i = 0; i < 5; ++i) {
        exercise();
    }
    // mallinfo2 needs glibc 2.33. Sniper's runtime is 2.31 and still has mallinfo.
    // Other C libraries (MinGW, musl) skip the heap check.
    long baseline = -1;
#ifdef LC_HAVE_MALLINFO2
    baseline = static_cast<long>(mallinfo2().uordblks);
#elif defined(__GLIBC__) || defined(__linux__)
    baseline = static_cast<long>(mallinfo().uordblks);
#endif
    if (baseline < 0) {
        return 0;
    }
    for (int i = 0; i < 40; ++i) {
        exercise();
    }
    long grown = -1;
#ifdef LC_HAVE_MALLINFO2
    grown = static_cast<long>(mallinfo2().uordblks) - baseline;
#elif defined(__GLIBC__) || defined(__linux__)
    grown = static_cast<long>(mallinfo().uordblks) - baseline;
#endif
    if (grown > 2048) {
        std::fprintf(stderr, "sprite heap grew by %ld bytes\n", grown);
        return fail(8, "destroy() leaked sprites");
    }
    return 0;
}

int lunduke_city_test_residential_valve()
{
    Micropolis sim;
    // The constructor's simInit() leaves initSimLoad set, so every tick stays
    // on phase 0. Valves start at 0. The first tick bumps simCycle to an odd
    // value and skips setValves; the second tick is the even cycle that runs it.
    // History buffers are HISTORY_LENGTH bytes (240 shorts), not 240 elements
    // past that. Only the slots setValves reads need to be defined.
    sim.resHist[1] = 0;
    sim.comHist[1] = 0;
    sim.indHist[1] = 0;
    auto prime = [&sim]() {
        sim.resPop = 800;
        sim.comPop = 0;
        sim.indPop = 0;
        sim.resHist[1] = 0;
        sim.comHist[1] = 0;
        sim.indHist[1] = 0;
        sim.cityTax = 7;
        sim.gameLevel = LEVEL_EASY;
        sim.resCap = false;
        sim.comCap = false;
        sim.indCap = false;
    };
    prime();
    sim.setSpeed(3);
    sim.setPasses(1);
    sim.simTick();
    prime();
    sim.simTick();

    float residential = 0;
    float commercial = 0;
    float industrial = 0;
    sim.getDemands(&residential, &commercial, &industrial);
    // Residential demand here is a shrinking city (ratio about 0.02), so the
    // valve moves down. The old bug stored the industrial ratio (clamped to
    // 2) in resRatio and pushed residential demand up instead.
    if (residential >= 0.0f) {
        std::fprintf(stderr, "res %f com %f ind %f\n", residential, commercial, industrial);
        return fail(9, "residential demand followed the industrial ratio");
    }
    // Unclamped industrial ratio is 5, which lands on the valve cap (demand 1).
    // Clamped to indRatioMax it lands near 600/1500.
    if (industrial <= 0.0f || industrial >= 1.0f) {
        std::fprintf(stderr, "res %f com %f ind %f\n", residential, commercial, industrial);
        return fail(10, "industrial demand was not clamped to indRatioMax");
    }
    return 0;
}

int lunduke_city_test_message_sound()
{
    struct Heard {
        std::vector<std::string> names;
    } heard;
    Micropolis sim;
    sim.setEnableSound(true);
    sim.setAutoGoto(false);
    sim.callbackData = &heard;
    sim.callbackHook = [](Micropolis *, void *data, const char *name, const char *, va_list args) {
        if (name == nullptr || std::string(name) != "makeSound") {
            return;
        }
        (void)va_arg(args, char *);
        const char *sound = va_arg(args, char *);
        (void)va_arg(args, int);
        (void)va_arg(args, int);
        if (sound != nullptr && sound[0] != '\0') {
            static_cast<Heard *>(data)->names.emplace_back(sound);
        }
    };
    sim.sendMessage(MESSAGE_FIRE_REPORTED, 4, 4, true, false);
    bool siren = false;
    for (const auto &sound : heard.names) {
        if (sound == "Siren") {
            siren = true;
        }
    }
    if (!siren) {
        std::fprintf(stderr, "sounds %zu\n", heard.names.size());
        return fail(11, "a fire message did not play the siren");
    }
    return 0;
}

int lunduke_city_test_tokyo_monster()
{
#ifndef LUNDUKE_CITY_ASSET_DIR
    return fail(13, "Tokyo monster test has no asset directory");
#else
    const std::string dir = std::string(LUNDUKE_CITY_ASSET_DIR) + "/res";
    for (int seed = 1; seed <= 8; ++seed) {
        Micropolis sim;
        sim.resourceDir = dir;
        if (!sim.loadScenario(SC_TOKYO)) {
            std::fprintf(stderr, "resource %s\n", dir.c_str());
            return fail(13, "could not load the Tokyo scenario");
        }
        sim.setSpeed(3);
        sim.setPasses(1);
        sim.primeRandom(seed);
        sim.makeMonster();
        SimSprite *born = sim.getSprite(SPRITE_MONSTER);
        if (born == nullptr || born->frame == 0) {
            std::fprintf(stderr, "seed %d created no monster\n", seed);
            return fail(14, "makeMonster did not leave a visible monster");
        }
        for (int tick = 0; tick < 100; ++tick) {
            sim.simTick();
            SimSprite *live = sim.getSprite(SPRITE_MONSTER);
            if (live == nullptr || live->frame == 0) {
                std::fprintf(stderr, "seed %d died at tick %d\n", seed, tick);
                return fail(15, "Tokyo plus makeMonster vanished before 100 ticks");
            }
        }
    }
    return 0;
#endif
}

int lunduke_city_test_history_starts_clear()
{
    Micropolis sim;
    const int misc_last = (MISC_HISTORY_LENGTH / 2) - 1;
    if (sim.resHist == nullptr || sim.comHist == nullptr || sim.indHist == nullptr || sim.moneyHist == nullptr ||
        sim.pollutionHist == nullptr || sim.crimeHist == nullptr || sim.miscHist == nullptr || sim.graph10Max != 0 ||
        sim.resHist[0] != 0 || sim.resHist[118] != 0 || sim.resHist[238] != 0 || sim.comHist[238] != 0 ||
        sim.indHist[238] != 0 || sim.moneyHist[0] != 0 || sim.pollutionHist[0] != 0 || sim.crimeHist[0] != 0 ||
        sim.miscHist[0] != 0 || sim.miscHist[misc_last] != 0) {
        return fail(12, "history buffers were not zero before initGraphMax");
    }
    return 0;
}

struct SimulatorProbe {
    static void scan(Micropolis &sim, int x1, int x2) { sim.mapScan(x1, x2); }

    static void askChurch(Micropolis &sim, int x, int y)
    {
        sim.needHospital = 0;
        sim.needChurch = 1;
        sim.makeHospital(Position(x, y));
    }

    static bool churchPending(const Micropolis &sim) { return sim.needChurch != 0; }

    static void fire_zone(Micropolis &sim, int x, int y, int tile) { sim.fireZone(Position(x, y), tile); }

    static void fire_in_zone(Micropolis &sim, int x, int y, int tile) { sim.startFireInZone(x, y, tile); }
};

void fill_dirt(Micropolis &sim)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            sim.map[x][y] = DIRT;
        }
    }
}

// doRadTile decays when the next 16-bit roll's low 12 bits are clear.
// primeRandom stores the seed as the generator state, and the scanner
// draws one roll for a lone radiation tile.
int decay_seed()
{
    for (int seed = 1; seed < 2000000; ++seed) {
        std::uint64_t next = static_cast<std::uint64_t>(static_cast<unsigned int>(seed));
        next = next * 1103515245u + 12345u;
        const unsigned roll = static_cast<unsigned>((next & 0xffff00u) >> 8);
        if ((roll & 4095u) == 0) {
            return seed;
        }
    }
    return 0;
}

int lunduke_city_test_civic_gap_tiles()
{
    if (!civicGapTile(SPECIAL_MAYOR) || !civicGapTile(SPECIAL_CASINO) || !civicGapTile(SPECIAL_AMUSEMENT) ||
        civicGapTile(RADTILE) || civicGapTile(SPECIAL_ZOO) || civicGapTile(SPECIAL_PRISON) ||
        civicGapTile(SPECIAL_MARINA) || civicGapTile(SPECIAL_FOUNTAIN)) {
        return fail(16, "the civic-gap check does not match tiles 53-55");
    }

    Micropolis sim;
    fill_dirt(sim);
    sim.autoBulldoze = true;
    sim.setFunds(100000);
    const EditingTool lines[] = {TOOL_ROAD, TOOL_RAILROAD, TOOL_WIRE};
    const int kept[] = {SPECIAL_MAYOR,  SPECIAL_CASINO,   SPECIAL_AMUSEMENT, SPECIAL_ZOO,  SPECIAL_PARK,
                        SPECIAL_LIBRARY, SPECIAL_PRISON, SPECIAL_MARINA,   SPECIAL_FOUNTAIN, SPECIAL_BODY};
    for (int tile : kept) {
        for (EditingTool tool : lines) {
            sim.map[10][10] = static_cast<unsigned short>(tile | BULLBIT | BURNBIT | ZONEBIT);
            const long before = static_cast<long>(sim.totalFunds);
            const ToolResult result = sim.doTool(tool, 10, 10);
            if (result == TOOLRESULT_OK || (sim.map[10][10] & LOMASK) != tile ||
                static_cast<long>(sim.totalFunds) != before) {
                std::fprintf(stderr, "tool %d paved tile %d into %u\n", static_cast<int>(tool), tile, sim.map[10][10]);
                return fail(17, "a road, rail, or wire paved through a special centre");
            }
        }
    }
    const int cleared[] = {WOODS, RUBBLE, RADTILE};
    for (int tile : cleared) {
        sim.map[10][10] = static_cast<unsigned short>(tile | BULLBIT | BURNBIT);
        const ToolResult result = sim.doTool(TOOL_ROAD, 10, 10);
        const int after = sim.map[10][10] & LOMASK;
        if (result != TOOLRESULT_OK || after == tile || after == DIRT) {
            std::fprintf(stderr, "tree-like tile %d became %d result %d\n", tile, after, static_cast<int>(result));
            return fail(18, "auto-bulldoze stopped clearing trees, rubble, or radiation");
        }
    }

    const int seed = decay_seed();
    if (seed == 0) {
        return fail(19, "could not find a radiation-decay roll");
    }
    const int centres[] = {SPECIAL_MAYOR, SPECIAL_CASINO, SPECIAL_AMUSEMENT};
    for (int tile : centres) {
        fill_dirt(sim);
        sim.map[8][8] = static_cast<unsigned short>(tile | ZONEBIT | BULLBIT | BURNBIT);
        sim.primeRandom(seed);
        SimulatorProbe::scan(sim, 8, 9);
        if ((sim.map[8][8] & LOMASK) != tile) {
            std::fprintf(stderr, "tile %d decayed to %u\n", tile, sim.map[8][8]);
            return fail(20, "a civic centre decayed like radiation");
        }
    }
    fill_dirt(sim);
    sim.map[8][8] = RADTILE;
    sim.primeRandom(seed);
    SimulatorProbe::scan(sim, 8, 9);
    if ((sim.map[8][8] & LOMASK) != DIRT) {
        std::fprintf(stderr, "radiation became %u\n", sim.map[8][8]);
        return fail(21, "real radiation no longer decays");
    }
    return 0;
}

int lunduke_city_test_all_churches()
{
    // The church sheet draws 956-1019, so all eight types are drawable and
    // the grown church is whichever type the roll picked.
    if (kShippedTileCount != 1020 || CHURCH0BASE + 8 >= kShippedTileCount || CHURCH7LAST >= kShippedTileCount ||
        kShippedTileCount > SPECIAL_ZOO) {
        return fail(22, "the drawable tile range no longer covers every church");
    }
    Micropolis sim;
    fill_dirt(sim);
    sim.primeRandom(1);
    bool seen[8] = {};
    for (int i = 0; i < 256; ++i) {
        SimulatorProbe::askChurch(sim, 20, 20);
        if (SimulatorProbe::churchPending(sim)) {
            return fail(24, "a church request was left pending");
        }
        const int centre = sim.map[20][20] & LOMASK;
        int type = -1;
        if (centre == CHURCH0) {
            type = 0;
        } else if (centre >= CHURCH1 && centre <= CHURCH7 && (centre - CHURCH1) % 9 == 0) {
            type = 1 + (centre - CHURCH1) / 9;
        }
        if (type < 0) {
            std::fprintf(stderr, "centre %d\n", centre);
            return fail(25, "a grown church centre is not a church type");
        }
        seen[type] = true;
        const int base = type == 0 ? CHURCH0BASE : CHURCH1BASE + (type - 1) * 9;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int tile = sim.map[20 + dx][20 + dy] & LOMASK;
                if (tile != base + (dy + 1) * 3 + (dx + 1) || tile >= kShippedTileCount) {
                    std::fprintf(stderr, "church type %d cell %d\n", type, tile);
                    return fail(26, "a grown church block is not its type's nine tiles");
                }
            }
        }
    }
    for (int type = 0; type < 8; ++type) {
        if (!seen[type]) {
            std::fprintf(stderr, "type %d\n", type);
            return fail(23, "a church type never grew");
        }
    }
    return 0;
}

// A burning church is a 3x3 building. The fire code once sized every tile
// above the port as 4x4, which flagged the ring of tiles east and south of
// a church as bulldozable and let a $1 bulldoze punch holes in the
// neighbouring buildings.
int lunduke_city_test_church_fire_footprint()
{
    for (int type = 0; type < 8; ++type) {
        const int base = type == 0 ? CHURCH0BASE : CHURCH1BASE + (type - 1) * 9;
        for (int route = 0; route < 2; ++route) {
            Micropolis sim;
            fill_dirt(sim);
            for (int x = 15; x <= 25; ++x) {
                for (int y = 15; y <= 25; ++y) {
                    sim.map[x][y] = static_cast<unsigned short>(RESBASE + 1) | BURNBIT;
                }
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    sim.map[20 + dx][20 + dy] = static_cast<unsigned short>(base + (dy + 1) * 3 + dx + 1) | BURNBIT;
                }
            }
            sim.map[20][20] |= ZONEBIT;
            const int centre = base + 4;
            if (route == 0) {
                SimulatorProbe::fire_zone(sim, 20, 20, centre);
            } else {
                SimulatorProbe::fire_in_zone(sim, 20, 20, centre);
            }
            for (int x = 15; x <= 25; ++x) {
                for (int y = 15; y <= 25; ++y) {
                    const bool inside = x >= 19 && x <= 21 && y >= 19 && y <= 21;
                    const bool flagged = (sim.map[x][y] & BULLBIT) != 0;
                    if (inside != flagged) {
                        std::fprintf(stderr, "type %d route %d tile %d,%d flagged %d\n", type, route, x, y, flagged);
                        return fail(27, "a burning church flagged the wrong tiles bulldozable");
                    }
                }
            }
        }
    }
    return 0;
}

int main()
{
    if (const int code = lunduke_city_test_vote_problems()) {
        return code;
    }
    if (const int code = lunduke_city_test_destroy_sprites()) {
        return code;
    }
    if (const int code = lunduke_city_test_residential_valve()) {
        return code;
    }
    if (const int code = lunduke_city_test_message_sound()) {
        return code;
    }
    if (const int code = lunduke_city_test_history_starts_clear()) {
        return code;
    }
    if (const int code = lunduke_city_test_tokyo_monster()) {
        return code;
    }
    if (const int code = lunduke_city_test_civic_gap_tiles()) {
        return code;
    }
    if (const int code = lunduke_city_test_all_churches()) {
        return code;
    }
    if (const int code = lunduke_city_test_church_fire_footprint()) {
        return code;
    }
    return 0;
}

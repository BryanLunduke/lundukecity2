// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Headless checks for the vendored-engine edits: voteProblems() must not
// read past the problem table, and destroy() must release sprites.

#include "micropolis.h"
#include "text.h"

#include <cstdarg>
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
    return 0;
}

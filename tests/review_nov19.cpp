// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 19 review (balance and playability).

#include "city_session.hpp"
#include "messages.hpp"
#include "tools.hpp"

#include "micropolis.h"
#include "text.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Nov19DisasterProbe {
    static void roll(Micropolis &sim) { sim.doDisasters(); }
};

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

struct Sent {
    int number = 0;
    int x = 0;
    int y = 0;
};

// Records every engine message the sim sends.
void listen(Micropolis &sim, std::vector<Sent> &heard)
{
    sim.callbackData = &heard;
    sim.callbackHook = [](Micropolis *, void *data, const char *name, const char *params, va_list args) {
        if (name == nullptr || std::string(name) != "update" || params == nullptr ||
            std::string(params) != "sdddbb") {
            return;
        }
        const char *what = va_arg(args, char *);
        if (what == nullptr || std::string(what) != "message") {
            return;
        }
        Sent sent;
        sent.number = va_arg(args, int);
        sent.x = va_arg(args, int);
        sent.y = va_arg(args, int);
        static_cast<std::vector<Sent> *>(data)->push_back(sent);
    };
}

int count_of(const std::vector<Sent> &heard, int number)
{
    int n = 0;
    for (const Sent &sent : heard) {
        if (sent.number == number) {
            ++n;
        }
    }
    return n;
}

// #4: an Easy city has no random disasters for five years, then they
// come back; a spreading fire gets a notice that points at it.
int check_easy_disasters()
{
    for (int pass = 0; pass < 2; ++pass) {
        // Heap, not stack: a Micropolis is several megabytes.
        auto owned = std::make_unique<Micropolis>();
        Micropolis &sim = *owned;
        sim.generateSomeCity(3);
        sim.setAutoGoto(false);
        sim.setGameLevel(LEVEL_EASY);
        sim.setEnableDisasters(true);
        // Houses everywhere, so a random fire always finds something to burn.
        for (int x = 0; x < WORLD_W; ++x) {
            for (int y = 0; y < WORLD_H; ++y) {
                sim.map[x][y] = static_cast<unsigned short>((HOUSE + 1) | BURNBIT | BULLBIT);
            }
        }
        std::vector<Sent> heard;
        listen(sim, heard);
        sim.cityTime = pass == 0 ? 4 * 48 + 47 : 5 * 48;
        for (int i = 0; i < 20000; ++i) {
            Nov19DisasterProbe::roll(sim);
        }
        const int fires = count_of(heard, MESSAGE_FIRE_REPORTED);
        if (pass == 0 && fires != 0) {
            std::fprintf(stderr, "fires in grace %d\n", fires);
            return fail(1, "an Easy city had a random fire in its first five years");
        }
        if (pass == 1 && fires == 0) {
            return fail(2, "random fires did not come back after the Easy grace years");
        }
    }
    auto hard_owned = std::make_unique<Micropolis>();
    Micropolis &hard = *hard_owned;
    hard.generateSomeCity(3);
    hard.setAutoGoto(false);
    hard.setGameLevel(LEVEL_HARD);
    hard.setEnableDisasters(true);
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            hard.map[x][y] = static_cast<unsigned short>((HOUSE + 1) | BURNBIT | BULLBIT);
        }
    }
    std::vector<Sent> hard_heard;
    listen(hard, hard_heard);
    hard.cityTime = 10;
    for (int i = 0; i < 20000; ++i) {
        Nov19DisasterProbe::roll(hard);
    }
    if (count_of(hard_heard, MESSAGE_FIRE_REPORTED) == 0) {
        return fail(3, "the Easy grace years reached Hard");
    }

    auto city_owned = std::make_unique<Micropolis>();
    Micropolis &city = *city_owned;
    city.generateSomeCity(3);
    city.setAutoGoto(false);
    std::vector<Sent> heard;
    listen(city, heard);
    city.cityTime = 64;
    city.firePop = Micropolis::FIRE_SPREADING_TILES;
    city.fireSpotX = 40;
    city.fireSpotY = 30;
    city.sendMessages();
    bool pointed = false;
    for (const Sent &sent : heard) {
        if (sent.number == MESSAGE_FIRE_SPREADING && sent.x == 40 && sent.y == 30) {
            pointed = true;
        }
    }
    if (!pointed) {
        return fail(4, "a spreading fire did not send a notice at the fire");
    }
    if (message_for_number(MESSAGE_FIRE_SPREADING).find("Fire spreading") != 0 ||
        !notice_is_disaster(message_for_number(MESSAGE_FIRE_SPREADING))) {
        return fail(5, "the Fire spreading notice has no text or is not a disaster");
    }
    // The same fire two months later is not news again; a year later it
    // gets one reminder.
    heard.clear();
    city.cityTime = 64 + 8;
    city.sendMessages();
    if (count_of(heard, MESSAGE_FIRE_SPREADING) != 0) {
        return fail(7, "a fire still burning was reported again two months later");
    }
    city.cityTime = 64 + 48;
    city.sendMessages();
    if (count_of(heard, MESSAGE_FIRE_SPREADING) != 1) {
        return fail(8, "a fire burning for a year got no reminder");
    }
    heard.clear();
    city.cityTime = 64 + 56;
    city.firePop = Micropolis::FIRE_SPREADING_TILES - 1;
    city.sendMessages();
    if (count_of(heard, MESSAGE_FIRE_SPREADING) != 0) {
        return fail(6, "a small fire was reported as spreading");
    }
    // A new outbreak after the fire died down is reported at once.
    city.cityTime = 64 + 64;
    city.firePop = Micropolis::FIRE_SPREADING_TILES;
    city.sendMessages();
    if (count_of(heard, MESSAGE_FIRE_SPREADING) != 1) {
        return fail(9, "a new outbreak was not reported");
    }
    return 0;
}

// #5: a city past 10,000 people collects 2% more tax per 1,000 people,
// at most 30%; smaller cities are unchanged.
int check_tax_growth()
{
    auto owned = std::make_unique<Micropolis>();
    Micropolis &sim = *owned;
    sim.generateSomeCity(3);
    sim.setGameLevel(LEVEL_MEDIUM);
    const int pops[] = {0, 10000, 10999, 15000, 25000, 200000};
    const int expect[] = {0, 0, 0, 10, 30, 30};
    sim.cityPop = 0;
    const Quad base = sim.projectBudget(0, 0, 0, 0, 2000, 100, 7).tax;
    if (base <= 0) {
        return fail(20, "the tax projection is empty");
    }
    for (int i = 0; i < 6; ++i) {
        sim.cityPop = pops[i];
        const Quad tax = sim.projectBudget(0, 0, 0, 0, 2000, 100, 7).tax;
        if (sim.taxGrowthPercent() != expect[i] || tax != base + base * expect[i] / 100) {
            std::fprintf(stderr, "pop %d percent %d tax %d base %d\n", pops[i], static_cast<int>(sim.taxGrowthPercent()),
                         static_cast<int>(tax), static_cast<int>(base));
            return fail(21, "big-city tax growth is not 2% per 1,000 people past 10,000, capped at 30%");
        }
    }
    return 0;
}

// #6: auto budget that the engine switched off for lack of money comes
// back on, with the player's funding, once the treasury has recovered.
// A player's own switch-off stays off.
int check_auto_budget_recovers()
{
    auto owned = std::make_unique<Micropolis>();
    Micropolis &sim = *owned;
    sim.generateSomeCity(3);
    std::vector<Sent> heard;
    listen(sim, heard);
    auto short_year = [&sim]() {
        sim.setAutoBudget(true);
        sim.roadFund = 400;
        sim.policeFund = 300;
        sim.fireFund = 300;
        sim.roadPercent = 1.0f;
        sim.policePercent = 0.0f;
        sim.firePercent = 0.1f;
        sim.roadPercentWanted = 1.0f;
        sim.policePercentWanted = 1.0f;
        sim.firePercentWanted = 1.0f;
        // What budget.cpp does when the year cannot be paid.
        sim.setAutoBudget(false);
        sim.autoBudgetForcedOff = true;
    };
    short_year();
    sim.taxFund = 500;
    sim.setFunds(1000);
    sim.restoreWantedFunding();
    if (sim.autoBudget || sim.policePercent != 0.0f) {
        return fail(30, "auto budget came back before the treasury could pay twice the request");
    }
    sim.setFunds(1600);
    sim.restoreWantedFunding();
    if (!sim.autoBudget || sim.autoBudgetForcedOff || sim.policePercent != 1.0f || sim.firePercent != 1.0f ||
        count_of(heard, MESSAGE_AUTO_BUDGET_RESTORED) != 1) {
        return fail(31, "auto budget and full funding did not come back once funds recovered");
    }
    if (message_for_number(MESSAGE_AUTO_BUDGET_RESTORED).find("Auto budget is back on") == std::string::npos) {
        return fail(32, "the auto budget notice has no text");
    }
    // The player turned it off (CitySession::set_auto_budget clears the
    // engine's flag): it stays off, whatever the treasury.
    short_year();
    sim.autoBudgetForcedOff = false;
    sim.setFunds(100000);
    sim.restoreWantedFunding();
    if (sim.autoBudget || sim.policePercent != 0.0f) {
        return fail(33, "auto budget came back after the player switched it off");
    }
    // Auto budget left on with a cut from an earlier short year: the cut
    // is lifted without a notice.
    short_year();
    sim.setAutoBudget(true);
    heard.clear();
    sim.restoreWantedFunding();
    if (sim.policePercent != 1.0f || count_of(heard, MESSAGE_AUTO_BUDGET_RESTORED) != 0) {
        return fail(34, "auto budget kept a short year's cut after the money came back");
    }
    return 0;
}

// #7: the short tool hints (all the handheld shows) say what an
// attraction needs before it earns anything.
int check_attraction_hints()
{
    const char *names[] = {"Casino", "Amusement Park", "Marina"};
    for (const char *name : names) {
        bool found = false;
        for (int i = 0; i < kToolCount; ++i) {
            const ToolDef *tool = tool_by_index(i);
            if (tool == nullptr || std::string(tool->name) != name) {
                continue;
            }
            found = true;
            const std::string hint = tool->hint;
            if (hint.find("power") == std::string::npos || hint.find("road") == std::string::npos ||
                hint.find("2,000 residents") == std::string::npos) {
                std::fprintf(stderr, "%s: %s\n", name, hint.c_str());
                return fail(40, "an attraction hint leaves out power, road or 2,000 residents");
            }
        }
        if (!found) {
            return fail(41, std::string("no tool named ") + name);
        }
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_easy_disasters()) {
        return r;
    }
    if (const int r = check_tax_growth()) {
        return r;
    }
    if (const int r = check_auto_budget_recovers()) {
        return r;
    }
    if (const int r = check_attraction_hints()) {
        return r;
    }
    std::puts("review_nov19 ok");
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the November 19 review (balance and playability).

#include "city_session.hpp"
#include "messages.hpp"
#include "civic.hpp"
#include "scenario_sidecar.hpp"
#include "placement_hint.hpp"
#include "tools.hpp"

#include "micropolis.h"
#include "text.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Reaches engine internals for the checks below (a friend of Micropolis).
struct Nov19DisasterProbe {
    static void roll(Micropolis &sim) { sim.doDisasters(); }
    static void growth(Micropolis &sim, Quad last_population, Quad res_pop)
    {
        sim.cityTime = 4;
        sim.cityPopLast = last_population;
        sim.categoryLast = 0;
        sim.resPop = res_pop;
        sim.comPop = 0;
        sim.indPop = 0;
        sim.dataCenterPop = 0;
        sim.checkGrowth();
    }
    static Quad people(Micropolis &sim) { return sim.getPopulation(); }
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

// #8: the power line tool has one name: the toolbar's.
int check_power_line_name()
{
    const ToolDef *lines = nullptr;
    for (int i = 0; i < kToolCount; ++i) {
        const ToolDef *tool = tool_by_index(i);
        if (tool != nullptr && tool->engine_id == TOOL_WIRE) {
            lines = tool;
        }
    }
    if (lines == nullptr || std::string(lines->name) != "Power lines" ||
        std::string(lines->hint) != "Power lines: $5. Underwater power line: $25") {
        return fail(50, "the power line tool hint does not use the toolbar name");
    }
    if (message_for_number(MESSAGE_UNDERWATER_WIRE) != "Underwater power line: $25" ||
        short_build_reason("No place to land a power line.") != "No power line landing" ||
        short_build_reason("Underwater power line: $25") == "No power line landing") {
        return fail(51, "a power line message still says wire");
    }
    return 0;
}

// #9: the NEWS line greys out a notice after six months and drops it
// after a year.
int check_news_staleness()
{
    if (news_staleness(0) != 0 || news_staleness(5) != 0 || news_staleness(6) != 1 || news_staleness(11) != 1 ||
        news_staleness(12) != 2 || news_staleness(14) != 2) {
        return fail(60, "news staleness is not 6 months grey, 12 months gone");
    }
    return 0;
}

// #11: Boston starts below 500, so its goal and meter ask for a rise.
int check_boston_goal()
{
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_BOSTON - SC_DULLSVILLE);
    if (std::string(def.goal) != "Raise the score above 500 by 2015.") {
        std::fprintf(stderr, "goal '%s'\n", def.goal);
        return fail(70, "the Boston goal does not ask to raise the score");
    }
    CitySession boston;
    if (!boston.load_scenario(SC_BOSTON)) {
        return fail(71, "Boston did not load");
    }
    const std::string meter = boston.scenario_meter();
    if (meter.find("raise it above 500 by 2015") == std::string::npos || meter.find("keep it") != std::string::npos) {
        std::fprintf(stderr, "meter '%s'\n", meter.c_str());
        return fail(72, "the Boston meter does not ask to raise the score");
    }
    return 0;
}

// #12: a city loaded without its sidecar knows the milestones it has
// already reached, and announces none of them.
int check_load_without_sidecar()
{
    const std::string path = "/tmp/lunduke-review-nov19-nosidecar.cty";
    std::remove(path.c_str());
    std::remove(scenario_sidecar_path(path).c_str());
    std::int64_t people = 0;
    {
        CitySession source;
        if (!source.load_scenario(SC_HAMBURG)) {
            return fail(80, "could not load a scenario city for the sidecar check");
        }
        // One step of the sim starts a new scan, which clears the live
        // census: a save made then stores no population, like a classic
        // save or one made at that moment of the month.
        source.set_speed(3);
        source.set_simulation_paused(false);
        source.tick();
        if (!source.save_city_as(path)) {
            return fail(80, "could not save a scenario city for the sidecar check");
        }
        people = source.evaluation().population;
    }
    std::remove(scenario_sidecar_path(path).c_str());
    if (people < 10000) {
        std::fprintf(stderr, "people %lld\n", static_cast<long long>(people));
        return fail(81, "the sidecar check city is too small to test");
    }
    CitySession loaded;
    if (!loaded.load_city(path)) {
        return fail(82, "the city without a sidecar did not load");
    }
    const civic::Ledger &ledger = loaded.ledger();
    if (!ledger.hit[civic::Town].earned || !ledger.hit[civic::City].earned || !ledger.hit[civic::Powered].earned ||
        !ledger.hit[civic::City].before_start) {
        return fail(83, "a city loaded without a sidecar did not mark its milestones as already reached");
    }
    if (!loaded.note_civic_month(true).empty()) {
        return fail(84, "a city loaded without a sidecar announced a milestone it already had");
    }
    std::remove(path.c_str());
    return 0;
}

// #13: the year a budget closes ends with the Funds readout's number,
// and the next year starts from it.
int check_year_funds()
{
    CitySession city;
    CitySession::NewCitySpec spec;
    spec.name = "Ledger";
    spec.seed = 5;
    spec.seed_was_set = true;
    city.new_city(spec);
    city.set_speed(3);
    city.set_simulation_paused(false);
    for (int i = 0; i < 400000 && !city.budget_pending(); ++i) {
        city.tick();
    }
    if (!city.budget_pending()) {
        return fail(90, "the first January budget never came");
    }
    const std::int64_t shown = city.funds();
    city.finish_budget_edit();
    if (!city.ledger().have_start_funds || city.ledger().start_funds != shown) {
        std::fprintf(stderr, "shown %lld start %lld\n", static_cast<long long>(shown),
                     static_cast<long long>(city.ledger().start_funds));
        return fail(91, "the new year does not start from the funds shown when the old one ended");
    }
    return 0;
}

// #14: population milestones are announced on the way up only.
int check_growth_only()
{
    auto owned = std::make_unique<Micropolis>();
    Micropolis &sim = *owned;
    sim.generateSomeCity(3);
    sim.setAutoGoto(false);
    std::vector<Sent> heard;
    listen(sim, heard);
    // About 3,000 people: a town, down from a city of 12,000.
    Nov19DisasterProbe::growth(sim, 12000, 150);
    const Quad people = Nov19DisasterProbe::people(sim);
    if (people < 2000 || people >= 10000) {
        std::fprintf(stderr, "population %lld\n", static_cast<long long>(people));
        return fail(100, "the growth check city is not a town");
    }
    if (count_of(heard, MESSAGE_REACHED_TOWN) != 0) {
        return fail(101, "a shrinking city was told it reached 2,000");
    }
    // The same town, up from a village of 1,500.
    Nov19DisasterProbe::growth(sim, 1500, 150);
    if (count_of(heard, MESSAGE_REACHED_TOWN) != 1) {
        return fail(102, "a growing city was not told it reached 2,000");
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
    if (const int r = check_power_line_name()) {
        return r;
    }
    if (const int r = check_news_staleness()) {
        return r;
    }
    if (const int r = check_boston_goal()) {
        return r;
    }
    if (const int r = check_load_without_sidecar()) {
        return r;
    }
    if (const int r = check_year_funds()) {
        return r;
    }
    if (const int r = check_growth_only()) {
        return r;
    }
    std::puts("review_nov19 ok");
    return 0;
}

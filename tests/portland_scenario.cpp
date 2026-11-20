// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The Portland scenario: its catalog entry and map, the riot that breaks
// out downtown in August 2020 (fires on downtown shops, crime up city-wide),
// and the goal: the riot over, no fire burning anywhere and average crime
// below PORTLAND_CRIME_GOAL at the deadline.

#include "city_events.hpp"
#include "city_session.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

struct Heard {
    int won = 0;
    int lost = 0;
    int riot_calls = 0;
};

void listen(Micropolis &sim, Heard &heard)
{
    sim.callbackData = &heard;
    sim.callbackHook = [](Micropolis *, void *data, const char *name, const char *, va_list) {
        Heard *out = static_cast<Heard *>(data);
        if (name == nullptr) {
            return;
        }
        const std::string what(name);
        if (what == "winGame") {
            ++out->won;
        } else if (what == "loseGame") {
            ++out->lost;
        } else if (what == "scenarioRiot") {
            ++out->riot_calls;
        }
    };
}

bool is_water(int raw)
{
    const int tile = raw & LOMASK;
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

bool is_fire(int raw)
{
    const int tile = raw & LOMASK;
    return tile >= FIREBASE && tile <= LASTFIRE;
}

void clear_fires(Micropolis &sim)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (is_fire(sim.map[x][y])) {
                sim.map[x][y] = DIRT;
            }
        }
    }
}

} // namespace

int main()
{
    // Catalog.
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_PORTLAND - 1);
    if (def.id != SC_PORTLAND || std::string(def.name) != "Portland" || def.year != 2020 ||
        def.years != PORTLAND_YEARS) {
        return fail(1, "Portland is not the eleventh scenario in the catalog");
    }
    if (std::string(def.goal).find("below " + std::to_string(PORTLAND_CRIME_GOAL) + " ") == std::string::npos ||
        std::string(def.goal).find("fire") == std::string::npos) {
        return fail(2, std::string("the goal text does not match PORTLAND_CRIME_GOAL: ") + def.goal);
    }
    if (accepted_sidecar_score_type(SC_PORTLAND, SC_PORTLAND) != SC_PORTLAND ||
        accepted_sidecar_score_type(0, SC_PORTLAND) != SC_PORTLAND) {
        return fail(3, "a Portland save's scenario clock is not accepted");
    }

    // Loading it: mid-July 2020.
    CitySession session;
    if (!session.load_scenario(SC_PORTLAND)) {
        return fail(4, "Portland did not load: " + session.message());
    }
    const auto month = [&session]() { return static_cast<int>((session.city_time() % 48) / 4); };
    if (session.city_name() != "Portland" || session.funds() != 20000 || session.city_year() != 2020 ||
        month() != 6 || session.scenario_id() != SC_PORTLAND || session.scenario_years_left() != PORTLAND_YEARS) {
        return fail(5, "Portland did not open in July 2020 with $20,000 and a year to go");
    }
    if (session.events_state().riotSteps != 0) {
        return fail(6, "the riot is on before the scenario has run");
    }

    // The map: the Columbia, the Willamette downtown and at St. Johns, and
    // the Multnomah Channel are water; downtown, Hayden Island, Ross
    // Island, Forest Park and the east side are land.
    const struct {
        int x, y;
        bool water;
        const char *what;
    } places[] = {
        {40, 4, true, "the Columbia"},       {80, 9, true, "the Columbia off PDX"},
        {56, 64, true, "the Willamette downtown"}, {30, 24, true, "the Willamette at St. Johns"},
        {58, 80, true, "the Willamette at Ross Island"}, {10, 13, true, "the Multnomah Channel"},
        {48, 60, false, "downtown"},         {54, 7, false, "Hayden Island"},
        {61, 85, false, "Ross Island"},      {15, 40, false, "Forest Park"},
        {100, 60, false, "the east side"},
    };
    for (const auto &p : places) {
        if (is_water(session.map_value(p.x, p.y)) != p.water) {
            return fail(7, std::string(p.what) + " is on the wrong side of the shore");
        }
    }
    const int forest = session.map_value(15, 40) & LOMASK;
    if (forest < WOODS_LOW || forest > WOODS_HIGH) {
        return fail(8, "Forest Park is not woods");
    }
    int downtown_shops = 0;
    for (int x = CitySession::kPortlandRiotX - city_events::kRiotDowntownRadius;
         x <= CitySession::kPortlandRiotX + city_events::kRiotDowntownRadius; ++x) {
        for (int y = CitySession::kPortlandRiotY - city_events::kRiotDowntownRadius;
             y <= CitySession::kPortlandRiotY + city_events::kRiotDowntownRadius; ++y) {
            const int raw = session.map_value(x, y);
            if ((raw & ZONEBIT) && (raw & LOMASK) >= COMBASE && (raw & LOMASK) <= COMLAST) {
                ++downtown_shops;
            }
        }
    }
    if (downtown_shops < city_events::kRiotFires) {
        return fail(9, "downtown Portland has too few shops for the riot");
    }

    // The riot breaks out downtown in August.
    session.set_speed(3);
    const long long start = session.city_time();
    long ticks = 0;
    while (session.events_state().riotSteps == 0 && session.city_time() < start + 12 && ticks < 200000) {
        session.tick();
        ++ticks;
    }
    const long long broke = session.city_time() - start;
    if (session.events_state().riotSteps == 0 || broke > PORTLAND_RIOT_WAIT + 2 || month() != 7) {
        return fail(10, "the riot did not break out in August 2020 (" + std::to_string(broke) + " weeks)");
    }
    int fires = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (!is_fire(session.map_value(x, y))) {
                continue;
            }
            const int dx = x - CitySession::kPortlandRiotX;
            const int dy = y - CitySession::kPortlandRiotY;
            const int reach = city_events::kRiotDowntownRadius + 2;
            if (dx * dx + dy * dy > reach * reach) {
                return fail(11, "a riot fire broke out outside downtown at " + std::to_string(x) + "," +
                                    std::to_string(y));
            }
            ++fires;
        }
    }
    if (fires == 0) {
        return fail(12, "the riot set no fires downtown");
    }
    if (session.scenario_meter_short().find("Riot") == std::string::npos ||
        session.scenario_meter_short().find("fire") == std::string::npos ||
        session.scenario_meter().find("riot under way") == std::string::npos ||
        session.scenario_meter().find(" burning") == std::string::npos) {
        return fail(13, "the goal meter does not show the riot and its fires: " + session.scenario_meter_short());
    }
    // The engine asks once, and only in Portland.
    {
        Micropolis sim;
        sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard heard;
        listen(sim, heard);
        if (!sim.loadScenario(SC_PORTLAND)) {
            return fail(14, "the engine did not load portland.cty");
        }
        sim.setSpeed(3);
        const long long t0 = sim.cityTime;
        while (sim.cityTime < t0 + 24) {
            sim.simTick();
        }
        if (heard.riot_calls != 1) {
            return fail(15, "the engine asked for the riot " + std::to_string(heard.riot_calls) + " times");
        }
        Micropolis seattle;
        seattle.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard quiet;
        listen(seattle, quiet);
        seattle.loadScenario(SC_SEATTLE);
        seattle.setSpeed(3);
        const long long t1 = seattle.cityTime;
        while (seattle.cityTime < t1 + 24) {
            seattle.simTick();
        }
        if (quiet.riot_calls != 0) {
            return fail(16, "another scenario asked for the Portland riot");
        }

        // start_riot_at: downtown fires, and only one riot at a time.
        std::vector<city_events::Note> notes;
        sim.cityEvents.riotSteps = 0;
        clear_fires(sim);
        if (!city_events::start_riot_at(&sim, CitySession::kPortlandRiotX, CitySession::kPortlandRiotY, notes) ||
            sim.cityEvents.riotSteps != city_events::kRiotSteps) {
            return fail(17, "start_riot_at did not start a riot downtown");
        }
        if (city_events::start_riot_at(&sim, CitySession::kPortlandRiotX, CitySession::kPortlandRiotY, notes)) {
            return fail(18, "a second riot started while one was under way");
        }

        // The goal: the riot over, no fire anywhere, crime below the goal.
        sim.crimeAverage = 0;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 1 || heard.won != 0) {
            return fail(19, "Portland was won with the riot still going on");
        }
        sim.cityEvents.riotSteps = 0;
        clear_fires(sim);
        sim.map[100][60] = FIRE | ANIMBIT;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 2 || heard.won != 0) {
            return fail(20, "Portland was won with a fire still burning");
        }
        sim.map[100][60] = DIRT;
        sim.crimeAverage = PORTLAND_CRIME_GOAL;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 3 || heard.won != 0) {
            return fail(21, "Portland was won with crime at the goal, not below it");
        }
        sim.crimeAverage = PORTLAND_CRIME_GOAL - 1;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.won != 1) {
            return fail(22, "Portland was lost with the riot over, the fires out and crime below the goal");
        }
    }

    // A city with no fires reads as fires out on the meter.
    CitySession calm;
    if (!calm.load_scenario(SC_PORTLAND) || calm.scenario_meter_short().find("Fires out") == std::string::npos ||
        calm.scenario_meter().find("all fires out") == std::string::npos) {
        return fail(23, "the meter does not say the fires are out");
    }
    std::printf("portland ok: riot after %lld weeks, %d fires downtown\n", broke, fires);
    return 0;
}

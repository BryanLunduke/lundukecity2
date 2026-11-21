// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The Seattle scenario: its catalog entry and map, the Autonomous Zone that
// forms on Capitol Hill a few weeks in and grows while Capitol Hill has no
// police, and the goal: no Autonomous Zone and average crime below
// SEATTLE_CRIME_GOAL at the deadline.

#include "city_session.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

struct Heard {
    int won = 0;
    int lost = 0;
    int zone_calls = 0;
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
        } else if (what == "scenarioAutonomousZone") {
            ++out->zone_calls;
        }
    };
}

bool is_water(int raw)
{
    const int tile = raw & LOMASK;
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

} // namespace

int main()
{
    // Catalog.
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_SEATTLE - 1);
    if (def.id != SC_SEATTLE || std::string(def.name) != "Seattle" || def.year != 2020 || def.years != SEATTLE_YEARS) {
        return fail(1, "Seattle is not the ninth scenario in the catalog");
    }
    if (std::string(def.goal).find("below " + std::to_string(SEATTLE_CRIME_GOAL) + " ") == std::string::npos ||
        std::string(def.goal).find("Autonomous Zone") == std::string::npos) {
        return fail(2, std::string("the goal text does not match SEATTLE_CRIME_GOAL: ") + def.goal);
    }
    if (accepted_sidecar_score_type(SC_SEATTLE, SC_SEATTLE) != SC_SEATTLE ||
        accepted_sidecar_score_type(0, SC_SEATTLE) != SC_SEATTLE || accepted_sidecar_score_type(SC_COUNT, SC_COUNT) != 0) {
        return fail(3, "a Seattle save's scenario clock is not accepted");
    }

    // Loading it.
    CitySession session;
    if (!session.load_scenario(SC_SEATTLE)) {
        return fail(4, "Seattle did not load: " + session.message());
    }
    if (session.city_name() != "Seattle" || session.funds() != 20000 || session.city_year() != 2020 ||
        session.scenario_id() != SC_SEATTLE || session.scenario_years_left() != SEATTLE_YEARS) {
        return fail(5, "Seattle did not open in 2020 with $20,000 and five years to go");
    }
    if (session.events_state().zoneCount != 0) {
        return fail(6, "the Autonomous Zone stands before the scenario has run");
    }

    // The map: Elliott Bay, Lake Union, Lake Washington, Green Lake and
    // the Ship Canal are water; downtown and Capitol Hill are land; Cal
    // Anderson Park is a park; no police station on Capitol Hill.
    const struct {
        int x, y;
        bool water;
        const char *what;
    } places[] = {
        {35, 70, true, "Elliott Bay"}, {64, 45, true, "Lake Union"}, {110, 50, true, "Lake Washington"},
        {62, 7, true, "Green Lake"}, {40, 26, true, "the Ship Canal"}, {5, 10, true, "Puget Sound"},
        {62, 68, false, "downtown"}, {80, 62, false, "Capitol Hill"}, {30, 10, false, "Ballard"},
        {52, 90, false, "Harbor Island"},
    };
    for (const auto &p : places) {
        if (is_water(session.map_value(p.x, p.y)) != p.water) {
            return fail(7, std::string(p.what) + " is on the wrong side of the shore");
        }
    }
    const int park = session.map_value(CitySession::kSeattleZoneX, CitySession::kSeattleZoneY) & LOMASK;
    if (park < WOODS_LOW || park >= RUBBLE) {
        return fail(8, "Cal Anderson Park is not a park (tile " + std::to_string(park) + ")");
    }
    int police = 0;
    int capitol_police = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if ((session.map_value(x, y) & LOMASK) == POLICESTATION) {
                ++police;
                if (std::abs(x - CitySession::kSeattleZoneX) <= 9 && std::abs(y - CitySession::kSeattleZoneY) <= 9) {
                    ++capitol_police;
                }
            }
        }
    }
    if (police < 6 || capitol_police != 0) {
        return fail(9, "Seattle should have its precincts, but none on Capitol Hill");
    }

    // The zone forms on Capitol Hill a few weeks in.
    session.set_speed(3);
    const long long start = session.city_time();
    long ticks = 0;
    while (session.events_state().zoneCount == 0 && session.city_time() < start + 12 && ticks < 200000) {
        session.tick();
        ++ticks;
    }
    const long long formed = session.city_time() - start;
    if (session.events_state().zoneCount != 2 || formed > SEATTLE_ZONE_WAIT + 2) {
        return fail(10, "the Autonomous Zone did not form in June (" + std::to_string(formed) + " weeks, " +
                            std::to_string(session.events_state().zoneCount) + " blocks)");
    }
    if (!session.in_autonomous_zone(CitySession::kSeattleZoneX, CitySession::kSeattleZoneY)) {
        return fail(11, "the Autonomous Zone did not take Cal Anderson Park");
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (session.events_state().zoneTile[x][y] != 0 && (x < 70 || x > 90 || y < 50 || y > 70)) {
                return fail(12, "the Autonomous Zone formed off Capitol Hill at " + std::to_string(x) + "," +
                                    std::to_string(y));
            }
        }
    }
    if (session.scenario_meter_short().find("Zone 2 blocks") == std::string::npos ||
        session.scenario_meter().find("Autonomous Zone holds 2 blocks") == std::string::npos) {
        return fail(13, "the goal meter does not show the zone: " + session.scenario_meter_short());
    }

    // Left alone it grows (Capitol Hill has no police).
    while (session.city_time() < start + 48 && ticks < 2000000) {
        session.tick();
        ++ticks;
    }
    if (session.events_state().zoneCount <= 3) {
        return fail(14, "the Autonomous Zone did not grow on an unpoliced Capitol Hill");
    }

    // The engine asks once, and only in Seattle.
    {
        Micropolis sim;
        sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard heard;
        listen(sim, heard);
        if (!sim.loadScenario(SC_SEATTLE)) {
            return fail(15, "the engine did not load seattle.cty");
        }
        sim.setSpeed(3);
        const long long t0 = sim.cityTime;
        while (sim.cityTime < t0 + 24) {
            sim.simTick();
        }
        if (heard.zone_calls != 1) {
            return fail(16, "the engine asked for the zone " + std::to_string(heard.zone_calls) + " times");
        }
        Micropolis tokyo;
        tokyo.resourceDir = LUNDUKE_CITY_ASSET_DIR "/res";
        Heard quiet;
        listen(tokyo, quiet);
        tokyo.loadScenario(SC_TOKYO);
        tokyo.setSpeed(3);
        const long long t1 = tokyo.cityTime;
        while (tokyo.cityTime < t1 + 24) {
            tokyo.simTick();
        }
        if (quiet.zone_calls != 0) {
            return fail(17, "another scenario asked for the Seattle zone");
        }

        // The goal: no zone and crime below the goal.
        sim.cityEvents.zoneCount = 3;
        sim.crimeAverage = 0;
        sim.doScenarioScore(SC_SEATTLE);
        if (heard.lost != 1 || heard.won != 0) {
            return fail(18, "Seattle was won with the Autonomous Zone still standing");
        }
        sim.cityEvents.zoneCount = 0;
        sim.crimeAverage = SEATTLE_CRIME_GOAL;
        sim.doScenarioScore(SC_SEATTLE);
        if (heard.lost != 2 || heard.won != 0) {
            return fail(19, "Seattle was won with crime at the goal, not below it");
        }
        sim.crimeAverage = SEATTLE_CRIME_GOAL - 1;
        sim.doScenarioScore(SC_SEATTLE);
        if (heard.won != 1) {
            return fail(20, "Seattle was lost with the zone cleared and crime below the goal");
        }
    }

    // Before the zone forms the meter must not claim it is cleared.
    CitySession fresh;
    if (!fresh.load_scenario(SC_SEATTLE) || fresh.scenario_meter_short().find("Zone: not yet formed") == std::string::npos ||
        fresh.scenario_meter().find("has not formed yet") == std::string::npos ||
        fresh.scenario_meter_short().find("cleared") != std::string::npos ||
        fresh.scenario_meter().find("no Autonomous Zone") != std::string::npos) {
        return fail(21, "the meter does not say the zone has not formed yet: " + fresh.scenario_meter());
    }
    std::printf("seattle ok: zone formed after %lld weeks\n", formed);
    return 0;
}

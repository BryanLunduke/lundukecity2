// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The San Diego scenario: its catalog entry and map, the lion that gets out
// of the zoo in Balboa Park in April 2025, and the goal: the police caught
// the lion within SANDIEGO_LION_CATCH_STEPS and the city has at least
// SANDIEGO_POPULATION_GOAL people at the deadline. A San Diego save keeps
// the lion's record once it got out.

#include "city_events.hpp"
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
    int lion_calls = 0;
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
        } else if (what == "scenarioLionEscape") {
            ++out->lion_calls;
        }
    };
}

bool is_water(int raw)
{
    const int tile = raw & LOMASK;
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

// A census of exactly `people` (residents only).
void census(Micropolis &sim, long people)
{
    sim.censusSnapshotValid = true;
    sim.snapResPop = people / 20;
    sim.snapComPop = 0;
    sim.snapIndPop = 0;
    sim.snapDataCenterPop = 0;
}

// SANDIEGO_POPULATION_GOAL as the catalog and the meter write it.
const char *const kGoalText = "120,000";

} // namespace

int main()
{
    // Catalog.
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_SAN_DIEGO - 1);
    if (def.id != SC_SAN_DIEGO || std::string(def.name) != "San Diego" || def.year != 2025 ||
        def.years != SANDIEGO_YEARS) {
        return fail(1, "San Diego is not the twelfth scenario in the catalog");
    }
    if (std::string(def.goal).find(kGoalText) == std::string::npos ||
        std::string(def.goal).find("lion") == std::string::npos ||
        std::string(def.goal).find("four months") == std::string::npos) {
        return fail(2, std::string("the goal text does not match the San Diego goal: ") + def.goal);
    }
    if (SANDIEGO_POPULATION_GOAL != 120000) {
        return fail(2, "update kGoalText with SANDIEGO_POPULATION_GOAL");
    }
    if (SANDIEGO_LION_CATCH_STEPS != 16 || SANDIEGO_LION_CATCH_STEPS >= city_events::kLionMaxSteps) {
        return fail(2, "four months is not inside the keepers' six");
    }
    if (accepted_sidecar_score_type(SC_SAN_DIEGO, SC_SAN_DIEGO) != SC_SAN_DIEGO ||
        accepted_sidecar_score_type(0, SC_SAN_DIEGO) != SC_SAN_DIEGO) {
        return fail(3, "a San Diego save's scenario clock is not accepted");
    }

    // Loading it: March 2025.
    CitySession session;
    if (!session.load_scenario(SC_SAN_DIEGO)) {
        return fail(4, "San Diego did not load: " + session.message());
    }
    const auto month = [&session]() { return static_cast<int>((session.city_time() % 48) / 4); };
    if (session.city_name() != "San Diego" || session.funds() != 20000 || session.city_year() != 2025 ||
        month() != 2 || session.scenario_id() != SC_SAN_DIEGO || session.scenario_years_left() != SANDIEGO_YEARS) {
        return fail(5, "San Diego did not open in March 2025 with $20,000 and two years to go");
    }
    if (session.events_state().lionActive != 0 || session.events_state().lionSteps != 0) {
        return fail(6, "the lion is out before the scenario has run");
    }
    if (session.scenario_meter_short().find("Lion in zoo") == std::string::npos ||
        session.scenario_meter().find(kGoalText) == std::string::npos) {
        return fail(6, "the goal meter does not show the zoo and the population goal: " + session.scenario_meter());
    }

    // The map: the Pacific, Mission Bay and San Diego Bay are water;
    // Coronado, Point Loma, Fiesta Island, Balboa Park and Kearny Mesa are
    // land.
    const struct {
        int x, y;
        bool water;
        const char *what;
    } places[] = {
        {1, 60, true, "the Pacific"},          {10, 28, true, "Mission Bay"},
        {45, 66, true, "San Diego Bay"},       {70, 88, true, "the bay off National City"},
        {30, 70, true, "the harbour mouth"},   {44, 80, false, "Coronado"},
        {10, 70, false, "Point Loma"},         {26, 24, false, "Fiesta Island"},
        {64, 55, false, "Balboa Park"},        {90, 10, false, "Kearny Mesa"},
    };
    for (const auto &p : places) {
        if (is_water(session.map_value(p.x, p.y)) != p.water) {
            return fail(7, std::string(p.what) + " is on the wrong side of the shore");
        }
    }
    // The zoo stands in Balboa Park, and no police station is near it.
    int zoo_x = -1;
    int zoo_y = -1;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if ((session.map_value(x, y) & LOMASK) == SPECIAL_ZOO) {
                zoo_x = x;
                zoo_y = y;
            }
        }
    }
    if (zoo_x < CitySession::kSanDiegoZooLeft || zoo_x > CitySession::kSanDiegoZooLeft + 2 ||
        zoo_y < CitySession::kSanDiegoZooTop || zoo_y > CitySession::kSanDiegoZooTop + 2) {
        return fail(8, "there is no zoo in Balboa Park");
    }
    for (int x = zoo_x - 9; x <= zoo_x + 9; ++x) {
        for (int y = zoo_y - 9; y <= zoo_y + 9; ++y) {
            if ((session.map_value(x, y) & LOMASK) == POLICESTATION) {
                return fail(9, "a police station already stands by the zoo");
            }
        }
    }

    // The lion gets out in April.
    session.set_speed(3);
    const long long start = session.city_time();
    long ticks = 0;
    while (session.events_state().lionActive == 0 && session.city_time() < start + 12 && ticks < 200000) {
        session.tick();
        ++ticks;
    }
    const long long out_after = session.city_time() - start;
    if (session.events_state().lionActive == 0 || out_after > SANDIEGO_LION_WAIT + 2 || month() != 3) {
        return fail(10, "the lion did not get out in April 2025 (" + std::to_string(out_after) + " weeks)");
    }
    const int dx = session.events_state().lionX / 16 - zoo_x;
    const int dy = session.events_state().lionY / 16 - zoo_y;
    if (dx * dx + dy * dy > 6 * 6) {
        return fail(11, "the lion did not get out at the zoo");
    }
    if (session.scenario_meter_short().find("Lion loose") == std::string::npos ||
        session.scenario_meter().find("police must catch it within four months") == std::string::npos) {
        return fail(12, "the goal meter does not show the lion loose: " + session.scenario_meter());
    }

    {
        // The engine asks once, and only in San Diego.
        Micropolis sim;
        sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard heard;
        listen(sim, heard);
        if (!sim.loadScenario(SC_SAN_DIEGO)) {
            return fail(13, "the engine did not load sandiego.cty");
        }
        sim.setSpeed(3);
        const long long t0 = sim.cityTime;
        while (sim.cityTime < t0 + 24) {
            sim.simTick();
        }
        if (heard.lion_calls != 1) {
            return fail(14, "the engine asked for the lion " + std::to_string(heard.lion_calls) + " times");
        }
        Micropolis portland;
        portland.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard quiet;
        listen(portland, quiet);
        portland.loadScenario(SC_PORTLAND);
        portland.setSpeed(3);
        const long long t1 = portland.cityTime;
        while (portland.cityTime < t1 + 24) {
            portland.simTick();
        }
        if (quiet.lion_calls != 0) {
            return fail(15, "another scenario asked for the San Diego lion");
        }

        // The goal. The lion never got out: lost.
        CityEvents &e = sim.cityEvents;
        e.lionActive = 0;
        e.lionSteps = 0;
        census(sim, SANDIEGO_POPULATION_GOAL);
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.lost != 1 || heard.won != 0) {
            return fail(16, "San Diego was won without the lion caught");
        }
        // Still loose: lost.
        e.lionActive = 1;
        e.lionSteps = 10;
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.lost != 2 || heard.won != 0) {
            return fail(17, "San Diego was won with the lion loose");
        }
        // Caught by the police, but after four months: lost.
        e.lionActive = 0;
        e.lionSteps = SANDIEGO_LION_CATCH_STEPS + 1;
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.lost != 3 || heard.won != 0) {
            return fail(18, "San Diego was won with the lion caught too late");
        }
        // Fetched by its keepers: lost.
        e.lionSteps = city_events::kLionMaxSteps;
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.lost != 4 || heard.won != 0) {
            return fail(19, "San Diego was won with the keepers catching the lion");
        }
        // Caught in time, but too few people: lost.
        e.lionSteps = SANDIEGO_LION_CATCH_STEPS;
        census(sim, SANDIEGO_POPULATION_GOAL - 20);
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.lost != 5 || heard.won != 0) {
            return fail(20, "San Diego was won below the population goal");
        }
        // Caught in time and the people kept: won.
        census(sim, SANDIEGO_POPULATION_GOAL);
        sim.doScenarioScore(SC_SAN_DIEGO);
        if (heard.won != 1) {
            return fail(21, "San Diego was lost with the lion caught in time and the people kept");
        }

        // A San Diego save keeps the caught lion's record; elsewhere a
        // finished lion leaves no trailer, as before.
        const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                                 "/sandiego_scenario_test.cty";
        e.lionActive = 0;
        e.lionSteps = 7;
        if (!sim.saveFile(path.c_str())) {
            return fail(22, "the San Diego city did not save");
        }
        Micropolis back;
        back.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        if (!back.loadFile(path.c_str()) || back.cityEvents.lionSteps != 7 || back.cityEvents.lionActive != 0) {
            return fail(23, "a San Diego save lost the lion's record");
        }
        portland.cityEvents.lionActive = 0;
        portland.cityEvents.lionSteps = 7;
        if (!portland.saveFile(path.c_str())) {
            return fail(24, "the Portland city did not save");
        }
        Micropolis other;
        other.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        if (!other.loadFile(path.c_str()) || other.cityEvents.lionSteps != 0) {
            return fail(25, "another city kept a finished lion's record");
        }
        std::remove(path.c_str());
    }

    std::printf("san diego ok: lion out after %lld weeks\n", out_after);
    return 0;
}

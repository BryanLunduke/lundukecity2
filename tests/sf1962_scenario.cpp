// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The San Francisco 1962 scenario: its catalog entry and map, the Alcatraz
// breakout in June 1962 (the escapees come ashore at Fisherman's Wharf, so
// the fires and the crime are in the city, not on the island), and the
// goal: the escape over and average crime below SF1962_CRIME_GOAL at the
// deadline.

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
    int escape_calls = 0;
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
        } else if (what == "scenarioPrisonEscape") {
            ++out->escape_calls;
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
    return tile >= FIREBASE && tile < ROADBASE;
}

} // namespace

int main()
{
    // Catalog.
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_SAN_FRANCISCO_1962 - 1);
    if (def.id != SC_SAN_FRANCISCO_1962 || std::string(def.name) != "San Francisco" || def.year != 1962 ||
        def.years != SF1962_YEARS) {
        return fail(1, "San Francisco 1962 is not the tenth scenario in the catalog");
    }
    if (std::string(def.goal).find("below " + std::to_string(SF1962_CRIME_GOAL) + " ") == std::string::npos ||
        std::string(def.goal).find("Alcatraz") == std::string::npos) {
        return fail(2, std::string("the goal text does not match SF1962_CRIME_GOAL: ") + def.goal);
    }
    if (accepted_sidecar_score_type(SC_SAN_FRANCISCO_1962, SC_SAN_FRANCISCO_1962) != SC_SAN_FRANCISCO_1962 ||
        accepted_sidecar_score_type(0, SC_SAN_FRANCISCO_1962) != SC_SAN_FRANCISCO_1962) {
        return fail(3, "a San Francisco save's scenario clock is not accepted");
    }

    // Loading it: May 1962.
    CitySession session;
    if (!session.load_scenario(SC_SAN_FRANCISCO_1962)) {
        return fail(4, "San Francisco did not load: " + session.message());
    }
    if (session.city_name() != "San Francisco" || session.funds() != 20000 || session.city_year() != 1962 ||
        static_cast<int>((session.city_time() % 48) / 4) != 4 || session.scenario_id() != SC_SAN_FRANCISCO_1962 ||
        session.scenario_years_left() != SF1962_YEARS) {
        return fail(5, "San Francisco did not open in May 1962 with $20,000 and two years to go");
    }
    if (session.events_state().prisonSteps != 0) {
        return fail(6, "the escape is on before the scenario has run");
    }
    if (session.scenario_meter_short().find("Escape: not yet started") == std::string::npos ||
        session.scenario_meter().find("has not started yet") == std::string::npos ||
        session.scenario_meter_short().find("No escapees loose") != std::string::npos) {
        return fail(6, "the meter does not say the escape has not started yet: " + session.scenario_meter());
    }

    // The map: the Golden Gate, the Bay, the Pacific and Lake Merced are
    // water; downtown, the Presidio, Treasure Island and Alcatraz are land.
    const struct {
        int x, y;
        bool water;
        const char *what;
    } places[] = {
        {20, 12, true, "the Golden Gate"}, {90, 15, true, "the Bay"},       {1, 60, true, "the Pacific"},
        {16, 90, true, "Lake Merced"},     {66, 18, true, "Alcatraz's channel"}, {84, 38, false, "downtown"},
        {35, 26, false, "the Presidio"},   {110, 15, false, "Treasure Island"},  {70, 12, false, "Alcatraz"},
        {72, 22, false, "Fisherman's Wharf"}, {20, 50, false, "Golden Gate Park"},
    };
    for (const auto &p : places) {
        if (is_water(session.map_value(p.x, p.y)) != p.water) {
            return fail(7, std::string(p.what) + " is on the wrong side of the shore");
        }
    }
    // The prison stands on Alcatraz, cut off by water from the Wharf.
    int prison_x = -1;
    int prison_y = -1;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if ((session.map_value(x, y) & LOMASK) == SPECIAL_PRISON) {
                prison_x = x;
                prison_y = y;
            }
        }
    }
    if (prison_x < 64 || prison_x > 74 || prison_y < 8 || prison_y > 15) {
        return fail(8, "there is no prison on Alcatraz");
    }
    for (int y = prison_y + 4; y < CitySession::kSf1962LandingY - 2; ++y) {
        if (!is_water(session.map_value(prison_x - 2, y))) {
            return fail(9, "Alcatraz is not an island");
        }
    }
    // 1962: power from fossil plants only, and no BART.
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if ((session.map_value(x, y) & LOMASK) == NUCLEAR) {
                return fail(10, "a nuclear plant in 1962 San Francisco");
            }
        }
    }

    // The breakout in June: fires and crime at the Wharf landing.
    session.set_speed(3);
    const long long start = session.city_time();
    long ticks = 0;
    while (session.events_state().prisonSteps == 0 && session.city_time() < start + 12 && ticks < 200000) {
        session.tick();
        ++ticks;
    }
    const long long broke = session.city_time() - start;
    const CityEvents &e = session.events_state();
    if (e.prisonSteps == 0 || broke > SF1962_ESCAPE_WAIT + 2 || static_cast<int>((session.city_time() % 48) / 4) != 5) {
        return fail(11, "the escape did not happen in June 1962 (" + std::to_string(broke) + " weeks)");
    }
    if (e.prisonX != CitySession::kSf1962LandingX || e.prisonY != CitySession::kSf1962LandingY) {
        return fail(12, "the escape is not centred on the Wharf landing");
    }
    int fires = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (!is_fire(session.map_value(x, y))) {
                continue;
            }
            const int dx = x - CitySession::kSf1962LandingX;
            const int dy = y - CitySession::kSf1962LandingY;
            if (dx * dx + dy * dy > city_events::kPrisonFireRadius * city_events::kPrisonFireRadius + 8) {
                return fail(13, "a fire broke out far from the landing at " + std::to_string(x) + "," +
                                    std::to_string(y));
            }
            if (std::abs(x - prison_x) <= 3 && std::abs(y - prison_y) <= 3) {
                return fail(14, "the fire is on Alcatraz, not in the city");
            }
            ++fires;
        }
    }
    if (fires == 0) {
        return fail(15, "the escape set no fires at the Wharf");
    }
    if (session.scenario_meter_short().find("Escapees loose") == std::string::npos ||
        session.scenario_meter().find("Alcatraz escapees at large") == std::string::npos) {
        return fail(16, "the goal meter does not show the escape: " + session.scenario_meter_short());
    }

    // The escape is over on its own within kPrisonMaxSteps.
    while (session.events_state().prisonSteps != 0 && session.city_time() < start + 60 && ticks < 2000000) {
        session.tick();
        ++ticks;
    }
    if (session.events_state().prisonSteps != 0 ||
        session.scenario_meter_short().find("No escapees loose") == std::string::npos) {
        return fail(17, "the escape never ended");
    }

    // The engine asks once, and only in San Francisco.
    {
        Micropolis sim;
        sim.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        Heard heard;
        listen(sim, heard);
        if (!sim.loadScenario(SC_SAN_FRANCISCO_1962)) {
            return fail(18, "the engine did not load sanfrancisco1962.cty");
        }
        sim.setSpeed(3);
        const long long t0 = sim.cityTime;
        while (sim.cityTime < t0 + 24) {
            sim.simTick();
        }
        if (heard.escape_calls != 1) {
            return fail(19, "the engine asked for the escape " + std::to_string(heard.escape_calls) + " times");
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
        if (quiet.escape_calls != 0) {
            return fail(20, "another scenario asked for the Alcatraz escape");
        }

        // The goal: the escape over and crime below the goal.
        sim.cityEvents.prisonSteps = 10;
        sim.crimeAverage = 0;
        sim.doScenarioScore(SC_SAN_FRANCISCO_1962);
        if (heard.lost != 1 || heard.won != 0) {
            return fail(21, "San Francisco was won with escapees still loose");
        }
        sim.cityEvents.prisonSteps = 0;
        sim.crimeAverage = SF1962_CRIME_GOAL;
        sim.doScenarioScore(SC_SAN_FRANCISCO_1962);
        if (heard.lost != 2 || heard.won != 0) {
            return fail(22, "San Francisco was won with crime at the goal, not below it");
        }
        sim.crimeAverage = SF1962_CRIME_GOAL - 1;
        sim.doScenarioScore(SC_SAN_FRANCISCO_1962);
        if (heard.won != 1) {
            return fail(23, "San Francisco was lost with the escape over and crime below the goal");
        }
    }
    std::printf("san francisco 1962 ok: escape after %lld weeks, %d fires at the Wharf\n", broke, fires);
    return 0;
}

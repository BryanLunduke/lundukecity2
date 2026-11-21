// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The Portland scenario: its catalog entry and map, the riot that breaks
// out downtown in August 2020 (fires on downtown shops, crime up city-wide),
// and the goal: the riot over, no riot fire still burning and average crime
// below PORTLAND_CRIME_GOAL at the deadline. Ordinary random fires do not count.

#include "city_events.hpp"
#include "city_session.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Micropolis names this struct a friend, so the test can run doFire.
struct SimulatorProbe {
    static void fire(Micropolis &sim, int x, int y) { sim.doFire(Position(x, y)); }
};

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
        std::string(def.goal).find("riot fire") == std::string::npos) {
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
        session.scenario_meter_short().find("riot fire") == std::string::npos ||
        session.scenario_meter().find("riot under way") == std::string::npos ||
        session.scenario_meter().find("riot fire") == std::string::npos) {
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

        // The goal: the riot over, no riot fire still burning, crime below the goal.
        sim.crimeAverage = 0;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 1 || heard.won != 0) {
            return fail(19, "Portland was won with the riot still going on");
        }
        sim.cityEvents.riotSteps = 0;
        clear_fires(sim);
        std::memset(sim.cityEvents.riotFire, 0, sizeof(sim.cityEvents.riotFire));
        // A riot fire still burning blocks the win.
        sim.map[50][60] = FIRE | ANIMBIT;
        sim.markRiotFire(50, 60);
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 2 || heard.won != 0) {
            return fail(20, "Portland was won with a riot fire still burning");
        }
        // An ordinary random fire does not.
        sim.map[50][60] = DIRT;
        sim.cityEvents.riotFire[50][60] = 0;
        sim.map[100][60] = FIRE | ANIMBIT;
        sim.crimeAverage = PORTLAND_CRIME_GOAL - 1;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 2 || heard.won != 1) {
            return fail(21, "Portland was lost because of a random fire that was not from the riot");
        }
        // Crime at the goal, not below it, still loses (clear the random fire).
        sim.map[100][60] = DIRT;
        sim.crimeAverage = PORTLAND_CRIME_GOAL;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.lost != 3 || heard.won != 1) {
            return fail(22, "Portland was won with crime at the goal, not below it");
        }
        sim.crimeAverage = PORTLAND_CRIME_GOAL - 1;
        sim.doScenarioScore(SC_PORTLAND);
        if (heard.won != 2) {
            return fail(23, "Portland was lost with the riot over, the riot fires out and crime below the goal");
        }

        // A save keeps the riot-fire record after the riot itself has ended.
        // Loading a city scans the map, and a fire tile may burn out in that
        // pass, so mark a cluster of riot fires and require that at least
        // one is still burning with its flag set.
        const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                                 "/portland_riot_fire.cty";
        for (int i = 0; i < 12; ++i) {
            const int fx = 40 + i * 2;
            const int fy = 60;
            sim.map[fx][fy] = FIRE | ANIMBIT;
            sim.markRiotFire(fx, fy);
        }
        if (!sim.saveFile(path.c_str())) {
            return fail(24, "the Portland city did not save");
        }
        Micropolis back;
        back.resourceDir = LUNDUKE_CITY_ASSET_DIR "/../../data/scenarios";
        if (!back.loadFile(path.c_str()) || back.cityEvents.riotSteps != 0 ||
            back.riotFiresBurning() == 0) {
            return fail(25, "a Portland save lost the riot fires still burning");
        }
        Heard after;
        listen(back, after);
        back.crimeAverage = 0;
        back.cityEvents.riotSteps = 0;
        back.doScenarioScore(SC_PORTLAND);
        if (after.lost != 1 || after.won != 0) {
            return fail(26, "a reloaded riot fire did not block the win");
        }
        std::remove(path.c_str());
    }

    // In Portland a riot fire in a fire station's good cover burns out on
    // its next pass (PORTLAND_RIOT_FIRE_RATE_DIVISOR). An ordinary fire
    // there, or a riot fire in any other city, keeps the classic odds (out
    // one pass in three), so some of 200 of those are still burning.
    {
        Micropolis f;
        for (int x = 0; x < WORLD_W; ++x) {
            for (int y = 0; y < WORLD_H; ++y) {
                f.map[x][y] = DIRT;
            }
        }
        f.fireStationEffectMap.worldSet(50, 60, 50);
        auto burning = [&f]() {
            const int tile = f.map[50][60] & LOMASK;
            return tile >= FIREBASE && tile <= LASTFIRE;
        };
        auto survivors = [&](Scenario type, bool riot) {
            f.scoreType = type;
            int left = 0;
            for (int i = 0; i < 200; ++i) {
                f.map[50][60] = FIRE | ANIMBIT;
                f.cityEvents.riotFire[50][60] = 0;
                if (riot) {
                    f.markRiotFire(50, 60);
                }
                SimulatorProbe::fire(f, 50, 60);
                left += burning() ? 1 : 0;
            }
            return left;
        };
        if (survivors(SC_PORTLAND, true) != 0) {
            return fail(28, "a Portland riot fire in good fire cover did not burn out on its next pass");
        }
        if (survivors(SC_PORTLAND, false) == 0) {
            return fail(29, "an ordinary fire in Portland burns out as fast as a riot fire");
        }
        if (survivors(SC_NONE, true) == 0) {
            return fail(30, "a riot fire outside the Portland scenario burns out faster than the classic odds");
        }
    }

    // A city with no riot fires reads as riot fires out on the meter.
    CitySession calm;
    if (!calm.load_scenario(SC_PORTLAND) ||
        calm.scenario_meter_short().find("Riot fires out") == std::string::npos ||
        calm.scenario_meter().find("all riot fires out") == std::string::npos) {
        return fail(27, "the meter does not say the riot fires are out");
    }
    std::printf("portland ok: riot after %lld weeks, %d fires downtown\n", broke, fires);
    return 0;
}

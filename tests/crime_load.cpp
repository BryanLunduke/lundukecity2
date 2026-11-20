// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// After loading a saved city, crime average and the crime map must match
// the values that were saved (land value used to start empty, so crime
// read too high for about a scan period). Seattle's starting crime, on a
// fresh scenario load, must stay as it was.

#include "city_session.hpp"

#include "micropolis.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

// Run the city long enough for land value to settle, and put down a few
// extra police stations so crime is not still at the opening spike.
void play_on(CitySession &session, int months, int stations)
{
    session.set_speed(3);
    const long long start = session.city_time();
    bool acted = false;
    long ticks = 0;
    // The engine pointer is not public; use tools after the first tick so
    // the session is ready, then find stations through the public API.
    while (session.city_time() < start + months * 4 && ticks < 40000000) {
        session.tick();
        ++ticks;
        if (acted || session.city_time() - start < 4) {
            continue;
        }
        acted = true;
        int placed = 0;
        for (int x = 2; x < WORLD_W - 2 && placed < stations; ++x) {
            for (int y = 2; y < WORLD_H - 2 && placed < stations; ++y) {
                const int raw = session.map_value(x, y);
                const int tile = raw & LOMASK;
                if ((raw & ZONEBIT) == 0 || tile < RESBASE || tile >= PORTBASE) {
                    continue;
                }
                bool nearby = false;
                for (int dx = -9; dx <= 9 && !nearby; ++dx) {
                    for (int dy = -9; dy <= 9; ++dy) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < WORLD_W && ny < WORLD_H &&
                            (session.map_value(nx, ny) & LOMASK) == POLICESTATION) {
                            nearby = true;
                        }
                    }
                }
                if (nearby) {
                    continue;
                }
                session.use_tool(TOOL_BULLDOZER, x, y);
                session.use_tool(TOOL_BULLDOZER, x, y);
                const long long before = session.funds();
                session.use_tool(TOOL_POLICESTATION, x, y);
                if (session.funds() < before) {
                    ++placed;
                }
            }
        }
    }
}

int roundtrip(int scenario, const char *name)
{
    CitySession session;
    if (!session.load_scenario(scenario)) {
        return fail(1, std::string(name) + " did not load: " + session.message());
    }
    play_on(session, 24, 8);
    const int saved_crime = session.crime_average();
    if (saved_crime <= 0) {
        return fail(2, std::string(name) + " never produced a crime reading");
    }
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                             "/crime_load_" + name + ".cty";
    if (!session.save_city_as(path)) {
        return fail(3, std::string(name) + " did not save");
    }
    CitySession loaded;
    if (!loaded.load_city(path)) {
        return fail(4, std::string(name) + " did not load back: " + loaded.message());
    }
    const int loaded_crime = loaded.crime_average();
    std::remove(path.c_str());
    const int delta = loaded_crime > saved_crime ? loaded_crime - saved_crime : saved_crime - loaded_crime;
    if (delta > 2) {
        std::fprintf(stderr, "%s crime before save %d after load %d\n", name, saved_crime, loaded_crime);
        return fail(5, std::string(name) + " crime after load does not match the saved reading");
    }
    return 0;
}

} // namespace

int main()
{
    // A fresh Seattle scenario must still open at its original (high) crime,
    // not at the settled mid-game figure. That is the scenario's starting
    // condition; only a saved city is restored.
    CitySession seattle;
    if (!seattle.load_scenario(SC_SEATTLE)) {
        return fail(6, "Seattle did not load: " + seattle.message());
    }
    if (seattle.crime_average() < 100) {
        std::fprintf(stderr, "Seattle start crime %d\n", seattle.crime_average());
        return fail(7, "Seattle's opening crime is no longer the unsettled start");
    }

    const int seattle_rt = roundtrip(SC_SEATTLE, "seattle");
    if (seattle_rt != 0) {
        return seattle_rt;
    }
    const int portland_rt = roundtrip(SC_PORTLAND, "portland");
    if (portland_rt != 0) {
        return portland_rt + 10;
    }
    const int sf_rt = roundtrip(SC_SAN_FRANCISCO_1962, "sf1962");
    if (sf_rt != 0) {
        return sf_rt + 20;
    }
    std::printf("crime load ok\n");
    return 0;
}

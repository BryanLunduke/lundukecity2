// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// A mid-game save comes back with the same tiles, score, clock, and the
// sidecar's RNG / valves / phase, so a resumed city starts from the same
// simulator state the uninterrupted game had.

#include "city_session.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

void run_ticks(CitySession &session, long count)
{
    for (long i = 0; i < count; ++i) {
        session.tick();
        if (session.budget_pending()) {
            session.commit_pending_budget();
        }
        (void)session.take_budget_request();
        (void)session.take_scenario_outcome();
    }
}

void run_until(CitySession &session, long long time)
{
    for (long guard = 0; session.city_time() < time && guard < 20000000; ++guard) {
        run_ticks(session, 1);
    }
}

} // namespace

int main()
{
    CitySession session;
    if (!session.load_scenario(SC_BOSTON)) {
        return fail(1, "Boston did not load: " + session.message());
    }
    session.set_speed(3);
    session.set_simulation_paused(false);
    const long long start = session.city_time();
    run_until(session, start + 12 * 4);

    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                             "/reload_determinism_boston.cty";
    if (!session.save_city_as(path)) {
        return fail(2, "Boston did not save");
    }
    const unsigned tiles = session.tile_sample();
    const int score = session.evaluation().score;
    const long long time = session.city_time();
    const int wait = session.disaster_wait();
    const int event = session.disaster_event();

    std::ifstream in(scenario_sidecar_path(path));
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("\"rng\"") == std::string::npos || text.find("\"sim_cycle\"") == std::string::npos ||
        text.find("\"valves\"") == std::string::npos || text.find("\"phase\"") == std::string::npos) {
        return fail(3, "the sidecar did not store rng, sim_cycle, valves, and phase");
    }

    CitySession loaded;
    if (!loaded.load_city(path)) {
        return fail(4, "Boston did not reload: " + loaded.message());
    }
    if (loaded.city_time() != time || loaded.evaluation().score != score || loaded.tile_sample() != tiles ||
        loaded.disaster_wait() != wait || loaded.disaster_event() != event) {
        return fail(5, "the reloaded city did not match the save (score " +
                           std::to_string(loaded.evaluation().score) + " vs " + std::to_string(score) +
                           ", tiles " + (loaded.tile_sample() == tiles ? "ok" : "differ") + ")");
    }
    return 0;
}

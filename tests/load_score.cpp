// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// A saved city comes back with the score it was saved with (the load used
// to re-score a map with no traffic, so the number jumped, and a scenario
// reload could win what the uninterrupted game lost), and with the clock
// where it stood: a save taken partway through the simulator's cycle must
// not run the clock one step ahead. An old save with no score or phase in
// its goal file still loads.

#include "city_session.hpp"

#include "micropolis.h"
#include "scenario_sidecar.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
    const int scenarios[] = {SC_HAMBURG, SC_TOKYO, SC_BOSTON, SC_RIO};
    for (int scenario : scenarios) {
        for (int extra = 0; extra < 16; extra += 5) {
            CitySession session;
            if (!session.load_scenario(scenario)) {
                return fail(1, "scenario did not load: " + session.message());
            }
            session.set_speed(3);
            session.set_simulation_paused(false);
            run_until(session, session.city_time() + 24 * 4);
            run_ticks(session, extra);
            const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                                     "/load_score_" + std::to_string(scenario) + "_" + std::to_string(extra) + ".cty";
            if (!session.save_city_as(path)) {
                return fail(2, "save failed");
            }
            const int saved_score = session.evaluation().score;
            const long long saved_time = session.city_time();
            const std::string saved_clock = session.scenario_clock_text();
            CitySession loaded;
            if (!loaded.load_city(path)) {
                return fail(3, "load failed: " + loaded.message());
            }
            if (loaded.evaluation().score != saved_score) {
                return fail(4, "scenario " + std::to_string(scenario) + ": score " + std::to_string(saved_score) +
                                   " came back as " + std::to_string(loaded.evaluation().score));
            }
            if (loaded.city_time() != saved_time || loaded.scenario_clock_text() != saved_clock) {
                return fail(5, "the clock moved across a load");
            }
            // Eight more ticks of city time: both games have spent the same
            // number of goal cycles, so the goal clock reads the same.
            session.set_simulation_paused(false);
            loaded.set_speed(3);
            loaded.set_simulation_paused(false);
            run_until(session, saved_time + 8);
            run_until(loaded, saved_time + 8);
            if (loaded.city_time() != session.city_time() ||
                loaded.scenario_clock_text() != session.scenario_clock_text()) {
                return fail(6, "scenario " + std::to_string(scenario) + ": the goal clock drifted after a load: '" +
                                   session.scenario_clock_text() + "' vs '" + loaded.scenario_clock_text() + "'");
            }
            // An older goal file has neither key. It loads and keeps working.
            const std::string sidecar = scenario_sidecar_path(path);
            std::ifstream in(sidecar, std::ios::binary);
            std::stringstream body;
            body << in.rdbuf();
            std::string text = body.str();
            if (text.find("\"score\"") == std::string::npos || text.find("\"phase\"") == std::string::npos) {
                return fail(7, "the goal file did not keep the score and phase");
            }
            for (const char *key : {"\"score\"", "\"phase\""}) {
                const auto at = text.find(key);
                const auto line_start = text.rfind('\n', at);
                const auto line_end = text.find('\n', at);
                text.erase(line_start == std::string::npos ? 0 : line_start, line_end - (line_start == std::string::npos ? 0 : line_start));
            }
            std::ofstream out(sidecar, std::ios::binary | std::ios::trunc);
            out << text;
            out.close();
            CitySession old_style;
            if (!old_style.load_city(path)) {
                return fail(8, "an older goal file did not load");
            }
            run_ticks(old_style, 40);
        }
    }
    return 0;
}

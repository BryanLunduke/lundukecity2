// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// A scenario save keeps the scripted disaster timer in the .cty.json
// sidecar. After a month-0 save and reload, every scenario arms at the
// same week an uninterrupted run would. An older sidecar without the
// new keys recomputes the remaining wait from the scenario start when
// the event has not begun yet, and does not re-arm an event that has
// already started.

#include "city_session.hpp"
#include "scenario_sidecar.hpp"

#include "micropolis.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

const char *scenario_name(int id)
{
    if (id >= SC_DULLSVILLE && id < SC_COUNT) {
        return CitySession::scenario_def(id - 1).name;
    }
    return "unknown";
}

std::string tmp_path(const std::string &stem)
{
    const char *dir = std::getenv("TMPDIR");
    return std::string(dir ? dir : "/tmp") + "/" + stem;
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

bool load_fresh(CitySession &session, int scenario)
{
    if (!session.load_scenario(scenario)) {
        return false;
    }
    session.set_speed(3);
    session.set_simulation_paused(false);
    return true;
}

// Drop disaster_event / disaster_wait so the file looks like an older sidecar.
bool strip_disaster_keys(const std::string &city_path)
{
    const std::string side = scenario_sidecar_path(city_path);
    std::ifstream in(side, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    auto drop = [&text](const char *key) {
        const std::string token = std::string("\"") + key + "\"";
        const auto at = text.find(token);
        if (at == std::string::npos) {
            return;
        }
        auto end = text.find_first_of(",\n", at);
        if (end == std::string::npos) {
            return;
        }
        if (text[end] == ',') {
            ++end;
            if (end < text.size() && text[end] == '\n') {
                ++end;
            }
            text.erase(at, end - at);
            return;
        }
        // Last field: remove the comma on the previous field too.
        text.erase(at, end - at);
        const auto comma = text.find_last_of(',');
        if (comma != std::string::npos) {
            text.erase(comma, 1);
        }
    };
    drop("disaster_event");
    drop("disaster_wait");
    std::ofstream out(side, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << text;
    ScenarioClock clock;
    return scenario_sidecar_parse(text, clock) && !clock.disaster_present;
}

} // namespace

int main()
{
    const int scenarios[] = {
        SC_DULLSVILLE, SC_SAN_FRANCISCO, SC_HAMBURG, SC_BERN, SC_TOKYO, SC_DETROIT,
        SC_BOSTON,     SC_RIO,           SC_SEATTLE, SC_SAN_FRANCISCO_1962,
        SC_PORTLAND,   SC_SAN_DIEGO,
    };

    // Month-0 save/reload keeps the opening timer, and the event is due
    // the same week as an uninterrupted run.
    for (int scenario : scenarios) {
        CitySession fresh;
        if (!load_fresh(fresh, scenario)) {
            return fail(1, std::string(scenario_name(scenario)) + " did not load: " + fresh.message());
        }
        const int event0 = fresh.disaster_event();
        const int wait0 = fresh.disaster_wait();
        const int initial = static_cast<int>(Micropolis::disasterWaitAtStart(static_cast<Scenario>(scenario)));
        if (event0 != scenario || wait0 != initial) {
            return fail(2, std::string(scenario_name(scenario)) + " opened with disaster " +
                               std::to_string(event0) + " wait " + std::to_string(wait0) + ", expected " +
                               std::to_string(scenario) + " / " + std::to_string(initial));
        }

        const std::string path = tmp_path("disaster_wait_" + std::to_string(scenario) + ".cty");
        if (!fresh.save_city_as(path)) {
            return fail(3, std::string(scenario_name(scenario)) + " did not save");
        }
        {
            std::ifstream in(scenario_sidecar_path(path));
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ScenarioClock clock;
            if (!scenario_sidecar_parse(text, clock) || !clock.disaster_present || clock.disaster_event != event0 ||
                clock.disaster_wait != wait0) {
                return fail(4, std::string(scenario_name(scenario)) +
                                   " sidecar did not store the disaster timer");
            }
        }

        CitySession loaded;
        if (!loaded.load_city(path)) {
            return fail(5, std::string(scenario_name(scenario)) + " did not reload: " + loaded.message());
        }
        if (loaded.disaster_event() != event0 || loaded.disaster_wait() != wait0 ||
            loaded.city_time() != fresh.city_time()) {
            return fail(6, std::string(scenario_name(scenario)) + " month-0 reload armed disaster " +
                               std::to_string(loaded.disaster_event()) + " wait " +
                               std::to_string(loaded.disaster_wait()) + ", expected " + std::to_string(event0) +
                               " / " + std::to_string(wait0));
        }

        // Older sidecar: drop the new keys and load again. The remaining
        // wait is recomputed from the scenario start (still month 0).
        if (!strip_disaster_keys(path)) {
            return fail(7, std::string(scenario_name(scenario)) + " could not write an older sidecar");
        }
        CitySession older;
        if (!older.load_city(path)) {
            return fail(8, std::string(scenario_name(scenario)) + " did not load an older sidecar: " +
                               older.message());
        }
        if (older.disaster_event() != event0 || older.disaster_wait() != wait0) {
            return fail(9, std::string(scenario_name(scenario)) +
                               " older sidecar did not recompute the opening wait (got " +
                               std::to_string(older.disaster_event()) + " / " + std::to_string(older.disaster_wait()) +
                               ")");
        }

        // The reloaded city and a never-saved run reach the same disaster
        // state after the opening wait has run out (or, for repeating
        // disasters, after the first week).
        CitySession control;
        if (!load_fresh(control, scenario)) {
            return fail(10, std::string(scenario_name(scenario)) + " control did not load");
        }
        const long long until = fresh.city_time() + static_cast<long long>(wait0) + 2;
        run_until(loaded, until);
        run_until(control, until);
        if (loaded.disaster_event() != control.disaster_event() || loaded.disaster_wait() != control.disaster_wait()) {
            return fail(11, std::string(scenario_name(scenario)) +
                               " reloaded timer diverged from an uninterrupted run (reload " +
                               std::to_string(loaded.disaster_event()) + "/" + std::to_string(loaded.disaster_wait()) +
                               ", control " + std::to_string(control.disaster_event()) + "/" +
                               std::to_string(control.disaster_wait()) + ")");
        }
        if (scenario == SC_SEATTLE && control.events_state().zoneCount == 0) {
            return fail(12, "Seattle's zone did not form after a month-0 reload");
        }
        if (scenario == SC_SAN_FRANCISCO_1962 && control.events_state().prisonSteps == 0) {
            return fail(13, "the 1962 escape did not start after a month-0 reload");
        }
        if (scenario == SC_PORTLAND && loaded.events_state().riotSteps == 0) {
            return fail(14, "Portland's riot did not start after a month-0 reload");
        }
        if (scenario == SC_SAN_DIEGO && loaded.events_state().lionSteps == 0 && loaded.events_state().lionActive == 0) {
            return fail(15, "San Diego's lion did not start after a month-0 reload");
        }
    }

    // Mid-wait: save two weeks in, reload, the remaining wait matches, and
    // the event is still pending.
    {
        CitySession session;
        if (!load_fresh(session, SC_SEATTLE)) {
            return fail(16, "Seattle mid-wait did not load");
        }
        const long long start = session.city_time();
        run_until(session, start + 2);
        const int event = session.disaster_event();
        const int wait = session.disaster_wait();
        if (event != SC_SEATTLE || wait <= 0 || wait >= Micropolis::disasterWaitAtStart(SC_SEATTLE)) {
            return fail(17, "Seattle was not mid-wait after two weeks (wait " + std::to_string(wait) + ")");
        }
        const std::string path = tmp_path("disaster_wait_seattle_mid.cty");
        if (!session.save_city_as(path)) {
            return fail(18, "Seattle mid-wait did not save");
        }
        CitySession loaded;
        if (!loaded.load_city(path) || loaded.disaster_event() != event || loaded.disaster_wait() != wait) {
            return fail(19, "Seattle mid-wait reload lost the timer");
        }
        if (!strip_disaster_keys(path)) {
            return fail(20, "could not write an older Seattle mid-wait sidecar");
        }
        CitySession older;
        if (!older.load_city(path) || older.disaster_event() != event || older.disaster_wait() != wait) {
            return fail(21, "an older Seattle mid-wait sidecar did not recompute the remaining wait (got " +
                                std::to_string(older.disaster_event()) + " / " + std::to_string(older.disaster_wait()) +
                                ")");
        }
        // And the zone still forms afterwards, in step with a control.
        CitySession control;
        if (!load_fresh(control, SC_SEATTLE)) {
            return fail(22, "Seattle mid-wait control did not load");
        }
        run_until(control, start + 2);
        const long long until = start + Micropolis::disasterWaitAtStart(SC_SEATTLE) + 2;
        run_until(loaded, until);
        run_until(control, until);
        if (loaded.events_state().zoneCount == 0 || loaded.events_state().zoneCount != control.events_state().zoneCount) {
            return fail(23, "Seattle mid-wait reload did not form the zone with the uninterrupted run");
        }
    }

    // After the event has started, an older sidecar must not re-arm the wait.
    {
        CitySession session;
        if (!load_fresh(session, SC_SEATTLE)) {
            return fail(24, "Seattle post-event did not load");
        }
        const long long start = session.city_time();
        run_until(session, start + Micropolis::disasterWaitAtStart(SC_SEATTLE) + 2);
        if (session.events_state().zoneCount == 0) {
            return fail(25, "Seattle's zone did not form before the post-event save");
        }
        const int event = session.disaster_event();
        const int wait = session.disaster_wait();
        if (event == SC_SEATTLE && wait > 0) {
            return fail(26, "Seattle's disaster timer was still running after the zone formed");
        }
        const std::string path = tmp_path("disaster_wait_seattle_after.cty");
        if (!session.save_city_as(path)) {
            return fail(27, "Seattle post-event did not save");
        }
        CitySession loaded;
        if (!loaded.load_city(path) || loaded.disaster_event() != event || loaded.disaster_wait() != wait ||
            loaded.events_state().zoneCount != session.events_state().zoneCount) {
            return fail(28, "Seattle post-event reload lost the zone or re-armed the timer");
        }
        if (!strip_disaster_keys(path)) {
            return fail(29, "could not write an older Seattle post-event sidecar");
        }
        CitySession older;
        if (!older.load_city(path)) {
            return fail(30, "Seattle post-event older sidecar did not load");
        }
        if (older.disaster_event() == SC_SEATTLE && older.disaster_wait() > 0) {
            return fail(31, "an older sidecar re-armed Seattle's zone after it had already formed");
        }
        if (older.events_state().zoneCount == 0) {
            return fail(32, "an older sidecar lost the already-formed zone");
        }
    }

    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Review nov24: Learn to Play has no hidden deadline (#2), its steps
// latch (#3/#4, saved with the city), the goal meter groups digits (#6),
// and the scenario-local budget tweaks (#12/#13).

#include "city_session.hpp"
#include "micropolis.h"
#include "tool_groups.hpp"
#include "tools.hpp"
#include "tutorial_goals.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int hostile_review_session_probe(CitySession &session, int op);

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

const char *tmp_base()
{
    const char *dir = std::getenv("TMPDIR");
    return dir != nullptr && dir[0] != '\0' ? dir : "/tmp";
}

} // namespace

int main()
{
    // Release-readiness #1: the third power plant satisfies Power too.
    const auto micro = tutorial::census([](int, int) { return SPECIAL_MICRONUKE; }, 1, 1, 0, false, false);
    if (micro.power_plants != 1) {
        return fail(20, "the tutorial ignored a Micro Nuclear Plant");
    }

    // Release-readiness #2: all three Parks row tools satisfy Parks.
    for (int park_tile : {WOODS2, FOUNTAIN, SPECIAL_PARK, SPECIAL_FOUNTAIN}) {
        const auto parks = tutorial::census([&](int, int) { return park_tile; }, 1, 1, 0, false, false);
        if (!tutorial::step_complete(tutorial::Parks, parks)) {
            return fail(21, "the tutorial ignored a park or fountain");
        }
    }
    // Release-readiness #3: Inspect has a desktop toolbar button, and Step 6 says where.
    {
        if (kPaletteToolCount != kToolCount || tool_by_index(kToolCount - 1)->engine_id != TOOL_QUERY) {
            return fail(22, "Inspect is not on the desktop toolbar");
        }
        for (int columns = 2; columns <= 4; ++columns) {
            int x = 0;
            int y = 0;
            if (!tool_groups::slot(kToolCount - 1, columns, 40, 18, x, y) ||
                tool_groups::index_at(x + 20, y + 20, columns, 40, 18) != kToolCount - 1 ||
                y + 40 > tool_groups::grid_height(columns, 40, 18)) {
                return fail(23, "the Inspect button has no slot in the grid");
            }
        }
        if (tool_groups::group_of(kToolCount - 1) != -1 ||
            tool_groups::section_jump(tool_groups::groups()[6].first, 1, kToolCount) != kToolCount - 1) {
            return fail(24, "the handheld section jumps changed");
        }
        const std::string step6 = tutorial::hint(tutorial::InspectBudget);
        if (step6.find("magnifier") == std::string::npos || step6.find("press Q") == std::string::npos) {
            return fail(25, "Step 6 does not say where Inspect is");
        }
    }

    // #6: grouped digits in every meter form.
    tutorial::Status s;
    s.population = 1440;
    if (tutorial::meter(tutorial::Population, s) != "Step 7 of 7 · 1,440 → 2,000" ||
        tutorial::meter_short(tutorial::Population, s) != "1,440→2,000" ||
        tutorial::meter_compact(tutorial::Population, s) != "1,440→2k") {
        std::fprintf(stderr, "'%s' '%s' '%s'\n", tutorial::meter(tutorial::Population, s).c_str(),
                     tutorial::meter_short(tutorial::Population, s).c_str(),
                     tutorial::meter_compact(tutorial::Population, s).c_str());
        return fail(1, "the population meter is not grouped");
    }
    if (tutorial::grouped_count(0) != "0" || tutorial::grouped_count(999) != "999" ||
        tutorial::grouped_count(1000) != "1,000" || tutorial::grouped_count(1234567) != "1,234,567") {
        return fail(2, "grouped_count is wrong");
    }

    // #3/#4: a passed step stays passed.
    tutorial::Status empty;
    if (tutorial::latched_step(empty, 0) != tutorial::Roads || tutorial::latched_step(empty, tutorial::Safety) != tutorial::Safety ||
        tutorial::latched_step(empty, tutorial::kStepCount) != tutorial::kStepCount) {
        return fail(3, "latched_step went back to an earlier step");
    }

    CitySession city;
    if (!city.load_scenario(SC_LEARN_TO_PLAY)) {
        return fail(4, "could not load Brookside");
    }
    city.setup_tutorial_through(tutorial::Zones);
    if (city.tutorial_step() != tutorial::Safety) {
        return fail(5, "the starter zones did not reach police and fire");
    }
    // Bulldoze most of the teaching road.
    for (int x = tutorial::kRoadX0; x < tutorial::kRoadX0 + 16; ++x) {
        city.use_tool(TOOL_BULLDOZER, x, tutorial::kRoadY);
        city.end_tool_stroke();
    }
    if (city.tutorial_step() != tutorial::Safety) {
        std::fprintf(stderr, "step %d after bulldozing\n", city.tutorial_step());
        return fail(6, "bulldozing the teaching road sent the tutorial back");
    }
    const std::string path = std::string(tmp_base()) + "/review_nov24_learn.cty";
    if (!city.save_city_as(path)) {
        return fail(7, "could not save Brookside");
    }
    CitySession reloaded;
    if (!reloaded.load_city(path) || reloaded.tutorial_step() != tutorial::Safety) {
        std::fprintf(stderr, "reloaded step %d\n", reloaded.tutorial_step());
        return fail(8, "the latched step did not survive save and load");
    }
    std::remove(path.c_str());
    std::remove((path + ".json").c_str());

    // #2: Brookside's score timer never runs down.
    CitySession idle;
    if (!idle.load_scenario(SC_LEARN_TO_PLAY)) {
        return fail(9, "could not load Brookside again");
    }
    const int wait0 = hostile_review_session_probe(idle, 22);
    idle.set_speed(3);
    idle.set_simulation_paused(false);
    for (int i = 0; i < 4000; ++i) {
        idle.tick();
    }
    const int wait1 = hostile_review_session_probe(idle, 22);
    if (wait0 <= 0 || wait1 != wait0) {
        std::fprintf(stderr, "scoreWait %d -> %d\n", wait0, wait1);
        return fail(10, "Learn to Play's score timer still counts down");
    }
    // Other scenarios still count down.
    CitySession dull;
    if (!dull.load_scenario(SC_DULLSVILLE)) {
        return fail(11, "could not load Dullsville");
    }
    const int dull0 = hostile_review_session_probe(dull, 22);
    dull.set_speed(3);
    dull.set_simulation_paused(false);
    for (int i = 0; i < 2000; ++i) {
        dull.tick();
    }
    if (hostile_review_session_probe(dull, 22) >= dull0) {
        return fail(12, "Dullsville's deadline stopped counting");
    }

    // #12/#13: scenario-local budget tweaks only.
    auto owned = std::make_unique<Micropolis>();
    Micropolis &sim = *owned;
    struct Want {
        Scenario which;
        Quad police;
        Quad fire;
    };
    const Want wants[] = {
        {SC_NONE, 100, 100},     {SC_DULLSVILLE, 100, 100}, {SC_SEATTLE, 100, 100},
        {SC_PORTLAND, 100, 60},  {SC_SAN_DIEGO, 100, 100},  {SC_LEARN_TO_PLAY, 25, 25},
    };
    for (const Want &want : wants) {
        sim.scenario = want.which;
        const Micropolis::BudgetBases bases = sim.projectBudget(0, 0, 2, 3, 0, 0, 7);
        if (sim.stationUpkeep(false) != want.police || sim.stationUpkeep(true) != want.fire ||
            bases.police != 2 * want.police || bases.fire != 3 * want.fire) {
            std::fprintf(stderr, "scenario %d: police %d fire %d\n", static_cast<int>(want.which),
                         static_cast<int>(sim.stationUpkeep(false)), static_cast<int>(sim.stationUpkeep(true)));
            return fail(13, "a station upkeep is wrong");
        }
    }
    // Every scenario keeps the 7% starting tax (editor's call, nov24).
    CitySession portland;
    if (!portland.load_scenario(SC_PORTLAND) || portland.tax() != 7) {
        return fail(14, "Portland does not start at 7%");
    }
    CitySession san_diego;
    if (!san_diego.load_scenario(SC_SAN_DIEGO) || san_diego.tax() != 7) {
        return fail(16, "San Diego does not start at 7%");
    }
    CitySession seattle;
    if (!seattle.load_scenario(SC_SEATTLE) || seattle.tax() != 7) {
        return fail(15, "Seattle's start tax changed");
    }

    std::printf("review_nov24 ok\n");
    return 0;
}

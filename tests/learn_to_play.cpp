// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Learn to Play / Brookside: catalog, map, teaching steps, determinism.

#include "city_session.hpp"
#include "scenario_briefing.hpp"
#include "micropolis.h"
#include "tutorial_goals.hpp"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

} // namespace

int main()
{
    const CitySession::ScenarioDef &def = CitySession::scenario_def(SC_LEARN_TO_PLAY - 1);
    if (def.id != SC_LEARN_TO_PLAY || std::string(def.name) != "Learn to Play" || def.year != 1900 ||
        def.years != 0 || std::string(CitySession::scenario_city(def)) != "Brookside") {
        return fail(1, "Learn to Play is missing from the catalog or has the wrong name/year");
    }
    if (scenario_difficulty_from(def.years, def.goal) != ScenarioDifficulty::Easy ||
        std::string(scenario_difficulty_tag(def.years, def.goal)) != "Easy" ||
        scenario_years_label(def.years) != "No deadline") {
        return fail(2, "Learn to Play should be Easy with no deadline");
    }
    if (std::string(def.summary).empty() || std::string(def.goal).find("2,000") == std::string::npos ||
        std::string(def.goal).find("no deadline") == std::string::npos) {
        return fail(3, "the briefing goal does not mention the population target or the open clock");
    }

    CitySession city;
    if (!city.load_scenario(SC_LEARN_TO_PLAY) || city.city_name() != "Brookside" || city.funds() != 20000 ||
        city.scenario_years_left() != 0) {
        std::fprintf(stderr, "name '%s' funds %lld left %d\n", city.city_name().c_str(),
                     static_cast<long long>(city.funds()), city.scenario_years_left());
        return fail(4, "Brookside did not load at $20,000 with no deadline");
    }
    if (city.tutorial_step() != tutorial::Roads || city.scenario_progress().find("Step 1 of 7") == std::string::npos ||
        city.scenario_meter().find("Roads") == std::string::npos || !city.scenario_clock_text().empty()) {
        std::fprintf(stderr, "step %d progress '%s' meter '%s' clock '%s'\n", city.tutorial_step(),
                     city.scenario_progress().c_str(), city.scenario_meter().c_str(), city.scenario_clock_text().c_str());
        return fail(5, "a fresh Brookside did not open on the Roads step");
    }

    const std::string first = city.change_fingerprint();
    CitySession again;
    if (!again.load_scenario(SC_LEARN_TO_PLAY) || again.change_fingerprint() != first) {
        return fail(6, "loading Brookside twice did not produce the same city");
    }

    // Pure census helpers: an empty status is step 1; filling roads, then
    // a plant, then all three zones, advances in order.
    tutorial::Status empty;
    if (tutorial::current_step(empty) != tutorial::Roads) {
        return fail(7, "an empty census should start at Roads");
    }
    empty.roads = tutorial::kRoadTiles;
    if (tutorial::current_step(empty) != tutorial::Power) {
        return fail(8, "enough roads should unlock Power");
    }
    empty.power_plants = 1;
    if (tutorial::current_step(empty) != tutorial::Zones) {
        return fail(9, "a plant should unlock Zones");
    }
    empty.res_zones = 1;
    empty.com_zones = 1;
    empty.ind_zones = 1;
    if (tutorial::current_step(empty) != tutorial::Safety) {
        return fail(10, "R, C and I should unlock police and fire");
    }
    empty.police = 1;
    empty.fire = 1;
    if (tutorial::current_step(empty) != tutorial::Parks) {
        return fail(11, "stations should unlock Parks");
    }
    empty.parks = 1;
    if (tutorial::current_step(empty) != tutorial::InspectBudget) {
        return fail(12, "a park should unlock Inspect and Budget");
    }
    empty.inspected = true;
    empty.budget_seen = true;
    if (tutorial::current_step(empty) != tutorial::Population) {
        return fail(13, "Inspect and Budget should unlock the population goal");
    }
    empty.population = LEARN_TO_PLAY_POPULATION_GOAL;
    if (tutorial::current_step(empty) != tutorial::kStepCount || !tutorial::step_complete(tutorial::Population, empty)) {
        return fail(14, "2,000 people should finish the tutorial");
    }

    city.setup_tutorial_through(tutorial::Roads);
    if (city.tutorial_step() != tutorial::Power) {
        std::fprintf(stderr, "after roads: step %d meter '%s'\n", city.tutorial_step(), city.scenario_meter().c_str());
        return fail(15, "laying the starter road did not advance to Power");
    }
    city.setup_tutorial_through(tutorial::Power);
    if (city.tutorial_step() != tutorial::Zones) {
        return fail(16, "placing a plant did not advance to Zones");
    }
    city.setup_tutorial_through(tutorial::Zones);
    if (city.tutorial_step() != tutorial::Safety) {
        return fail(17, "the starter R/C/I zones did not advance to police and fire");
    }
    city.setup_tutorial_through(tutorial::Safety);
    if (city.tutorial_step() != tutorial::Parks) {
        return fail(18, "police and fire did not advance to Parks");
    }
    city.setup_tutorial_through(tutorial::Parks);
    if (city.tutorial_step() != tutorial::InspectBudget) {
        return fail(19, "a park did not advance to Inspect and Budget");
    }
    city.note_tutorial_inspect();
    if (city.tutorial_step() != tutorial::InspectBudget) {
        return fail(20, "Inspect alone should not finish the Inspect and Budget step");
    }
    city.note_tutorial_budget();
    if (city.tutorial_step() != tutorial::Population) {
        return fail(21, "Inspect and Budget together should open the population step");
    }
    if (city.scenario_meter().find("2,000") == std::string::npos) {
        std::fprintf(stderr, "meter '%s'\n", city.scenario_meter().c_str());
        return fail(22, "the last step should show the 2,000-person target");
    }

    // Other scenarios are untouched.
    CitySession dull;
    if (!dull.load_scenario(SC_DULLSVILLE) || dull.city_name() != "Dullsville" || dull.funds() != 5000 ||
        dull.scenario_years_left() != 30) {
        return fail(23, "Dullsville changed when Learn to Play was added");
    }

    std::printf("learn_to_play ok\n");
    return 0;
}

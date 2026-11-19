// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the second November 18 review (nov18b) that the
// special-building tests do not already cover.

#include "city_session.hpp"
#include "dialog_copy.hpp"
#include "inspect_report.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

CitySession::NewCitySpec flat_spec(const char *name, int level)
{
    CitySession::NewCitySpec spec;
    spec.name = name;
    spec.seed = 12;
    spec.seed_was_set = true;
    spec.difficulty = level;
    spec.island = CitySession::kTerrainOff;
    spec.rivers = CitySession::kTerrainOff;
    spec.lakes = CitySession::kTerrainOff;
    spec.trees = CitySession::kTerrainOff;
    return spec;
}

// #4: a new city opens at 7/6/5% and its Budget shows no tax warning.
int check_default_tax()
{
    const int expect[] = {7, 6, 5};
    for (int level = CitySession::kLevelEasy; level <= CitySession::kLevelHard; ++level) {
        CitySession city;
        city.new_city(flat_spec("Tax Town", level));
        if (city.tax() != expect[level] || CitySession::default_tax(level) != expect[level]) {
            std::fprintf(stderr, "level %d tax %d\n", level, city.tax());
            return fail(1, "a new city did not open at its level's default tax");
        }
        if (tax_warning(city.tax(), city.difficulty()).level != 0) {
            std::fprintf(stderr, "level %d note '%s'\n", level, tax_warning(city.tax(), level).note.c_str());
            return fail(2, "a new city's default tax shows a Budget warning");
        }
        if (tax_warning(city.tax() + 1, level).level == 0) {
            return fail(3, "the default tax is not the highest rate without a warning");
        }
    }
    return 0;
}

int index_named(const char *name)
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).name) == name) {
            return i;
        }
    }
    return -1;
}

void run_ticks(CitySession &city, int ticks)
{
    city.set_simulation_paused(false);
    for (int i = 0; i < ticks; ++i) {
        city.tick();
        if (city.budget_pending()) {
            city.commit_pending_budget();
        }
        (void)city.take_budget_request();
    }
}

// A powered casino on a road, with nobody living anywhere: the Inspect
// card says Idle and gives one note.
int check_idle_card()
{
    CitySession city;
    city.new_city(flat_spec("Idle Town", CitySession::kLevelEasy));
    city.set_auto_bulldoze(true);
    city.set_disasters(false);
    city.use_tool(TOOL_COALPOWER, 50, 50);
    city.drag_tool(TOOL_WIRE, 51, 53, 55, 53);
    city.drag_tool(TOOL_ROAD, 54, 57, 58, 57);
    const int casino = index_named("Casino");
    if (casino < 0 || !city.place_special(casino, 56, 55)) {
        return fail(10, "could not place the casino for the idle check");
    }
    run_ticks(city, 400);
    const CitySession::InspectFacts facts = city.inspect_facts(56, 55);
    const inspect_report::Report report = inspect_report::make(facts.facts);
    if (facts.facts.power != 1 || !facts.facts.access) {
        std::fprintf(stderr, "power %d access %d\n", facts.facts.power, static_cast<int>(facts.facts.access));
        return fail(11, "the idle-check casino has no power or no road");
    }
    if (report.status != "Idle" || facts.notes.size() != 1 ||
        facts.notes[0] != "No income: no residents within 16 tiles") {
        std::fprintf(stderr, "status '%s' notes %zu\n", report.status.c_str(), facts.notes.size());
        for (const std::string &note : facts.notes) {
            std::fprintf(stderr, "  note '%s'\n", note.c_str());
        }
        return fail(12, "an income building with no residents is not Idle with one note");
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_default_tax()) {
        return r;
    }
    if (const int r = check_idle_card()) {
        return r;
    }
    std::puts("review_nov18b ok");
    return 0;
}

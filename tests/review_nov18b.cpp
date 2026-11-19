// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Regressions for the second November 18 review (nov18b) that the
// special-building tests do not already cover.

#include "city_session.hpp"
#include "dialog_copy.hpp"
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

} // namespace

int main()
{
    if (const int r = check_default_tax()) {
        return r;
    }
    std::puts("review_nov18b ok");
    return 0;
}

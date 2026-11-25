// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Regressions for the November 25 review that need a whole CitySession.

#include "city_session.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

CitySession::NewCitySpec flat_spec(const char *name)
{
    CitySession::NewCitySpec spec;
    spec.name = name;
    spec.seed = 12;
    spec.seed_was_set = true;
    spec.difficulty = CitySession::kLevelEasy;
    spec.island = CitySession::kTerrainOff;
    spec.rivers = CitySession::kTerrainOff;
    spec.lakes = CitySession::kTerrainOff;
    spec.trees = CitySession::kTerrainOff;
    return spec;
}

// Minor note: a paused city counted the coal plant and the zones built
// beside it or wired to it as unpowered until the simulator ran.
int check_unpowered_while_paused()
{
    CitySession city;
    city.new_city(flat_spec("Wire Town"));
    city.set_auto_bulldoze(true);
    city.set_disasters(false);
    city.set_simulation_paused(true);
    city.use_tool(TOOL_COALPOWER, 50, 50);
    city.use_tool(TOOL_RESIDENTIAL, 53, 50);
    if (city.crisis_report().unpowered != 0) {
        std::fprintf(stderr, "unpowered %d\n", city.crisis_report().unpowered);
        return fail(1, "a zone beside a plant, or the plant, counted as unpowered while paused");
    }
    city.use_tool(TOOL_INDUSTRIAL, 70, 50);
    if (city.crisis_report().unpowered != 1) {
        std::fprintf(stderr, "unpowered %d\n", city.crisis_report().unpowered);
        return fail(2, "a zone with no line to a plant did not count as unpowered");
    }
    city.drag_tool(TOOL_WIRE, 52, 52, 69, 52);
    city.drag_tool(TOOL_WIRE, 69, 51, 69, 51);
    if (city.crisis_report().unpowered != 0) {
        std::fprintf(stderr, "unpowered %d\n", city.crisis_report().unpowered);
        return fail(3, "a zone just wired to a plant still counted as unpowered while paused");
    }
    return 0;
}

} // namespace

int main()
{
    if (const int r = check_unpowered_while_paused()) {
        return r;
    }
    std::printf("review nov25 session checks passed\n");
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck hardware notes (Nov 25) #4: a Data Center makes no pollution. Its
// tiles read 0 on the pollution map and the Inspect card even beside a
// coal plant, while the land around it still shows the plant's smoke.

#include "city_session.hpp"
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

} // namespace

int main()
{
    CitySession city;
    city.new_city(flat_spec("Clean Servers"));
    city.set_auto_bulldoze(true);
    city.set_disasters(false);
    city.use_tool(TOOL_COALPOWER, 50, 50);
    if ((city.map_value(50, 50) & LOMASK) != POWERPLANT) {
        return fail(1, "could not build the coal plant");
    }
    // The plant covers 49..52; the Data Center 54..56, one tile of dirt apart.
    if (!city.place_special(special_datacenter_lot_index(), 55, 50)) {
        return fail(2, "could not build the Data Center");
    }
    city.set_simulation_paused(false);
    for (int i = 0; i < 1200; ++i) {
        city.tick();
        if (city.budget_pending()) {
            city.commit_pending_budget();
        }
        (void)city.take_budget_request();
    }
    if (city.layer_value(CitySession::MapLayer::Pollution, 50, 50) <= 0) {
        return fail(3, "the coal plant made no pollution, so the check proves nothing");
    }
    if (city.layer_value(CitySession::MapLayer::Pollution, 53, 50) <= 0) {
        return fail(4, "the dirt beside the plant should still show its pollution");
    }
    for (int y = 49; y <= 51; ++y) {
        for (int x = 54; x <= 56; ++x) {
            const int value = city.layer_value(CitySession::MapLayer::Pollution, x, y);
            if (value != 0) {
                std::fprintf(stderr, "tile %d,%d reads %d\n", x, y, value);
                return fail(5, "a Data Center tile reads as polluted");
            }
        }
    }
    if (!city.datacenter_tile(55, 50) || !city.datacenter_tile(54, 49) || city.datacenter_tile(53, 50) ||
        city.datacenter_tile(50, 50)) {
        return fail(7, "datacenter_tile does not match the Data Center's footprint");
    }
    const CitySession::TileInspect card = city.inspect_tile(55, 50);
    if (card.condition.find("Pollution") != std::string::npos) {
        return fail(6, "the Data Center's card names pollution: " + card.condition);
    }
    std::puts("deck_hw_nov25_city ok");
    return 0;
}

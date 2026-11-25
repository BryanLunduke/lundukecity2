// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck hardware notes (Nov 25) #4: a Data Center makes no pollution. Its
// tiles read 0 on the pollution map and the Inspect card even beside a
// coal plant, while the land around it still shows the plant's smoke.
// #10: a powered Data Center lifts the population density around it like
// a light C/I zone; an unpowered one does not.

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
    // One wire across the gap powers the Data Center (item 10 needs it lit).
    city.use_tool(TOOL_WIRE, 53, 50);
    // A house zone right beside the Data Center (57..59), powered through it,
    // a second Data Center on its far side (60..62), and a road along the
    // south edge: houses between two job sites, as in the dcpol MIX city.
    city.use_tool(TOOL_RESIDENTIAL, 58, 50);
    if (!city.place_special(special_datacenter_lot_index(), 61, 50)) {
        return fail(8, "could not build the second Data Center");
    }
    for (int x = 52; x <= 62; ++x) {
        city.use_tool(TOOL_ROAD, x, 52);
    }
    // Item 10: an unpowered Data Center far from the plant, as a control.
    if (!city.place_special(special_datacenter_lot_index(), 90, 80)) {
        return fail(8, "could not build the unpowered Data Center");
    }
    city.set_simulation_paused(false);
    for (int i = 0; i < 12000; ++i) {
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
    // Item 10: a powered, intact Data Center counts like light C/I in the
    // population density map (so houses beside it can pass the apartment
    // gate, > 64); an unpowered one adds nothing.
    if ((city.map_value(55, 50) & PWRBIT) == 0) {
        return fail(9, "the Data Center beside the plant has no power, so the check proves nothing");
    }
    const int dense = city.layer_value(CitySession::MapLayer::Population, 55, 50);
    const int houses = city.layer_value(CitySession::MapLayer::Population, 58, 50);
    const int cold = city.layer_value(CitySession::MapLayer::Population, 90, 80);
    const int zone = city.map_value(58, 50) & LOMASK;
    std::printf("density: powered DC %d, house zone %d (tile %d), unpowered DC %d\n", dense, houses, zone, cold);
    if (dense <= 0) {
        return fail(10, "a powered Data Center adds no population density");
    }
    if (cold != 0) {
        return fail(11, "an unpowered Data Center adds population density");
    }
    if (zone == FREEZ && houses <= 64) {
        return fail(12, "the house zone beside the Data Center is still under the apartment gate");
    }
    std::puts("deck_hw_nov25_city ok");
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Power plants (coal, nuclear, micro nuclear) and the Data Center work
// without a road, so their Inspect card never says "No road touches it" and
// the hover card reads "No road needed". Buildings that need road access
// (a fire station) still do. Water and bare land carry no road line at all.

#include "city_session.hpp"
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

bool says_no_road(const inspect_report::Report &report)
{
    for (const std::string &reason : report.reasons) {
        if (reason.rfind("No road", 0) == 0) {
            return true;
        }
    }
    return false;
}

// Top-left of a clear (dirt) square of side `side`, with a clear ring
// around it, so nothing (no road) touches what is built there.
bool clear_square(CitySession &city, int side, int skip_x, int &left, int &top)
{
    for (int y = 3; y + side + 3 < CitySession::kWorldH; ++y) {
        for (int x = skip_x; x + side + 3 < CitySession::kWorldW; ++x) {
            bool clear = true;
            for (int dy = -1; dy <= side && clear; ++dy) {
                for (int dx = -1; dx <= side && clear; ++dx) {
                    clear = (city.map_value(x + dx, y + dy) & LOMASK) == DIRT;
                }
            }
            if (clear) {
                left = x;
                top = y;
                return true;
            }
        }
    }
    return false;
}

struct Case {
    const char *what;
    int side;
    // Works without a road: no "No road" reason, grey "No road needed".
    bool exempt;
};

} // namespace

int main()
{
    if (!tile_is_power_plant(POWERPLANT) || !tile_is_power_plant(NUCLEAR) || !tile_is_power_plant(SPECIAL_MICRONUKE) ||
        tile_is_power_plant(SPECIAL_DATACENTER) || tile_is_power_plant(SPECIAL_DATACENTER_LOT) ||
        tile_is_power_plant(INDBASE + 4) || tile_is_power_plant(FIRESTATION) || tile_is_power_plant(POLICESTATION)) {
        return fail(1, "tile_is_power_plant picks the wrong tiles");
    }

    // The report itself: a building without a road lists "No road" unless
    // it does not need one.
    {
        inspect_report::Facts plant;
        plant.kind = inspect_report::Kind::Building;
        plant.power = 1;
        plant.access = false;
        plant.needs_road = false;
        const inspect_report::Report quiet = inspect_report::make(plant);
        if (says_no_road(quiet) || quiet.status != "Working") {
            return fail(2, "a power plant without a road should read Working");
        }
        inspect_report::Facts other = plant;
        other.needs_road = true;
        const inspect_report::Report loud = inspect_report::make(other);
        if (!says_no_road(loud) || loud.status != "Needs attention") {
            return fail(3, "a building that needs a road should still say No road");
        }
    }

    // Real buildings on bare land with no road anywhere near them.
    CitySession city;
    city.new_city("Plants", 11);
    city.set_disasters(false);
    city.set_auto_bulldoze(true);
    const Case cases[] = {
        {"coal", 4, true}, {"nuclear", 4, true}, {"micro", 1, true}, {"fire", 3, false}, {"datacenter", 3, true},
    };
    int skip_x = 3;
    for (const Case &c : cases) {
        int left = 0;
        int top = 0;
        if (!clear_square(city, c.side, skip_x, left, top)) {
            return fail(4, std::string("no clear land for ") + c.what);
        }
        skip_x = 3;
        const std::string what = c.what;
        int qx = left;
        int qy = top;
        bool placed = false;
        if (what == "coal") {
            city.use_tool(TOOL_COALPOWER, left + 1, top + 1);
            placed = (city.map_value(left + 1, top + 1) & LOMASK) == POWERPLANT;
        } else if (what == "nuclear") {
            city.use_tool(TOOL_NUCLEARPOWER, left + 1, top + 1);
            placed = (city.map_value(left + 1, top + 1) & LOMASK) == NUCLEAR;
        } else if (what == "micro") {
            placed = city.place_special(special_micro_nuclear_index(), left, top);
        } else if (what == "fire") {
            city.use_tool(TOOL_FIRESTATION, left + 1, top + 1);
            placed = (city.map_value(left + 1, top + 1) & LOMASK) == FIRESTATION;
        } else {
            placed = city.place_special(special_datacenter_lot_index(), left + 1, top + 1);
        }
        if (!placed) {
            return fail(5, std::string("could not build the ") + c.what);
        }
        // Inspect a corner tile, not the centre: the whole footprint counts.
        const CitySession::InspectFacts facts = city.inspect_facts(qx, qy);
        if (!facts.valid || facts.facts.access) {
            return fail(6, std::string("the ") + c.what + " should have no road");
        }
        if (facts.facts.needs_road == c.exempt) {
            return fail(7, std::string("the ") + c.what + " has the wrong road need");
        }
        const inspect_report::Report report = inspect_report::make(facts.facts);
        if (says_no_road(report) == c.exempt) {
            std::fprintf(stderr, "%s: %s\n", c.what, report.status.c_str());
            return fail(8, std::string("the ") + c.what + (c.exempt ? " says No road" : " lost its No road warning"));
        }
        if (c.exempt && report.status != "Working" && facts.facts.power == 1) {
            return fail(9, std::string("the powered ") + c.what + " should read Working, not " + report.status);
        }
        // The hover card: grey "No road needed" for an exempt building,
        // the red "No direct road" only where a road is needed.
        const CitySession::TileInspect hover = city.inspect_tile(qx, qy);
        const std::string want = c.exempt ? "No road needed" : "No direct road";
        if (hover.access != want) {
            return fail(10, std::string("the ") + c.what + " hover reads '" + hover.access + "', want '" + want + "'");
        }
        // A5: every tile of these $400+ buildings asks before a bulldozer
        // clears it, and names the building and its price.
        const int want_cost = what == "coal" ? 3000 : what == "nuclear" ? 5000 : what == "micro" ? 1000
                              : what == "fire" ? 500 : 400;
        for (int dy = 0; dy < c.side; ++dy) {
            for (int dx = 0; dx < c.side; ++dx) {
                std::string name;
                const int worth = city.bulldoze_worth(left + dx, top + dy, &name);
                if (worth != want_cost || name.empty()) {
                    std::fprintf(stderr, "%s at +%d,+%d: %d '%s'\n", c.what, dx, dy, worth, name.c_str());
                    return fail(12, std::string("the ") + c.what + " bulldoze price is wrong");
                }
            }
        }
        if (city.bulldoze_worth(left - 1, top - 1) != 0 && city.bulldoze_worth(left + c.side, top + c.side) != 0) {
            return fail(13, std::string("land next to the ") + c.what + " counts as part of it");
        }
    }
    // Non-buildings: no road line at all.
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            const int tile = city.map_value(x, y) & LOMASK;
            const bool water = tile >= RIVER && tile <= WATER_HIGH;
            if (!water && tile != DIRT) {
                continue;
            }
            const CitySession::TileInspect hover = city.inspect_tile(x, y);
            if (!hover.access.empty()) {
                return fail(11, std::string(water ? "water" : "dirt") + " shows a road line: " + hover.access);
            }
        }
    }
    // A cheap zone is not asked about.
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            const int tile = city.map_value(x, y) & LOMASK;
            if ((tile == DIRT || (tile >= RIVER && tile <= WATER_HIGH)) && city.bulldoze_worth(x, y) != 0) {
                return fail(14, "bare land or water has a bulldoze price");
            }
        }
    }
    std::puts("plant_inspect ok");
    return 0;
}

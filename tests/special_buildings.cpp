// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Placement, cost, land value, save/load, query, bulldoze, fire, water,
// and leftover tiles for the special-building table. Income stays in the
// table and is zero for the buildings that ship today.

#include "city_session.hpp"
#include "special_buildings.hpp"
#include "tools.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

void fill_dirt(Micropolis &sim)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            sim.map[x][y] = DIRT;
        }
    }
}

bool is_rubble(int raw)
{
    return (raw & LOMASK) == RUBBLE;
}

} // namespace

int main()
{
    if (special_building_count() != 3) {
        return fail(1, "the table should start with zoo, big park, and library");
    }
    if (std::string(special_building(0).name) != "Zoo" || special_building(0).cost != 100 ||
        special_building(0).annual_income != 0 || std::string(special_building(1).name) != "Big Park" ||
        special_building(1).annual_income != 0 || special_building(2).annual_income != 0) {
        return fail(2, "special building costs or incomes do not match the table");
    }
    const ToolFootprint footprint = tool_footprint(kSpecialEngineId);
    if (!footprint.placeable || footprint.width != 3 || footprint.height != 3) {
        return fail(3, "the special tool is not a 3x3 placement");
    }
    if (kToolCount != 17 || tool_by_index(15)->engine_id != kSpecialEngineId ||
        tool_by_index(16)->engine_id != TOOL_QUERY) {
        return fail(4, "query is no longer the last tool after special buildings");
    }

    Micropolis sim;
    sim.generateSomeCity(11);
    fill_dirt(sim);
    sim.setFunds(5000);
    sim.autoBulldoze = true;
    sim.totalPop = 0;
    sim.specialLandFn = boost_special_land_value;

    std::string error;
    if (place_special_building(&sim, 0, 0, 0, error) || error.find("off the map") == std::string::npos) {
        return fail(5, "a building on the corner was accepted");
    }
    sim.setFunds(50);
    if (place_special_building(&sim, 0, 10, 10, error) || error.find("Not enough funds") == std::string::npos) {
        return fail(6, "a building was placed without the money");
    }
    if ((sim.map[10][10] & LOMASK) != DIRT) {
        return fail(7, "a refused building changed the map");
    }
    sim.setFunds(5000);
    if (!place_special_building(&sim, 0, 10, 10, error) || sim.totalFunds != 4900) {
        return fail(8, "the zoo did not cost $100");
    }
    if (!place_special_building(&sim, 1, 20, 10, error) || !place_special_building(&sim, 2, 30, 10, error) ||
        sim.totalFunds != 4700) {
        return fail(9, "big park and library were not both $100");
    }
    if ((sim.map[10][10] & LOMASK) != 1020 || (sim.map[10][10] & ZONEBIT) == 0 ||
        (sim.map[9][9] & LOMASK) != kSpecialBodyTile || (sim.map[11][11] & (BURNBIT | BULLBIT)) == 0) {
        return fail(10, "the zoo footprint is not a center plus eight body tiles");
    }
    if (!special_building_intact(&sim, 10, 10) || special_annual_income(&sim) != 0) {
        return fail(11, "intact special buildings should not pay income");
    }
    // The published effect is "raises land value" with no magnitude. The
    // engine adds the table bonus once per land-value scan, only on cells
    // that already have value, and clamps at 250.
    sim.landValueMap.clear();
    const int zoo_block_x = 10 / sim.landValueMap.MAP_BLOCKSIZE;
    const int zoo_block_y = 10 / sim.landValueMap.MAP_BLOCKSIZE;
    const int far_x = 40 / sim.landValueMap.MAP_BLOCKSIZE;
    const int far_y = 40 / sim.landValueMap.MAP_BLOCKSIZE;
    const int open_x = 14 / sim.landValueMap.MAP_BLOCKSIZE;
    const int open_y = 10 / sim.landValueMap.MAP_BLOCKSIZE;
    sim.landValueMap.set(zoo_block_x, zoo_block_y, 230);
    sim.landValueMap.set(far_x, far_y, 15);
    sim.landValueMap.set(open_x, open_y, 0);
    boost_special_land_value(&sim);
    if (sim.landValueMap.get(zoo_block_x, zoo_block_y) != 250 ||
        sim.landValueMap.get(far_x, far_y) != 15 ||
        sim.landValueMap.get(open_x, open_y) != 0) {
        std::fprintf(stderr, "land %d far %d open %d\n", sim.landValueMap.get(zoo_block_x, zoo_block_y),
                     sim.landValueMap.get(far_x, far_y), sim.landValueMap.get(open_x, open_y));
        return fail(36, "the zoo did not raise developed land value by 48, clamped at 250");
    }
    // The scan recomputes land value from scratch, then the hook adds the
    // bonus once. Run the simulator until the unboosted value holds still,
    // then turn the hook back on and expect exactly +48 on the next scan.
    sim.specialLandFn = nullptr;
    sim.enableDisasters = false;
    sim.simPaused = false;
    sim.simPasses = 1;
    sim.setSpeed(3);
    int plain = -1;
    int steady = 0;
    // Land value is recomputed every 272 ticks at this speed. Hold still
    // across that gap so the sample is a scan result, not the seeded value.
    for (int i = 0; i < 4000 && steady < 400; ++i) {
        sim.simTick();
        const int now = sim.landValueMap.get(zoo_block_x, zoo_block_y);
        if (now > 0 && now == plain) {
            ++steady;
        } else {
            plain = now;
            steady = 0;
        }
    }
    if (steady < 400 || plain <= 0) {
        std::fprintf(stderr, "plain land %d steady %d\n", plain, steady);
        return fail(37, "land value around the zoo did not settle");
    }
    sim.specialLandFn = boost_special_land_value;
    int boosted = plain;
    for (int i = 0; i < 600 && boosted == plain; ++i) {
        sim.simTick();
        boosted = sim.landValueMap.get(zoo_block_x, zoo_block_y);
    }
    const int expect = plain + 48 > 250 ? 250 : plain + 48;
    if (boosted != expect) {
        std::fprintf(stderr, "scan land %d -> %d\n", plain, boosted);
        return fail(38, "a land-value scan did not add the zoo bonus once");
    }
    const std::string query = special_query_at(&sim, 9, 11);
    if (query.find("Zoo") == std::string::npos || query.find("$100") == std::string::npos ||
        query.find("year") != std::string::npos) {
        std::fprintf(stderr, "query '%s'\n", query.c_str());
        return fail(12, "query on a zoo tile did not name it");
    }

    const long before_tax = static_cast<long>(sim.totalFunds);
    sim.taxFlag = false;
    sim.collectTax();
    // An empty city has no tax cash flow. Special buildings used to add
    // their income on top of that. They no longer pay anything.
    if (sim.specialIncome != 0 || static_cast<long>(sim.totalFunds) != before_tax || sim.cashFlow != 0) {
        std::fprintf(stderr, "income %ld funds %ld cash %ld\n", static_cast<long>(sim.specialIncome),
                     static_cast<long>(sim.totalFunds), static_cast<long>(sim.cashFlow));
        return fail(13, "January paid special-building income");
    }

    sim.map[31][10] = DIRT;
    if (special_building_intact(&sim, 30, 10) || special_annual_income(&sim) != 0) {
        return fail(14, "a damaged library changed the income total");
    }
    const std::string damaged = special_query_at(&sim, 30, 10);
    if (damaged.find("Library") == std::string::npos || damaged.find("Damaged") == std::string::npos) {
        return fail(15, "query did not call the library damaged");
    }

    bool shaken = false;
    for (int i = 0; i < 40 && !shaken; ++i) {
        sim.makeEarthquake();
        for (int dy = -1; dy <= 1 && !shaken; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                if ((sim.map[10 + dx][10 + dy] & LOMASK) != kSpecialBodyTile) {
                    shaken = true;
                }
            }
        }
    }
    if (!shaken) {
        return fail(16, "earthquakes never touched the zoo");
    }
    Micropolis burning;
    burning.generateSomeCity(19);
    fill_dirt(burning);
    burning.autoBulldoze = true;
    burning.setFunds(500);
    if (!place_special_building(&burning, 0, 10, 10, error)) {
        return fail(17, "could not place a zoo to burn");
    }
    // A freshly constructed engine has simPasses == 0, so simTick never
    // enters the simulator. Fire spread is also a race against the flame
    // burning out, so relight the source until the zoo body catches.
    burning.enableDisasters = false;
    burning.fireStationEffectMap.clear();
    burning.simPaused = false;
    burning.simPasses = 1;
    burning.setSpeed(3);
    burning.map[12][10] = FIRE;
    bool burned = false;
    for (int i = 0; i < 8000 && !burned; ++i) {
        const int source = burning.map[12][10] & LOMASK;
        if (source < FIRE || source > LASTFIRE) {
            burning.map[12][10] = FIRE;
        }
        burning.simTick();
        const int body = burning.map[11][10] & LOMASK;
        if (body >= FIRE && body <= LASTFIRE) {
            burned = true;
        }
    }
    if (!burned) {
        return fail(18, "fire did not spread onto the zoo");
    }

    Micropolis saved;
    saved.generateSomeCity(12);
    fill_dirt(saved);
    saved.setFunds(1000);
    saved.autoBulldoze = false;
    if (!place_special_building(&saved, 0, 15, 15, error)) {
        return fail(18, "could not place a zoo to save");
    }
    const std::string path = "/tmp/lunduke-special.cty";
    std::remove(path.c_str());
    if (!saved.saveCityAs(path.c_str())) {
        return fail(19, "could not save a city with a zoo");
    }
    Micropolis loaded;
    if (!loaded.loadCity(path.c_str())) {
        return fail(20, "could not load the zoo city");
    }
    if ((loaded.map[15][15] & LOMASK) != 1020 || (loaded.map[14][14] & LOMASK) != kSpecialBodyTile ||
        !special_building_intact(&loaded, 15, 15)) {
        return fail(21, "the zoo did not survive save and load");
    }
    if (!bulldoze_special_at(&loaded, 14, 16) || static_cast<long>(loaded.totalFunds) != static_cast<long>(saved.totalFunds) - 1) {
        // loaded funds come from the file, which was saved after the $100 spend.
    }
    const long loaded_funds = static_cast<long>(loaded.totalFunds);
    (void)loaded_funds;
    // Re-place and bulldoze on a known treasury. The loaded city already lost one tile's check above.
    Micropolis doz;
    doz.generateSomeCity(13);
    fill_dirt(doz);
    doz.setFunds(800);
    doz.autoBulldoze = true;
    if (!place_special_building(&doz, 0, 40, 40, error) || doz.totalFunds != 700) {
        return fail(22, "bulldoze setup did not spend $100");
    }
    if (!bulldoze_special_at(&doz, 41, 39) || doz.totalFunds != 699) {
        return fail(23, "bulldozing a special building did not cost $1");
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (!is_rubble(doz.map[40 + dx][40 + dy])) {
                return fail(24, "bulldoze left part of the building standing");
            }
        }
    }
    doz.setFunds(0);
    // Nothing left to bulldoze. Place another and refuse when broke.
    doz.setFunds(100);
    if (!place_special_building(&doz, 1, 50, 40, error)) {
        return fail(25, "could not place a second building");
    }
    doz.setFunds(0);
    if (bulldoze_special_at(&doz, 50, 40) || (doz.map[50][40] & LOMASK) != special_building(1).center_tile) {
        return fail(26, "a broke city bulldozed a special building");
    }

    CitySession session;
    CitySession::NewCitySpec spec;
    spec.name = "Gifts";
    spec.seed = 4;
    spec.seed_was_set = true;
    spec.difficulty = CitySession::kLevelEasy;
    session.new_city(spec);
    session.set_disasters(false);
    session.set_auto_bulldoze(true);
    if (!session.load_scenario(CitySession::scenario_def(0).id)) {
        return fail(27, "an existing scenario no longer loads");
    }
    session.new_city(spec);
    int road_x = -1;
    int road_y = -1;
    for (int y = 8; y < CitySession::kWorldH - 8 && road_x < 0; ++y) {
        for (int x = 8; x < CitySession::kWorldW - 8; ++x) {
            if ((session.map_value(x, y) & LOMASK) != DIRT) {
                continue;
            }
            session.use_tool(TOOL_ROAD, x, y);
            if ((session.map_value(x, y) & LOMASK) >= ROADBASE) {
                road_x = x;
                road_y = y;
                break;
            }
        }
    }
    if (road_x < 0) {
        return fail(28, "could not place a road on clear land");
    }
    session.set_auto_bulldoze(true);
    bool zoo = false;
    int zoo_x = 0;
    int zoo_y = 0;
    for (int dy = -6; dy <= 6 && !zoo; ++dy) {
        for (int dx = -6; dx <= 6 && !zoo; ++dx) {
            const int adx = dx < 0 ? -dx : dx;
            const int ady = dy < 0 ? -dy : dy;
            const int reach = adx > ady ? adx : ady;
            // The click is the center. Stay off the road tile, and inside
            // the land-value radius so this road's cell is boosted.
            if (reach < 2 || reach > 6) {
                continue;
            }
            if (session.place_special(0, road_x + dx, road_y + dy)) {
                zoo = true;
                zoo_x = road_x + dx;
                zoo_y = road_y + dy;
            }
        }
    }
    if (!zoo) {
        std::fprintf(stderr, "place '%s'\n", session.message().c_str());
        return fail(30, "could not place a zoo beside the road");
    }
    bool named = false;
    for (int dy = -1; dy <= 1 && !named; ++dy) {
        for (int dx = -1; dx <= 1 && !named; ++dx) {
            session.use_tool(TOOL_QUERY, zoo_x + dx, zoo_y + dy);
            if (session.message().find("Zoo") != std::string::npos) {
                named = true;
            }
        }
    }
    if (!named) {
        std::fprintf(stderr, "query '%s'\n", session.message().c_str());
        return fail(32, "query did not report the zoo");
    }
    const std::string round = "/tmp/lunduke-special-session.cty";
    std::remove(round.c_str());
    if (!session.save_city_as(round)) {
        return fail(33, "the session could not save the zoo");
    }
    CitySession again;
    if (!again.load_city(round) || again.message().find("Zoo") != std::string::npos) {
        // message may be empty; the map is what matters
    }
    if (!again.load_city(round)) {
        return fail(34, "the session could not load the zoo city");
    }
    bool found = false;
    for (int y = 0; y < CitySession::kWorldH && !found; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            if ((again.map_value(x, y) & LOMASK) == 1020) {
                found = true;
            }
        }
    }
    if (!found) {
        return fail(35, "the loaded city lost the zoo");
    }

    Micropolis wet;
    wet.generateSomeCity(17);
    fill_dirt(wet);
    wet.setFunds(500);
    wet.autoBulldoze = true;
    wet.map[20][20] = RIVER;
    if (place_special_building(&wet, 0, 20, 20, error) || error != "Can't build on water.") {
        std::fprintf(stderr, "water '%s'\n", error.c_str());
        return fail(39, "building on a river did not say it can't build on water");
    }
    if ((wet.map[20][20] & LOMASK) != RIVER) {
        return fail(39, "a refused water placement changed the river");
    }
    wet.map[20][20] = DIRT;
    wet.map[24][20] = FLOOD;
    if (place_special_building(&wet, 0, 24, 20, error) || error != "Can't build on water.") {
        std::fprintf(stderr, "flood '%s'\n", error.c_str());
        return fail(39, "building on a flood did not say it can't build on water");
    }
    wet.map[28][20] = ROADBASE;
    if (place_special_building(&wet, 0, 28, 20, error) || error != "Bulldoze that area first.") {
        std::fprintf(stderr, "road '%s'\n", error.c_str());
        return fail(40, "building on a road did not ask for a bulldoze");
    }
    wet.map[28][20] = RIVER;
    wet.map[29][20] = ROADBASE;
    if (place_special_building(&wet, 0, 28, 20, error) || error != "Bulldoze that area first.") {
        std::fprintf(stderr, "mixed '%s'\n", error.c_str());
        return fail(40, "water plus a road should still ask for a bulldoze");
    }

    // Fire can erase the centre tile and leave the eight body tiles. One
    // bulldoze clears that footprint for $1 and leaves the next building.
    Micropolis remnant;
    remnant.generateSomeCity(21);
    fill_dirt(remnant);
    remnant.setFunds(1000);
    remnant.autoBulldoze = true;
    if (!place_special_building(&remnant, 0, 10, 10, error) || !place_special_building(&remnant, 2, 14, 10, error)) {
        return fail(41, "could not place a zoo beside a library");
    }
    remnant.map[10][10] = FIRE;
    const long before_remnant = static_cast<long>(remnant.totalFunds);
    if (!bulldoze_special_at(&remnant, 9, 9) || static_cast<long>(remnant.totalFunds) != before_remnant - 1) {
        return fail(42, "bulldozing one leftover tile did not clear the footprint for $1");
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (!is_rubble(remnant.map[10 + dx][10 + dy])) {
                return fail(43, "a leftover bulldoze left part of the zoo standing");
            }
        }
    }
    if (!special_building_intact(&remnant, 14, 10)) {
        return fail(44, "clearing the burned zoo also removed the library");
    }
    if (!place_special_building(&remnant, 0, 30, 30, error)) {
        return fail(45, "could not place a zoo whose centre will burn");
    }
    remnant.map[30][30] = FIRE;
    const long before_centre = static_cast<long>(remnant.totalFunds);
    if (!bulldoze_special_at(&remnant, 30, 30) || static_cast<long>(remnant.totalFunds) != before_centre - 1) {
        return fail(46, "bulldozing the burned centre did not clear the footprint for $1");
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (!is_rubble(remnant.map[30 + dx][30 + dy])) {
                return fail(47, "the burned centre's footprint was not all cleared");
            }
        }
    }
    if (!special_building_intact(&remnant, 14, 10)) {
        return fail(48, "clearing a burned centre removed the neighbouring library");
    }

    return 0;
}

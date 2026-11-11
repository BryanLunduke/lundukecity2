// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Prison, Marina, and Fountain. Costs, footprints, shoreline, power and
// road, land value, crime, the park fountain animation, and save/load.

#include "placement_hint.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

#ifndef LUNDUKE_CITY_ASSET_DIR
#define LUNDUKE_CITY_ASSET_DIR "third_party/micropolis-assets"
#endif

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

void arm(Micropolis &sim, int seed)
{
    sim.generateSomeCity(seed);
    fill_dirt(sim);
    sim.autoBulldoze = true;
    sim.enableDisasters = false;
    sim.setFunds(50000);
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

int word_at(const Micropolis &sim, int x, int y)
{
    if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H) {
        return -1;
    }
    return static_cast<int>(sim.map[x][y]);
}

} // namespace

int main()
{
    if (std::string(special_building(0).name) != "Zoo" || special_building(1).name != std::string("Big Park") ||
        special_building(2).name != std::string("Library")) {
        return fail(1, "the original specials are no longer the first three rows");
    }
    const int prison = index_named("Prison");
    const int marina = index_named("Marina");
    const int fountain = index_named("Fountain");
    const int zoo = index_named("Zoo");
    if (prison < 0 || marina < 0 || fountain < 0 || zoo < 0) {
        return fail(2, "prison, marina, or fountain is missing from the table");
    }
    if (index_named("Statue") >= 0 || index_named("Bus Station") >= 0 || index_named("Bus station") >= 0) {
        return fail(3, "a dropped building is in the table");
    }
    const SpecialBuilding &prison_row = special_building(prison);
    const SpecialBuilding &marina_row = special_building(marina);
    const SpecialBuilding &fountain_row = special_building(fountain);
    if (prison_row.cost != 3000 || prison_row.annual_income != 0 || prison_row.width != 4 || prison_row.height != 4 ||
        prison_row.anchor_x != 1 || prison_row.anchor_y != 1 || prison_row.center_tile != SPECIAL_PRISON ||
        !prison_row.needs_road || !prison_row.needs_power || prison_row.land_bonus != -48 || prison_row.crime_bonus != -24) {
        return fail(4, "prison cost, footprint, or effect does not match the first pass");
    }
    if (marina_row.cost != 1000 || marina_row.annual_income != 0 || marina_row.width != 3 || !marina_row.needs_south_water ||
        marina_row.land_bonus != special_building(zoo).land_bonus || marina_row.center_tile != SPECIAL_MARINA) {
        return fail(5, "marina cost, shoreline rule, or land value does not match the first pass");
    }
    if (fountain_row.cost != 20 || fountain_row.annual_income != 0 || fountain_row.width != 1 || fountain_row.height != 1 ||
        fountain_row.anchor_x != 0 || fountain_row.anchor_y != 0 || fountain_row.center_tile != SPECIAL_FOUNTAIN ||
        fountain_row.center_tile == FOUNTAIN || fountain_row.land_bonus != 16) {
        return fail(6, "fountain is not a $20 one-tile special on its own id");
    }
    if (std::string(prison_row.icon) != "prison" || std::string(marina_row.icon) != "marina" ||
        std::string(fountain_row.icon) != "fountain" || std::string(special_building(1).icon) != "bigpark") {
        return fail(7, "a toolbar stem gained a hyphen or Big Park's stem changed");
    }

    std::string error;
    Micropolis bare;
    arm(bare, 4);
    if (place_special_building(&bare, prison, 20, 20, error) || error.find("power and a road") == std::string::npos) {
        std::fprintf(stderr, "bare '%s'\n", error.c_str());
        return fail(8, "a prison with no road and no power was accepted");
    }
    if ((bare.map[20][20] & LOMASK) != DIRT || bare.totalFunds != 50000) {
        return fail(9, "a refused prison changed the map or the funds");
    }
    bare.map[20][18] = ROADBASE | BULLBIT | BURNBIT;
    if (place_special_building(&bare, prison, 20, 20, error) || error.find("Needs power") == std::string::npos) {
        std::fprintf(stderr, "road only '%s'\n", error.c_str());
        return fail(10, "a prison with a road but no power was accepted");
    }
    bare.map[20][18] = DIRT;
    bare.map[18][20] = HPOWER | CONDBIT | BULLBIT;
    if (place_special_building(&bare, prison, 20, 20, error) || error.find("Needs a road") == std::string::npos) {
        std::fprintf(stderr, "wire only '%s'\n", error.c_str());
        return fail(11, "a prison with power but no road was accepted");
    }
    bare.map[20][18] = ROADBASE | BULLBIT | BURNBIT;
    if (!place_special_building(&bare, prison, 20, 20, error) || bare.totalFunds != 47000) {
        std::fprintf(stderr, "place '%s' funds %ld\n", error.c_str(), static_cast<long>(bare.totalFunds));
        return fail(12, "a prison next to a road and a wire did not cost $3000");
    }
    if ((bare.map[20][20] & LOMASK) != SPECIAL_PRISON || (bare.map[20][20] & ZONEBIT) == 0 ||
        (bare.map[20][20] & CONDBIT) == 0 || (bare.map[19][19] & LOMASK) != kSpecialBodyTile ||
        (bare.map[22][22] & LOMASK) != kSpecialBodyTile || (bare.map[19][19] & ZONEBIT) != 0) {
        return fail(13, "the prison is not a 4x4 with the center on the click");
    }
    if (!special_building_intact(&bare, 20, 20) || special_annual_income(&bare) != 0) {
        return fail(14, "the prison did not stay intact or it pays a yearly bill");
    }
    const std::string prison_query = special_query_at(&bare, 22, 22);
    if (prison_query.find("Prison") == std::string::npos || prison_query.find("$3000") == std::string::npos) {
        std::fprintf(stderr, "query '%s'\n", prison_query.c_str());
        return fail(15, "query did not name the prison");
    }

    Micropolis shore;
    arm(shore, 5);
    if (place_special_building(&shore, marina, 30, 30, error) || error.find("shoreline") == std::string::npos) {
        std::fprintf(stderr, "dry '%s'\n", error.c_str());
        return fail(16, "a marina away from water was accepted");
    }
    shore.map[30][32] = DIRT;
    // Middle row of the 3x3. The dock row may cover water; this row may not.
    shore.map[29][30] = RIVER;
    if (place_special_building(&shore, marina, 30, 30, error) || error != "Can't build on water.") {
        std::fprintf(stderr, "on water '%s'\n", error.c_str());
        return fail(17, "a marina standing on water was accepted");
    }
    if ((shore.map[29][30] & LOMASK) != RIVER) {
        return fail(17, "a refused marina covered the water");
    }
    shore.map[29][30] = DIRT;
    shore.map[30][32] = RIVER;
    if (!place_special_building(&shore, marina, 30, 30, error) || shore.totalFunds != 49000) {
        std::fprintf(stderr, "marina '%s'\n", error.c_str());
        return fail(18, "a marina with water to the south did not cost $1000");
    }
    if ((shore.map[30][30] & LOMASK) != SPECIAL_MARINA || (shore.map[29][29] & LOMASK) != kSpecialBodyTile ||
        (shore.map[30][32] & LOMASK) != RIVER || !special_building_intact(&shore, 30, 30)) {
        return fail(19, "the marina footprint covered the water or lost its center");
    }
    const std::string marina_query = special_query_at(&shore, 31, 29);
    if (marina_query.find("Marina") == std::string::npos || marina_query.find("$1000") == std::string::npos) {
        return fail(20, "query did not name the marina");
    }

    Micropolis ornament;
    arm(ornament, 6);
    if (!place_special_building(&ornament, fountain, 0, 0, error) || ornament.totalFunds != 49980) {
        std::fprintf(stderr, "corner '%s'\n", error.c_str());
        return fail(21, "a fountain could not be placed on the corner for $20");
    }
    if ((ornament.map[0][0] & LOMASK) != SPECIAL_FOUNTAIN || (ornament.map[0][0] & ZONEBIT) == 0 ||
        (ornament.map[0][0] & LOMASK) == FOUNTAIN) {
        return fail(22, "the corner fountain used the park animation tile");
    }
    if (!place_special_building(&ornament, fountain, 8, 8, error) || !special_building_intact(&ornament, 8, 8)) {
        return fail(23, "a second fountain was refused");
    }
    const std::string fountain_query = special_query_at(&ornament, 8, 8);
    if (fountain_query.find("Fountain") == std::string::npos || fountain_query.find("$20") == std::string::npos) {
        return fail(24, "query did not name the fountain");
    }

    auto at = [](const Micropolis &sim) {
        return [&sim](int x, int y) { return word_at(sim, x, y); };
    };
    Micropolis hint;
    arm(hint, 7);
    hint.map[40][42] = RIVER;
    const PlaceForecast marina_hint = forecast_tile(kSpecialEngineId, 40, 40, marina, 50000, true, at(hint));
    if (marina_hint.mark != PlaceMark::Ready || marina_hint.cost != 1000 || marina_hint.width != 3 ||
        marina_hint.left != 39 || marina_hint.top != 39 || marina_hint.tag.find("$1,000") == std::string::npos) {
        std::fprintf(stderr, "hint '%s' cost %d %dx%d at %d,%d\n", marina_hint.tag.c_str(), marina_hint.cost,
                     marina_hint.width, marina_hint.height, marina_hint.left, marina_hint.top);
        return fail(25, "the marina hover did not show a 3x3 at $1,000");
    }
    hint.map[40][42] = DIRT;
    const PlaceForecast dry_hint = forecast_tile(kSpecialEngineId, 40, 40, marina, 50000, true, at(hint));
    if (dry_hint.mark != PlaceMark::Blocked || dry_hint.tag.find("shoreline") == std::string::npos) {
        std::fprintf(stderr, "dry hint '%s'\n", dry_hint.tag.c_str());
        return fail(26, "the marina hover did not ask for a south shoreline");
    }
    const PlaceForecast fountain_hint = forecast_tile(kSpecialEngineId, 3, 3, fountain, 50000, true, at(hint));
    if (fountain_hint.mark != PlaceMark::Ready || fountain_hint.width != 1 || fountain_hint.left != 3 ||
        fountain_hint.cost != 20 || fountain_hint.tag.find("$20") == std::string::npos) {
        std::fprintf(stderr, "fountain hint '%s'\n", fountain_hint.tag.c_str());
        return fail(27, "the fountain hover is not a $20 single tile");
    }
    hint.map[12][10] = ROADBASE | BULLBIT;
    hint.map[10][12] = HPOWER | CONDBIT | BULLBIT;
    const PlaceForecast prison_hint = forecast_tile(kSpecialEngineId, 12, 12, prison, 50000, true, at(hint));
    if (prison_hint.mark != PlaceMark::Ready || prison_hint.width != 4 || prison_hint.height != 4 ||
        prison_hint.left != 11 || prison_hint.top != 11 || prison_hint.cost != 3000 ||
        prison_hint.tag.find("$3,000") == std::string::npos) {
        std::fprintf(stderr, "prison hint '%s' %d at %d,%d %dx%d\n", prison_hint.tag.c_str(), prison_hint.cost,
                     prison_hint.left, prison_hint.top, prison_hint.width, prison_hint.height);
        return fail(28, "the prison hover is not a 4x4 at $3,000");
    }

    Micropolis land;
    arm(land, 8);
    land.specialLandFn = boost_special_land_value;
    if (!place_special_building(&land, zoo, 10, 10, error)) {
        return fail(29, "could not place a zoo for the land-value check");
    }
    land.map[20][8] = ROADBASE | BULLBIT;
    land.map[18][10] = HPOWER | CONDBIT | BULLBIT;
    if (!place_special_building(&land, prison, 20, 10, error)) {
        return fail(30, "could not place a prison for the land-value check");
    }
    land.map[30][12] = RIVER;
    if (!place_special_building(&land, marina, 30, 10, error)) {
        return fail(31, "could not place a marina for the land-value check");
    }
    if (!place_special_building(&land, fountain, 50, 10, error)) {
        return fail(32, "could not place a fountain for the land-value check");
    }
    land.landValueMap.clear();
    const int block = land.landValueMap.MAP_BLOCKSIZE;
    const int zoo_x = 10 / block;
    const int zoo_y = 10 / block;
    const int prison_x = 20 / block;
    const int prison_y = 10 / block;
    const int marina_x = 30 / block;
    const int marina_y = 10 / block;
    const int fountain_x = 50 / block;
    const int fountain_y = 10 / block;
    const int open_x = 80 / block;
    const int open_y = 80 / block;
    land.landValueMap.set(zoo_x, zoo_y, 100);
    land.landValueMap.set(prison_x, prison_y, 100);
    land.landValueMap.set(marina_x, marina_y, 100);
    land.landValueMap.set(fountain_x, fountain_y, 100);
    land.landValueMap.set(open_x, open_y, 40);
    land.landValueMap.set(prison_x, prison_y + 8, 10);
    boost_special_land_value(&land);
    if (land.landValueMap.get(zoo_x, zoo_y) != 148 || land.landValueMap.get(marina_x, marina_y) != 148) {
        std::fprintf(stderr, "zoo %d marina %d\n", land.landValueMap.get(zoo_x, zoo_y),
                     land.landValueMap.get(marina_x, marina_y));
        return fail(33, "the marina land bonus is not the zoo bonus");
    }
    if (land.landValueMap.get(fountain_x, fountain_y) != 116 || land.landValueMap.get(open_x, open_y) != 40) {
        std::fprintf(stderr, "fountain %d open %d\n", land.landValueMap.get(fountain_x, fountain_y),
                     land.landValueMap.get(open_x, open_y));
        return fail(34, "the fountain did not add 16, or it reached a distant cell");
    }
    if (land.landValueMap.get(prison_x, prison_y) != 52) {
        std::fprintf(stderr, "prison land %d\n", land.landValueMap.get(prison_x, prison_y));
        return fail(35, "the prison did not lower developed land value by 48");
    }
    land.landValueMap.set(prison_x, prison_y, 10);
    land.landValueMap.set(open_x, open_y, 0);
    boost_special_land_value(&land);
    if (land.landValueMap.get(prison_x, prison_y) != 1 || land.landValueMap.get(open_x, open_y) != 0) {
        return fail(36, "the prison land drop did not stop at 1, or it touched empty land");
    }

    Micropolis crime;
    arm(crime, 9);
    crime.map[20][18] = ROADBASE | BULLBIT;
    crime.map[18][20] = HPOWER | CONDBIT | BULLBIT;
    if (!place_special_building(&crime, prison, 20, 20, error)) {
        return fail(37, "could not place a prison for the crime check");
    }
    crime.crimeRateMap.clear();
    crime.crimeRateMap.worldSet(20, 20, 80);
    crime.specialCrimeFn = boost_special_crime;
    boost_special_crime(&crime);
    if (crime.crimeRateMap.worldGet(20, 20) != 80) {
        std::fprintf(stderr, "unpowered %d\n", crime.crimeRateMap.worldGet(20, 20));
        return fail(38, "an unpowered prison reduced crime");
    }
    crime.map[20][20] |= PWRBIT;
    boost_special_crime(&crime);
    if (crime.crimeRateMap.worldGet(20, 20) != 56) {
        std::fprintf(stderr, "powered %d\n", crime.crimeRateMap.worldGet(20, 20));
        return fail(39, "a powered prison on a road did not reduce crime by 24");
    }
    crime.map[20][18] = DIRT;
    crime.crimeRateMap.worldSet(20, 20, 80);
    boost_special_crime(&crime);
    if (crime.crimeRateMap.worldGet(20, 20) != 80) {
        return fail(40, "a prison with no road still reduced crime");
    }

    const long before_dozer = static_cast<long>(bare.totalFunds);
    if (!bulldoze_special_at(&bare, 22, 19) || static_cast<long>(bare.totalFunds) != before_dozer - 1) {
        return fail(41, "bulldozing the prison did not cost $1");
    }
    for (int y = 19; y <= 22; ++y) {
        for (int x = 19; x <= 22; ++x) {
            if ((bare.map[x][y] & LOMASK) != RUBBLE) {
                return fail(42, "bulldoze left part of the prison standing");
            }
        }
    }
    if ((bare.map[20][18] & LOMASK) != ROADBASE || (bare.map[18][20] & LOMASK) != HPOWER) {
        return fail(43, "bulldozing the prison also removed the road or the wire");
    }

    Micropolis burned;
    arm(burned, 10);
    burned.map[10][8] = ROADBASE | BULLBIT;
    burned.map[8][10] = HPOWER | CONDBIT | BULLBIT;
    if (!place_special_building(&burned, prison, 10, 10, error) || !place_special_building(&burned, zoo, 18, 10, error)) {
        return fail(44, "could not place a prison beside a zoo");
    }
    burned.map[10][10] = FIRE;
    if (!bulldoze_special_at(&burned, 9, 9) || !special_building_intact(&burned, 18, 10)) {
        return fail(45, "clearing a burned prison also removed the zoo");
    }
    for (int y = 9; y <= 12; ++y) {
        for (int x = 9; x <= 12; ++x) {
            if ((burned.map[x][y] & LOMASK) != RUBBLE) {
                return fail(46, "the burned prison footprint was not cleared");
            }
        }
    }

    Micropolis grown;
    arm(grown, 11);
    grown.map[14][12] = ROADBASE | BULLBIT;
    grown.map[12][14] = HPOWER | CONDBIT | BULLBIT;
    grown.map[24][16] = RIVER;
    if (!place_special_building(&grown, prison, 14, 14, error) || !place_special_building(&grown, marina, 24, 14, error) ||
        !place_special_building(&grown, fountain, 40, 14, error)) {
        return fail(47, "could not place the three buildings for the simulation check");
    }
    grown.simPaused = false;
    grown.simPasses = 1;
    grown.setSpeed(3);
    for (int i = 0; i < 400; ++i) {
        grown.simTick();
    }
    if ((grown.map[14][14] & LOMASK) != SPECIAL_PRISON || (grown.map[24][24] & LOMASK) == SPECIAL_MARINA ||
        (grown.map[24][14] & LOMASK) != SPECIAL_MARINA || (grown.map[40][14] & LOMASK) != SPECIAL_FOUNTAIN) {
        return fail(48, "the simulator replaced a prison, marina, or fountain");
    }

    Micropolis classic;
    classic.resourceDir = std::string(LUNDUKE_CITY_ASSET_DIR) + "/res";
    if (!classic.loadScenario(SC_DULLSVILLE)) {
        std::fprintf(stderr, "scenario '%s'\n", classic.loadErrorDetail.c_str());
        return fail(49, "an old scenario city no longer loads");
    }
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            const int tile = classic.map[x][y] & LOMASK;
            if (tile == SPECIAL_PRISON || tile == SPECIAL_MARINA || tile == SPECIAL_FOUNTAIN) {
                return fail(50, "Dullsville already contained a new special");
            }
        }
    }

    const std::string saved_path = "/tmp/lunduke-new-specials.cty";
    std::remove(saved_path.c_str());
    if (!ornament.saveCityAs(saved_path.c_str())) {
        return fail(51, "could not save a city with a fountain");
    }
    Micropolis loaded;
    if (!loaded.loadCity(saved_path.c_str())) {
        std::fprintf(stderr, "load '%s'\n", loaded.loadErrorDetail.c_str());
        return fail(52, "could not load the fountain city");
    }
    if ((loaded.map[0][0] & LOMASK) != SPECIAL_FOUNTAIN || (loaded.map[0][0] & ZONEBIT) == 0 ||
        !special_building_intact(&loaded, 8, 8)) {
        return fail(53, "the fountain did not survive save and load, including the map corner");
    }

    Micropolis crowd;
    arm(crowd, 12);
    if (!place_special_building(&crowd, zoo, 10, 10, error)) {
        return fail(54, "could not place a zoo in the crowded city");
    }
    int planted = 0;
    for (int y = 0; y < WORLD_H && planted < 1400; ++y) {
        for (int x = 0; x < WORLD_W && planted < 1400; ++x) {
            if ((crowd.map[x][y] & LOMASK) != DIRT) {
                continue;
            }
            crowd.map[x][y] = static_cast<unsigned short>(SPECIAL_FOUNTAIN | ZONEBIT | BURNBIT | BULLBIT);
            ++planted;
        }
    }
    if (planted < 1400) {
        return fail(55, "could not plant enough fountains to pass the old centre limit");
    }
    const std::string crowd_path = "/tmp/lunduke-fountain-crowd.cty";
    std::remove(crowd_path.c_str());
    if (!crowd.saveCityAs(crowd_path.c_str())) {
        return fail(56, "a city of fountains could not be saved");
    }
    Micropolis crowd_loaded;
    if (!crowd_loaded.loadCity(crowd_path.c_str())) {
        std::fprintf(stderr, "crowd '%s'\n", crowd_loaded.loadErrorDetail.c_str());
        return fail(57, "a city with more than one centre per nine tiles of fountains was rejected");
    }
    if (!special_building_intact(&crowd_loaded, 10, 10)) {
        return fail(58, "the zoo in the fountain city did not load");
    }

    Micropolis parks;
    arm(parks, 13);
    bool saw_fountain = false;
    for (int i = 0; i < 80 && !saw_fountain; ++i) {
        parks.toolDown(TOOL_PARK, static_cast<short>(i + 1), 4);
        if ((parks.map[i + 1][4] & LOMASK) == FOUNTAIN) {
            saw_fountain = true;
        }
    }
    if (!saw_fountain) {
        return fail(59, "the park tool no longer plants the animated fountain");
    }
    int park_x = -1;
    for (int i = 0; i < 80; ++i) {
        if ((parks.map[i + 1][4] & LOMASK) == FOUNTAIN) {
            park_x = i + 1;
            parks.map[park_x][4] |= ANIMBIT;
            break;
        }
    }
    parks.animateTiles();
    if (park_x < 0 || (parks.map[park_x][4] & LOMASK) != 841) {
        return fail(60, "the park fountain did not animate from 840 to 841");
    }
    if (!place_special_building(&parks, fountain, 6, 8, error)) {
        return fail(61, "could not place a fountain beside the park animation");
    }
    parks.map[6][8] |= ANIMBIT;
    parks.animateTiles();
    if ((parks.map[6][8] & LOMASK) != SPECIAL_FOUNTAIN || (parks.map[park_x][4] & LOMASK) == SPECIAL_FOUNTAIN) {
        return fail(62, "the placeable fountain stole the park animation");
    }

    Micropolis powered;
    arm(powered, 14);
    if (powered.toolDown(TOOL_COALPOWER, 8, 8) != TOOLRESULT_OK) {
        return fail(63, "could not place a coal plant");
    }
    for (int x = 11; x <= 14; ++x) {
        if (powered.toolDown(TOOL_WIRE, static_cast<short>(x), 8) != TOOLRESULT_OK) {
            return fail(64, "could not run a wire from the plant");
        }
    }
    for (int x = 7; x <= 18; ++x) {
        if (powered.toolDown(TOOL_ROAD, static_cast<short>(x), 6) != TOOLRESULT_OK) {
            std::fprintf(stderr, "road %d\n", x);
            return fail(65, "could not run a road along the prison");
        }
    }
    if (!place_special_building(&powered, prison, 16, 8, error)) {
        std::fprintf(stderr, "powered place '%s'\n", error.c_str());
        return fail(66, "the prison refused a road and a wire that reach a plant");
    }
    powered.simPaused = false;
    powered.simPasses = 1;
    powered.setSpeed(3);
    powered.enableDisasters = false;
    bool lit = false;
    for (int i = 0; i < 2500 && !lit; ++i) {
        powered.simTick();
        if ((powered.map[16][8] & PWRBIT) != 0 && (powered.map[16][8] & LOMASK) == SPECIAL_PRISON) {
            lit = true;
        }
    }
    if (!lit) {
        return fail(67, "the power grid did not reach the prison");
    }

    // Dock row may cover shoreline. Open water has to remain south of the
    // 3x3. Land in that same row stays land, and bulldoze puts each covered
    // shoreline tile back, including after save and load.
    Micropolis harbor;
    arm(harbor, 15);
    harbor.map[29][31] = REDGE;
    harbor.map[31][31] = FIRSTRIVEDGE;
    if (place_special_building(&harbor, marina, 30, 30, error) || error.find("shoreline") == std::string::npos) {
        std::fprintf(stderr, "only dock '%s'\n", error.c_str());
        return fail(68, "a marina that covered the only water was accepted");
    }
    if ((harbor.map[29][31] & LOMASK) != REDGE || harbor.totalFunds != 50000) {
        return fail(69, "a refused dock-row marina changed the shoreline");
    }
    harbor.map[30][32] = CHANNEL;
    if (!place_special_building(&harbor, marina, 30, 30, error) || harbor.totalFunds != 49000) {
        std::fprintf(stderr, "harbor '%s'\n", error.c_str());
        return fail(70, "a marina whose dock row covers shoreline was refused");
    }
    if ((harbor.map[30][30] & LOMASK) != SPECIAL_MARINA || (harbor.map[29][29] & LOMASK) != kSpecialBodyTile ||
        (harbor.map[29][31] & LOMASK) != kSpecialBodyTile || (harbor.map[30][31] & LOMASK) != kSpecialBodyTile ||
        (harbor.map[31][31] & LOMASK) != kSpecialBodyTile || (harbor.map[30][32] & LOMASK) != CHANNEL ||
        !special_building_intact(&harbor, 30, 30)) {
        return fail(71, "the dock row did not cover the shoreline or it ate the open water");
    }
    const std::string harbor_path = "/tmp/lunduke-marina-dock.cty";
    std::remove(harbor_path.c_str());
    if (!harbor.saveCityAs(harbor_path.c_str())) {
        return fail(72, "could not save a marina that covers shoreline");
    }
    {
        FILE *saved = std::fopen(harbor_path.c_str(), "rb");
        if (saved == nullptr) {
            return fail(73, "could not read the marina save back");
        }
        std::string bytes;
        char buf[4096];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), saved)) > 0) {
            bytes.append(buf, n);
        }
        std::fclose(saved);
        if (bytes.find("LCM1") == std::string::npos) {
            return fail(74, "the save did not keep the covered shoreline");
        }
    }
    Micropolis loaded_harbor;
    if (!loaded_harbor.loadCity(harbor_path.c_str())) {
        std::fprintf(stderr, "harbor load '%s'\n", loaded_harbor.loadErrorDetail.c_str());
        return fail(75, "could not load the marina that covers shoreline");
    }
    if ((loaded_harbor.map[29][31] & LOMASK) != kSpecialBodyTile ||
        (loaded_harbor.map[30][32] & LOMASK) != CHANNEL) {
        return fail(76, "the loaded marina lost its dock or the open water");
    }
    const long harbor_funds = static_cast<long>(loaded_harbor.totalFunds);
    if (!bulldoze_special_at(&loaded_harbor, 30, 31) ||
        static_cast<long>(loaded_harbor.totalFunds) != harbor_funds - 1) {
        return fail(77, "bulldozing the loaded marina did not cost $1");
    }
    if (loaded_harbor.map[29][31] != REDGE || loaded_harbor.map[31][31] != FIRSTRIVEDGE ||
        (loaded_harbor.map[30][31] & LOMASK) != RUBBLE || (loaded_harbor.map[29][29] & LOMASK) != RUBBLE ||
        (loaded_harbor.map[30][32] & LOMASK) != CHANNEL) {
        std::fprintf(stderr, "restored %u %u mid %u\n", loaded_harbor.map[29][31], loaded_harbor.map[31][31],
                     loaded_harbor.map[30][31]);
        return fail(78, "bulldoze did not put the original shoreline back");
    }
    if (bulldoze_special_at(&loaded_harbor, 29, 31)) {
        return fail(79, "a second bulldoze treated the restored shoreline as a marina");
    }
    if (loaded_harbor.map[29][31] != REDGE) {
        return fail(80, "bulldozing the restored shore changed the tile");
    }

    Micropolis dock_hint;
    arm(dock_hint, 16);
    dock_hint.map[29][31] = REDGE;
    dock_hint.map[30][32] = RIVER;
    const PlaceForecast dock_ok = forecast_tile(kSpecialEngineId, 30, 30, marina, 50000, true, at(dock_hint));
    if (dock_ok.mark != PlaceMark::Ready || dock_ok.cost != 1000 || dock_ok.width != 3) {
        std::fprintf(stderr, "dock hint '%s'\n", dock_ok.tag.c_str());
        return fail(81, "the hover refused a dock row on shoreline with water beyond");
    }
    dock_hint.map[30][32] = DIRT;
    const PlaceForecast dock_dry = forecast_tile(kSpecialEngineId, 30, 30, marina, 50000, true, at(dock_hint));
    if (dock_dry.mark != PlaceMark::Blocked || dock_dry.tag.find("shoreline") == std::string::npos) {
        std::fprintf(stderr, "dock dry '%s'\n", dock_dry.tag.c_str());
        return fail(82, "the hover allowed a marina that would consume the only water");
    }
    dock_hint.map[30][30] = RIVER;
    dock_hint.map[30][32] = RIVER;
    const PlaceForecast dock_north = forecast_tile(kSpecialEngineId, 30, 30, marina, 50000, true, at(dock_hint));
    if (dock_north.mark != PlaceMark::Blocked || dock_north.tag.find("water") == std::string::npos ||
        dock_north.tag.find("shoreline") != std::string::npos) {
        std::fprintf(stderr, "dock north '%s'\n", dock_north.tag.c_str());
        return fail(83, "the hover allowed water under the marina building row");
    }

    // Tiles 53-55 sit under the first road tile. The hover must not offer
    // to pave them, and it must still offer to clear a tree.
    Micropolis hover;
    arm(hover, 21);
    hover.map[12][12] = static_cast<unsigned short>(SPECIAL_MAYOR | ZONEBIT | BULLBIT | BURNBIT);
    hover.map[14][14] = static_cast<unsigned short>(SPECIAL_CASINO | ZONEBIT | BULLBIT | BURNBIT);
    hover.map[16][16] = static_cast<unsigned short>(SPECIAL_AMUSEMENT | ZONEBIT | BULLBIT | BURNBIT);
    hover.map[18][18] = static_cast<unsigned short>(SPECIAL_PRISON | ZONEBIT | BULLBIT | BURNBIT);
    hover.map[20][20] = static_cast<unsigned short>(WOODS | BULLBIT | BURNBIT);
    const int line_tools[] = {TOOL_ROAD, TOOL_RAILROAD, TOOL_WIRE};
    const int blocked_at[][2] = {{12, 12}, {14, 14}, {16, 16}, {18, 18}};
    for (int tool : line_tools) {
        for (const auto &cell : blocked_at) {
            const PlaceForecast hint = forecast_tile(tool, cell[0], cell[1], -1, 50000, true, at(hover));
            if (hint.mark == PlaceMark::Ready || hint.mark == PlaceMark::Caution) {
                std::fprintf(stderr, "hover tool %d at %d,%d '%s'\n", tool, cell[0], cell[1], hint.tag.c_str());
                return fail(84, "the hover would pave a civic centre or the prison");
            }
        }
    }
    const PlaceForecast tree_hint = forecast_tile(TOOL_ROAD, 20, 20, -1, 50000, true, at(hover));
    if (tree_hint.mark != PlaceMark::Ready || tree_hint.cost != 11) {
        std::fprintf(stderr, "tree hover '%s' cost %d\n", tree_hint.tag.c_str(), tree_hint.cost);
        return fail(85, "the hover stopped auto-bulldozing a tree");
    }

    // A crash turns the dock into an explosion. The shoreline note has to
    // survive that frame and a save taken while it is still playing. If
    // the whole footprint is gone, bulldoze still puts the shore back.
    Micropolis blown;
    arm(blown, 22);
    if (!place_special_building(&blown, zoo, 27, 30, error)) {
        return fail(86, "could not place a zoo beside the marina");
    }
    blown.map[29][31] = REDGE;
    blown.map[31][31] = FIRSTRIVEDGE;
    blown.map[30][32] = CHANNEL;
    if (!place_special_building(&blown, marina, 30, 30, error) || blown.marinaDock.size() != 2) {
        std::fprintf(stderr, "blown '%s' notes %zu\n", error.c_str(), blown.marinaDock.size());
        return fail(87, "the explosion setup did not cover two shoreline tiles");
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int frame = (dx == -1 && dy == 1) ? TINYEXP : LASTTINYEXP;
            blown.map[30 + dx][30 + dy] = static_cast<unsigned short>(frame | BULLBIT | ANIMBIT);
        }
    }
    const std::string blown_path = "/tmp/lunduke-marina-explosion.cty";
    std::remove(blown_path.c_str());
    if (!blown.saveCityAs(blown_path.c_str())) {
        return fail(88, "could not save a marina while its dock was an explosion");
    }
    if (blown.marinaDock.size() != 2) {
        std::fprintf(stderr, "notes after save %zu\n", blown.marinaDock.size());
        return fail(89, "saving pruned the shoreline notes while the explosion was still playing");
    }
    const long blown_funds = static_cast<long>(blown.totalFunds);
    if (!bulldoze_special_at(&blown, 30, 30) || static_cast<long>(blown.totalFunds) != blown_funds - 1) {
        return fail(90, "bulldozing an exploded marina did not cost $1");
    }
    if (blown.map[29][31] != REDGE || blown.map[31][31] != FIRSTRIVEDGE || (blown.map[30][31] & LOMASK) != RUBBLE ||
        (blown.map[30][32] & LOMASK) != CHANNEL || !special_building_intact(&blown, 27, 30)) {
        std::fprintf(stderr, "shore %u %u mid %u zoo %u\n", blown.map[29][31], blown.map[31][31], blown.map[30][31],
                     blown.map[27][30]);
        return fail(91, "bulldozing the exploded marina did not put the shoreline back");
    }
    Micropolis loaded_blown;
    if (!loaded_blown.loadCity(blown_path.c_str())) {
        std::fprintf(stderr, "blown load '%s'\n", loaded_blown.loadErrorDetail.c_str());
        return fail(92, "could not load the marina saved during the explosion");
    }
    if (loaded_blown.marinaDock.size() != 2) {
        std::fprintf(stderr, "loaded notes %zu\n", loaded_blown.marinaDock.size());
        return fail(93, "the save dropped the shoreline record");
    }
    const long loaded_blown_funds = static_cast<long>(loaded_blown.totalFunds);
    if (!bulldoze_special_at(&loaded_blown, 29, 31) ||
        static_cast<long>(loaded_blown.totalFunds) != loaded_blown_funds - 1 ||
        loaded_blown.map[29][31] != REDGE || loaded_blown.map[31][31] != FIRSTRIVEDGE ||
        (loaded_blown.map[30][32] & LOMASK) != CHANNEL) {
        std::fprintf(stderr, "loaded shore %u %u\n", loaded_blown.map[29][31], loaded_blown.map[31][31]);
        return fail(94, "bulldozing the loaded explosion did not restore the shoreline");
    }

    // After the explosion animation the cells are rubble and every body
    // tile is gone. Bulldozing a corner of that lot still restores the shore.
    Micropolis rubble_dock;
    arm(rubble_dock, 23);
    rubble_dock.map[29][31] = REDGE;
    rubble_dock.map[31][31] = FIRSTRIVEDGE;
    rubble_dock.map[30][32] = CHANNEL;
    if (!place_special_building(&rubble_dock, marina, 30, 30, error)) {
        return fail(95, "could not place a marina that will be reduced to rubble");
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            rubble_dock.map[30 + dx][30 + dy] = static_cast<unsigned short>(RUBBLE | BULLBIT);
        }
    }
    rubble_dock.setFunds(0);
    if (bulldoze_special_at(&rubble_dock, 29, 29) || rubble_dock.map[29][31] != (RUBBLE | BULLBIT)) {
        return fail(96, "a broke city restored a destroyed marina");
    }
    rubble_dock.setFunds(50);
    if (!bulldoze_special_at(&rubble_dock, 29, 29) || rubble_dock.map[29][31] != REDGE ||
        rubble_dock.map[31][31] != FIRSTRIVEDGE || (rubble_dock.map[30][32] & LOMASK) != CHANNEL ||
        rubble_dock.totalFunds != 49) {
        std::fprintf(stderr, "rubble shore %u %u funds %ld\n", rubble_dock.map[29][31], rubble_dock.map[31][31],
                     static_cast<long>(rubble_dock.totalFunds));
        return fail(97, "bulldozing a rubble marina did not put the shoreline back");
    }
    if (bulldoze_special_at(&rubble_dock, 29, 31) || rubble_dock.map[29][31] != REDGE) {
        return fail(98, "a second bulldoze treated the restored shoreline as a marina");
    }

    std::printf("new specials ok\n");
    return 0;
}

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
    shore.map[29][31] = RIVER;
    if (place_special_building(&shore, marina, 30, 30, error) || error != "Can't build on water.") {
        std::fprintf(stderr, "on water '%s'\n", error.c_str());
        return fail(17, "a marina standing on water was accepted");
    }
    shore.map[29][31] = DIRT;
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

    std::printf("new specials ok\n");
    return 0;
}

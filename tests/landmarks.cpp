// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Mayor's House unlock and cap, amusement park and casino income, casino
// crime, the year digest, and a skippable dither.

#include "civic.hpp"
#include "scene_fade.hpp"
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

void fill_dirt(Micropolis &sim)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            sim.map[x][y] = DIRT;
        }
    }
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

void arm(Micropolis &sim, int people)
{
    sim.generateSomeCity(3);
    fill_dirt(sim);
    sim.autoBulldoze = true;
    sim.totalPop = 0;
    sim.resPop = people / 20;
    sim.comPop = 0;
    sim.indPop = 0;
    sim.censusSnapshotValid = false;
    sim.setFunds(20000);
    sim.specialIncomeFn = special_annual_income;
    sim.specialCrimeFn = boost_special_crime;
}

} // namespace

int main()
{
    const int mayor = index_named("Mayor's House");
    const int casino = index_named("Casino");
    const int park = index_named("Amusement Park");
    const int zoo = index_named("Zoo");
    if (mayor < 0 || casino < 0 || park < 0 || zoo < 0) {
        return fail(1, "a landmark is missing from the table");
    }
    if (special_building(zoo).annual_income != 0 || special_building(mayor).annual_income != 0 ||
        special_building(park).annual_income != 200 || special_building(casino).annual_income != 300 ||
        special_building(mayor).cost != 100 || special_building(park).cost != 100) {
        return fail(2, "landmark prices or incomes do not match the City 2 rules");
    }
    if (std::string(special_building(park).effect).find("$200") == std::string::npos ||
        std::string(special_building(casino).effect).find("$300") == std::string::npos ||
        std::string(special_building(casino).effect).find("crime") == std::string::npos ||
        std::string(special_building(mayor).effect).find("residential") == std::string::npos) {
        return fail(3, "query text does not describe the landmark");
    }

    Micropolis locked;
    arm(locked, 1980);
    std::string error;
    if (place_special_building(&locked, mayor, 10, 10, error) ||
        error.find("Available at 2,000 population (Town)") == std::string::npos) {
        std::fprintf(stderr, "lock '%s' pop %d\n", error.c_str(), special_population(&locked));
        return fail(4, "Mayor's House was not locked below Town");
    }
    if ((locked.map[10][10] & LOMASK) != DIRT || locked.totalFunds != 20000) {
        return fail(5, "a locked Mayor's House changed the map or the funds");
    }
    const std::string menu = special_menu_detail(&locked, mayor);
    if (menu.find("Available at 2,000 population (Town)") == std::string::npos) {
        return fail(6, "the specials menu hides a locked Mayor's House");
    }

    Micropolis open;
    arm(open, 2000);
    if (special_population(&open) < 2000 || !place_special_building(&open, mayor, 10, 10, error)) {
        std::fprintf(stderr, "unlock '%s' pop %d\n", error.c_str(), special_population(&open));
        return fail(7, "Mayor's House did not unlock at 2,000");
    }
    if (place_special_building(&open, mayor, 20, 10, error) || error.find("Only one Mayor's House") == std::string::npos) {
        return fail(8, "a second Mayor's House was accepted");
    }
    if (!place_special_building(&open, zoo, 20, 10, error)) {
        return fail(9, "the zoo was capped along with the Mayor's House");
    }

    Micropolis mix;
    arm(mix, 5000);
    if (!place_special_building(&mix, park, 10, 10, error) || !place_special_building(&mix, casino, 16, 10, error) ||
        !place_special_building(&mix, park, 22, 10, error)) {
        return fail(10, "a mix of parks and casinos under the cap was refused");
    }
    if (place_special_building(&mix, casino, 28, 10, error) || error.find("limited to 3") == std::string::npos) {
        std::fprintf(stderr, "cap '%s'\n", error.c_str());
        return fail(11, "the shared park and casino cap was not enforced");
    }
    if (!place_special_building(&mix, zoo, 34, 10, error)) {
        return fail(12, "the zoo was caught by the park and casino cap");
    }

    Micropolis books;
    arm(books, 0);
    if (!place_special_building(&books, park, 10, 20, error) || !place_special_building(&books, casino, 20, 20, error)) {
        return fail(13, "park and casino could not be placed for the income check");
    }
    const long before = static_cast<long>(books.totalFunds);
    books.taxFlag = false;
    books.collectTax();
    if (books.specialIncome != 500 || static_cast<long>(books.totalFunds) != before) {
        std::fprintf(stderr, "income %ld funds %ld\n", static_cast<long>(books.specialIncome),
                     static_cast<long>(books.totalFunds));
        return fail(14, "January did not record $200 plus $300 without paying it early");
    }
    books.commitBudgetPayment();
    if (books.specialIncome != 0 || static_cast<long>(books.totalFunds) != before + 500) {
        std::fprintf(stderr, "paid %ld funds %ld\n", static_cast<long>(books.specialIncome),
                     static_cast<long>(books.totalFunds));
        return fail(15, "collecting the year did not pay the park and the casino");
    }

    Micropolis zoo_only;
    arm(zoo_only, 0);
    if (!place_special_building(&zoo_only, zoo, 10, 10, error)) {
        return fail(16, "the zoo could not be placed");
    }
    const long zoo_funds = static_cast<long>(zoo_only.totalFunds);
    zoo_only.collectTax();
    zoo_only.commitBudgetPayment();
    if (zoo_only.specialIncome != 0 || static_cast<long>(zoo_only.totalFunds) != zoo_funds) {
        return fail(17, "the zoo paid income");
    }

    Micropolis crime;
    arm(crime, 0);
    if (!place_special_building(&crime, casino, 30, 30, error)) {
        return fail(18, "the casino could not be placed for the crime check");
    }
    crime.crimeRateMap.clear();
    crime.policeStationMap.clear();
    const int sample_x = 30;
    const int sample_y = 30;
    crime.crimeRateMap.worldSet(sample_x, sample_y, 20);
    boost_special_crime(&crime);
    const int raised = crime.crimeRateMap.worldGet(sample_x, sample_y);
    if (raised < 100) {
        std::fprintf(stderr, "crime %d\n", raised);
        return fail(19, "the casino did not raise local crime");
    }
    crime.crimeRateMap.clear();
    crime.crimeRateMap.worldSet(sample_x, sample_y, 20);
    crime.policeStationMap.worldSet(sample_x, sample_y, 800);
    boost_special_crime(&crime);
    const int policed = crime.crimeRateMap.worldGet(sample_x, sample_y);
    if (policed >= raised) {
        std::fprintf(stderr, "policed %d raised %d\n", policed, raised);
        return fail(20, "police coverage did not cut the casino crime");
    }

    Micropolis quiet;
    arm(quiet, 0);
    if (!place_special_building(&quiet, park, 30, 30, error)) {
        return fail(21, "the amusement park could not be placed");
    }
    quiet.crimeRateMap.clear();
    quiet.crimeRateMap.worldSet(30, 30, 15);
    boost_special_crime(&quiet);
    if (quiet.crimeRateMap.worldGet(30, 30) != 15) {
        return fail(22, "the amusement park raised crime");
    }

    civic::Ledger ledger;
    civic::award(ledger, civic::Town, 1904, 2, nullptr);
    std::vector<civic::DigestNotice> notes;
    civic::DigestNotice fire;
    fire.text = "Fire reported.";
    fire.year = 1904;
    fire.month = 6;
    fire.has_place = true;
    fire.tile_x = 12;
    fire.tile_y = 18;
    notes.push_back(fire);
    notes.push_back(fire);
    notes.push_back(fire);
    civic::DigestNotice nag;
    nag.text = "Cannot build here.";
    nag.year = 1904;
    nag.month = 4;
    for (int i = 0; i < 8; ++i) {
        notes.push_back(nag);
    }
    const civic::YearDigest digest =
        civic::make_year_digest(1000, 1500, true, false, 2000, ledger, nullptr, "Crime", 40, 0, 0, false, 1905, notes);
    if (digest.card.opening_value.find("1,000") == std::string::npos || digest.events.empty()) {
        return fail(23, "the year digest dropped the opening funds");
    }
    bool saw_fire = false;
    bool saw_town = false;
    bool dumped_nag = false;
    for (const civic::DigestLine &line : digest.events) {
        if (line.text.find("Fire reported") != std::string::npos && line.text.find("3 times") != std::string::npos) {
            saw_fire = true;
        }
        if (line.text.find("Town") != std::string::npos) {
            saw_town = true;
        }
        if (line.text.find("Cannot build here") != std::string::npos) {
            dumped_nag = true;
        }
    }
    if (!saw_fire || !saw_town || dumped_nag || !digest.locate || digest.locate_x != 12) {
        return fail(24, "the digest did not coalesce the fire or locate it");
    }
    const civic::YearDigest quiet_year =
        civic::make_year_digest(0, 0, true, false, 0, civic::Ledger{}, nullptr, nullptr, -1, -1, 0, false, 1901, {});
    if (quiet_year.events.empty() || quiet_year.events[0].text.find("No disaster") == std::string::npos) {
        return fail(25, "an empty year invented an event");
    }

    SceneFade fade;
    fade.start();
    if (!fade.active() || !fade.darken(0, 1)) {
        return fail(26, "the dither did not start");
    }
    int dark = 0;
    int clear = 0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            if (fade.darken(x, y)) {
                ++dark;
            } else {
                ++clear;
            }
        }
    }
    if (dark == 0 || clear == 0) {
        return fail(27, "the dither covered every pixel or none");
    }
    fade.skip();
    if (fade.active() || fade.darken(0, 0)) {
        return fail(28, "a click did not skip the dither");
    }
    fade.start();
    fade.advance(SceneFade::kTotalMs);
    if (fade.active()) {
        return fail(29, "the dither outlived its duration");
    }
    if (civic::year_just_ended(1901, 0, 48) != 1900 || civic::year_just_ended(1900, 11, 47) != 1900 ||
        civic::year_just_ended(1900, 0, 0) != 1900) {
        return fail(30, "the reviewed year is not the year that just ended");
    }

    std::printf("landmarks ok\n");
    return 0;
}

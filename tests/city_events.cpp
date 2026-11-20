// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Riot, Autonomous Zone, Lion Escape and Prison Escape: deterministic sim
// checks on a hand-built map, the zoo and prison requirements, tools
// refused inside the zone, and the LCD1 save/load round trip.

#include "city_events.hpp"
#include "city_session.hpp"
#include "city_tail.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"
#include "text.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using city_events::Kind;
using city_events::Note;

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "city_events %d: %s\n", code, message.c_str());
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

void road(Micropolis &sim, int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; ++x) {
        for (int y = y0; y <= y1; ++y) {
            sim.map[x][y] = ROADS | BULLBIT | BURNBIT;
        }
    }
}

// A built 3x3 zone with this centre tile, laid out the way zonePlop does.
void plop(Micropolis &sim, int cx, int cy, int center)
{
    int base = center - 4;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            sim.map[cx + dx][cy + dy] = static_cast<unsigned short>(base++ | BNCNBIT);
        }
    }
    sim.map[cx][cy] |= ZONEBIT | BULLBIT;
}

int count_fires(const Micropolis &sim)
{
    int fires = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int tile = sim.map[x][y] & LOMASK;
            if (tile >= FIRE && tile <= LASTFIRE) {
                ++fires;
            }
        }
    }
    return fires;
}

void set_police(Micropolis &sim, int value)
{
    for (int bx = 0; bx < sim.policeStationEffectMap.MAP_W; ++bx) {
        for (int by = 0; by < sim.policeStationEffectMap.MAP_H; ++by) {
            sim.policeStationEffectMap.set(bx, by, static_cast<short>(value));
        }
    }
}

void set_land(Micropolis &sim, int value)
{
    for (int x = 0; x < WORLD_W; x += 2) {
        for (int y = 0; y < WORLD_H; y += 2) {
            sim.landValueMap.worldSet(x, y, static_cast<Byte>(value));
        }
    }
}

// One city-time step: the clock moves on, then 16 simulator frames.
void advance(Micropolis &sim, int steps, std::vector<Note> &notes)
{
    for (int s = 0; s < steps; ++s) {
        ++sim.cityTime;
        for (int f = 0; f < 16; ++f) {
            city_events::frame(&sim, notes);
        }
    }
}

bool said(const std::vector<Note> &notes, const std::string &part)
{
    for (const Note &note : notes) {
        if (note.text.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void base_city(Micropolis &sim)
{
    sim.generateSomeCity(11);
    fill_dirt(sim);
    sim.setFunds(100000);
    sim.cityTime = 480;
    city_events::clear(&sim);
    set_police(sim, 0);
    set_land(sim, 0);
}

// Commercial blocks on both sides of a road: a built-up neighbourhood.
void commercial_strip(Micropolis &sim)
{
    road(sim, 10, 20, 70, 20);
    for (int i = 0; i < 10; ++i) {
        plop(sim, 12 + i * 6, 22, CZB + 9 * 2);
        plop(sim, 12 + i * 6, 18, CZB + 9 * 2);
    }
}

int prison_index()
{
    return special_index_for_tile(SPECIAL_PRISON);
}

// Zoo at (60, 50): a road loop around it, a road east with homes on it.
bool zoo_town(Micropolis &sim, std::string &error)
{
    if (!place_special_building(&sim, 0, 60, 50, error)) {
        return false;
    }
    road(sim, 58, 48, 62, 48);
    road(sim, 58, 52, 100, 52);
    road(sim, 58, 48, 58, 52);
    road(sim, 62, 48, 62, 52);
    for (int i = 0; i < 6; ++i) {
        plop(sim, 66 + i * 5, 54, RZB + 9 * 3);
    }
    return true;
}

int lion_tests()
{
    Micropolis sim;
    base_city(sim);
    std::vector<Note> notes;
    if (city_events::unavailable_reason(&sim, Kind::LionEscape).find("Zoo") == std::string::npos ||
        city_events::start(&sim, Kind::LionEscape, notes) || sim.cityEvents.lionActive != 0) {
        return fail(40, "Lion Escape started, or did not ask for a Zoo, in a city without one");
    }
    std::string error;
    if (!zoo_town(sim, error)) {
        return fail(41, "could not place the test zoo: " + error);
    }
    notes.clear();
    if (!city_events::unavailable_reason(&sim, Kind::LionEscape).empty() ||
        !city_events::start(&sim, Kind::LionEscape, notes) || sim.cityEvents.lionActive == 0 ||
        !said(notes, "A lion has escaped from the zoo!")) {
        return fail(42, "Lion Escape did not start beside a zoo with roads");
    }
    const city_events::LionView start = city_events::lion(&sim);
    if (!notes.back().has_place || notes.back().x != start.x / 16 || notes.back().y != start.y / 16) {
        return fail(43, "the lion notice does not point at the lion");
    }
    // It walks only on roads and clear land.
    Micropolis copy;
    base_city(copy);
    zoo_town(copy, error);
    std::vector<Note> again;
    city_events::start(&copy, Kind::LionEscape, again);
    for (int step = 0; step < 20; ++step) {
        ++sim.cityTime;
        for (int f = 0; f < 16; ++f) {
            city_events::frame(&sim, notes);
            const city_events::LionView now = city_events::lion(&sim);
            if (!now.active) {
                break;
            }
            for (int corner = 0; corner < 2; ++corner) {
                const int tx = (now.x - (corner ? 0 : 1)) / 16;
                const int ty = (now.y - (corner ? 0 : 1)) / 16;
                const int tile = sim.map[tx][ty] & LOMASK;
                if (!(tile == DIRT || (tile >= ROADBASE && tile <= LASTROAD))) {
                    return fail(44, "the lion walked onto tile " + std::to_string(tile));
                }
            }
        }
    }
    if (sim.cityEvents.lionEaten <= 0 || !said(notes, "eating residents")) {
        return fail(45, "the lion ate nobody in five months beside six homes");
    }
    // Same city, same time: same walk.
    for (int step = 0; step < 20; ++step) {
        ++copy.cityTime;
        for (int f = 0; f < 16; ++f) {
            city_events::frame(&copy, again);
        }
    }
    if (copy.cityEvents.lionX != sim.cityEvents.lionX || copy.cityEvents.lionY != sim.cityEvents.lionY ||
        copy.cityEvents.lionEaten != sim.cityEvents.lionEaten) {
        return fail(46, "the lion's walk is not deterministic");
    }
    // Keepers bring it home at six months.
    notes.clear();
    advance(sim, city_events::kLionMaxSteps, notes);
    if (sim.cityEvents.lionActive != 0 || !said(notes, "Zookeepers caught the lion")) {
        return fail(47, "the lion was still loose after six months");
    }
    // Police coverage catches it after a month.
    notes.clear();
    if (!city_events::start(&sim, Kind::LionEscape, notes)) {
        return fail(48, "a second Lion Escape did not start");
    }
    set_police(sim, city_events::kLionCatchCoverage);
    advance(sim, city_events::kLionCatchAfterSteps - 1, notes);
    if (sim.cityEvents.lionActive == 0) {
        return fail(49, "the police caught the lion before it had been out a month");
    }
    advance(sim, 1, notes);
    if (sim.cityEvents.lionActive != 0 || !said(notes, "Police caught the lion")) {
        return fail(50, "police coverage did not catch the lion");
    }
    // A zoo walled in by buildings has nowhere to let a lion out.
    Micropolis boxed;
    base_city(boxed);
    place_special_building(&boxed, 0, 60, 50, error);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) == 2) {
                boxed.map[60 + dx][50 + dy] = WOODS | BLBNCNBIT;
            }
        }
    }
    if (city_events::unavailable_reason(&boxed, Kind::LionEscape).find("road or open land") == std::string::npos) {
        return fail(51, "a walled-in zoo let the lion out");
    }
    return 0;
}

int zone_tests()
{
    Micropolis sim;
    base_city(sim);
    std::vector<Note> notes;
    if (!city_events::start_zone_at(&sim, 4, 4, notes) || sim.cityEvents.zoneCount != 2 ||
        !said(notes, "An Autonomous Zone has formed!")) {
        return fail(20, "the zone did not form two blocks wide");
    }
    if (city_events::start_zone_at(&sim, 8, 8, notes) || city_events::start_zone_at(&sim, 14, 2, notes)) {
        return fail(21, "a second zone, or one off the map edge, was allowed");
    }
    // Blocks (4,4) and (5,4) are tiles 32..47 x 32..39.
    if (!city_events::in_zone(&sim, 32, 32) || !city_events::in_zone(&sim, 47, 39) ||
        city_events::in_zone(&sim, 48, 32) || city_events::in_zone(&sim, 32, 40)) {
        return fail(22, "the zone does not cover its two blocks exactly");
    }
    if (!city_events::edit_blocked(&sim, TOOL_ROAD, 33, 33) || city_events::edit_blocked(&sim, TOOL_ROAD, 50, 33) ||
        !city_events::edit_blocked(&sim, TOOL_RESIDENTIAL, 48, 33) ||
        city_events::edit_blocked(&sim, TOOL_RESIDENTIAL, 49, 33) ||
        city_events::edit_blocked(&sim, TOOL_QUERY, 33, 33) ||
        !city_events::edit_blocked(&sim, TOOL_BULLDOZER, 40, 36) ||
        !city_events::edit_blocked(&sim, TOOL_AIRPORT, 48, 40) ||
        city_events::edit_blocked(&sim, TOOL_AIRPORT, 49, 40)) {
        return fail(23, "a tool footprint is judged wrongly against the zone border");
    }
    // The engine refuses with its own notice.
    sim.editBlockedFn = city_events::edit_blocked;
    if (sim.doTool(TOOL_ROAD, 33, 33) != TOOLRESULT_FAILED || (sim.map[33][33] & LOMASK) != DIRT ||
        sim.doTool(TOOL_ROAD, 50, 33) != TOOLRESULT_OK) {
        return fail(24, "the engine built inside the zone, or refused outside it");
    }
    sim.editBlockedFn = nullptr;
    // Land value: 50 off inside, falling off over 4 tiles outside.
    set_land(sim, 100);
    city_events::apply_land(&sim);
    if (sim.landValueMap.worldGet(34, 34) != 50 || sim.landValueMap.worldGet(30, 34) != 60 ||
        sim.landValueMap.worldGet(28, 34) != 80 || sim.landValueMap.worldGet(26, 34) != 100 ||
        sim.landValueMap.worldGet(48, 34) != 60 || sim.landValueMap.worldGet(34, 42) != 80) {
        return fail(25, "land value near the zone: inside " + std::to_string(sim.landValueMap.worldGet(34, 34)) +
                            ", 1 tile out " + std::to_string(sim.landValueMap.worldGet(30, 34)) + ", 3 out " +
                            std::to_string(sim.landValueMap.worldGet(28, 34)) + ", 5 out " +
                            std::to_string(sim.landValueMap.worldGet(26, 34)));
    }
    // Every two months: grow where police are thin.
    notes.clear();
    advance(sim, city_events::kZoneReviewSteps - 1, notes);
    if (sim.cityEvents.zoneCount != 2) {
        return fail(26, "the zone changed before two months");
    }
    advance(sim, 1, notes);
    if (sim.cityEvents.zoneCount != 3 || !said(notes, "has grown")) {
        return fail(27, "the zone did not grow with no police");
    }
    // It grows toward the least-policed neighbour.
    set_police(sim, 0);
    sim.policeStationEffectMap.set(6, 4, 30);
    sim.policeStationEffectMap.set(3, 4, 30);
    sim.policeStationEffectMap.set(4, 3, 30);
    sim.policeStationEffectMap.set(5, 3, 30);
    sim.policeStationEffectMap.set(4, 5, 30);
    advance(sim, city_events::kZoneReviewSteps, notes);
    const auto blocks = city_events::zone_blocks(&sim);
    if (blocks.size() != 4) {
        return fail(28, "the zone did not grow a second time");
    }
    // Between the thresholds it holds.
    set_police(sim, (city_events::kZoneGrowBelow + city_events::kZoneShrinkAt) / 2);
    advance(sim, city_events::kZoneReviewSteps * 2, notes);
    if (sim.cityEvents.zoneCount != 4) {
        return fail(29, "the zone moved with middling police coverage");
    }
    // Strong police shrink it, a block every two months, until it is gone.
    set_police(sim, city_events::kZoneShrinkAt);
    notes.clear();
    advance(sim, city_events::kZoneReviewSteps, notes);
    if (sim.cityEvents.zoneCount != 3 || !said(notes, "shrinking")) {
        return fail(30, "police coverage did not shrink the zone");
    }
    advance(sim, city_events::kZoneReviewSteps * 3, notes);
    if (sim.cityEvents.zoneCount != 0 || !said(notes, "has ended") || city_events::in_zone(&sim, 33, 33)) {
        return fail(31, "the zone did not end under police pressure");
    }
    // Gone: land value is back to the scan's own figure.
    set_land(sim, 100);
    city_events::apply_land(&sim);
    if (sim.landValueMap.worldGet(34, 34) != 100) {
        return fail(32, "land value stayed low after the zone ended");
    }
    // The barricades run along the outside edge only.
    city_events::start_zone_at(&sim, 4, 4, notes);
    const auto pieces = city_events::barricades(&sim);
    if (pieces.size() != 2 * (16 + 8)) {
        return fail(33, "the barricade count is " + std::to_string(pieces.size()));
    }
    // A cap keeps a neglected zone from eating the whole map.
    set_police(sim, 0);
    advance(sim, city_events::kZoneReviewSteps * 80, notes);
    if (sim.cityEvents.zoneCount != city_events::kZoneMaxBlocks) {
        return fail(34, "the zone grew past its cap or stopped short: " + std::to_string(sim.cityEvents.zoneCount));
    }
    // The menu's zone picks the least-policed built-up pair.
    Micropolis built;
    base_city(built);
    if (city_events::unavailable_reason(&built, Kind::AutonomousZone).find("built-up") == std::string::npos) {
        return fail(35, "an empty map offered a zone");
    }
    commercial_strip(built);
    set_police(built, 100);
    built.policeStationEffectMap.set(4, 2, 0);
    built.policeStationEffectMap.set(5, 2, 0);
    notes.clear();
    if (!city_events::start(&built, Kind::AutonomousZone, notes) || !city_events::in_zone(&built, 32, 20) ||
        !city_events::in_zone(&built, 47, 20)) {
        return fail(36, "the zone did not form on the least-policed built-up blocks");
    }
    return 0;
}

int riot_tests()
{
    Micropolis sim;
    base_city(sim);
    commercial_strip(sim);
    Micropolis twin;
    base_city(twin);
    commercial_strip(twin);
    std::vector<Note> notes;
    if (!city_events::unavailable_reason(&sim, Kind::Riot).empty() || !city_events::start(&sim, Kind::Riot, notes) ||
        !said(notes, "Fiery, but mostly peaceful.")) {
        return fail(10, "the riot did not start with its notice");
    }
    if (count_fires(sim) != city_events::kRiotFires) {
        return fail(11, "the riot lit " + std::to_string(count_fires(sim)) + " fires");
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int tile = sim.map[x][y] & LOMASK;
            if (tile >= FIRE && tile <= LASTFIRE && (y < 17 || y > 23 || y == 20)) {
                return fail(12, "a riot fire is not on a commercial building");
            }
        }
    }
    std::vector<Note> twin_notes;
    city_events::start(&twin, Kind::Riot, twin_notes);
    // The fire frame comes from the engine's own animation roll; where the
    // fires are comes from the city alone.
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int a = sim.map[x][y] & LOMASK;
            const int b = twin.map[x][y] & LOMASK;
            if ((a >= FIRE && a <= LASTFIRE) != (b >= FIRE && b <= LASTFIRE)) {
                return fail(13, "the riot is not deterministic");
            }
        }
    }
    // Crime: +40 on developed blocks, fading linearly over six months.
    set_land(sim, 50);
    for (int x = 0; x < WORLD_W; x += 2) {
        for (int y = 0; y < WORLD_H; y += 2) {
            sim.crimeRateMap.worldSet(x, y, 10);
        }
    }
    city_events::apply_crime(&sim);
    if (sim.crimeRateMap.worldGet(80, 80) != 10 + city_events::kRiotCrimePeak) {
        return fail(14, "riot crime at the start is " + std::to_string(sim.crimeRateMap.worldGet(80, 80) - 10));
    }
    advance(sim, city_events::kRiotSteps / 2, notes);
    if (city_events::riot_crime_now(&sim) != city_events::kRiotCrimePeak) {
        return fail(15, "riot crime after three months is " + std::to_string(city_events::riot_crime_now(&sim)));
    }
    advance(sim, city_events::kRiotSteps / 4, notes);
    if (city_events::riot_crime_now(&sim) != city_events::kRiotCrimePeak / 2) {
        return fail(18, "riot crime after four and a half months is " +
                            std::to_string(city_events::riot_crime_now(&sim)));
    }
    advance(sim, city_events::kRiotSteps / 4, notes);
    if (city_events::riot_crime_now(&sim) != 0 || city_events::any_active(&sim)) {
        return fail(16, "riot crime outlasted six months");
    }
    // A town with no shops still riots: crime only.
    Micropolis bare;
    base_city(bare);
    notes.clear();
    if (!city_events::start(&bare, Kind::Riot, notes) || count_fires(bare) != 0 ||
        city_events::riot_crime_now(&bare) != city_events::kRiotCrimePeak) {
        return fail(17, "a riot without commercial buildings misbehaved");
    }
    return 0;
}

bool prison_town(Micropolis &sim, std::string &error)
{
    if (prison_index() < 0 || !place_special_building(&sim, prison_index(), 50, 50, error)) {
        return false;
    }
    road(sim, 46, 48, 46, 56);
    for (int i = 0; i < 3; ++i) {
        plop(sim, 44, 49 + i * 3, RZB + 9 * 3);
        plop(sim, 56, 49 + i * 3, RZB + 9 * 3);
    }
    plop(sim, 80, 80, RZB + 9 * 3); // well out of reach
    // Woods right by the prison: the inmates set buildings on fire, not trees.
    for (int x = 48; x <= 56; ++x) {
        for (int y = 43; y <= 46; ++y) {
            sim.map[x][y] = WOODS | BLBNCNBIT;
        }
    }
    return true;
}

int prison_tests()
{
    Micropolis sim;
    base_city(sim);
    std::vector<Note> notes;
    if (city_events::unavailable_reason(&sim, Kind::PrisonEscape).find("Prison") == std::string::npos ||
        city_events::start(&sim, Kind::PrisonEscape, notes)) {
        return fail(60, "Prison Escape started, or did not ask for a Prison, without one");
    }
    std::string error;
    if (!prison_town(sim, error)) {
        return fail(61, "could not place the test prison: " + error);
    }
    Micropolis slow;
    base_city(slow);
    prison_town(slow, error);
    notes.clear();
    if (!city_events::start(&sim, Kind::PrisonEscape, notes) || !said(notes, "Prison break!")) {
        return fail(62, "Prison Escape did not start beside a prison");
    }
    const int fires = count_fires(sim);
    for (int x = 48; x <= 56; ++x) {
        for (int y = 43; y <= 46; ++y) {
            if ((sim.map[x][y] & LOMASK) != WOODS) {
                return fail(68, "the prison break set the woods on fire");
            }
        }
    }
    if (fires < 1 || fires > city_events::kPrisonFires) {
        return fail(63, "the prison break lit " + std::to_string(fires) + " fires");
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const int tile = sim.map[x][y] & LOMASK;
            const int dx = x - 50;
            const int dy = y - 50;
            if (tile >= FIRE && tile <= LASTFIRE &&
                dx * dx + dy * dy > city_events::kPrisonFireRadius * city_events::kPrisonFireRadius) {
                return fail(64, "a prison-break fire is too far from the prison");
            }
            if (sim.map[x][y] != slow.map[x][y] && dx >= -1 && dx <= 2 && dy >= -1 && dy <= 2) {
                return fail(65, "the prison set itself on fire");
            }
        }
    }
    // Crime near the prison only.
    set_land(sim, 50);
    for (int x = 0; x < WORLD_W; x += 2) {
        for (int y = 0; y < WORLD_H; y += 2) {
            sim.crimeRateMap.worldSet(x, y, 0);
        }
    }
    city_events::apply_crime(&sim);
    if (sim.crimeRateMap.worldGet(52, 52) != city_events::kPrisonCrimePeak || sim.crimeRateMap.worldGet(80, 80) != 0) {
        return fail(66, "prison-break crime is not local");
    }
    // It fades faster with police nearby.
    std::vector<Note> slow_notes;
    city_events::start(&slow, Kind::PrisonEscape, slow_notes);
    set_police(sim, 200);
    int with_police = 0;
    while (sim.cityEvents.prisonSteps > 0 && with_police < 100) {
        advance(sim, 1, notes);
        ++with_police;
    }
    int without = 0;
    while (slow.cityEvents.prisonSteps > 0 && without < 100) {
        advance(slow, 1, slow_notes);
        ++without;
    }
    if (!(with_police < without) || without != (city_events::kPrisonCrimePeak + 1) / 2 ||
        !said(notes, "back behind bars")) {
        return fail(67, "prison-break crime lasted " + std::to_string(with_police) + " steps with police and " +
                            std::to_string(without) + " without");
    }
    return 0;
}

int random_tests()
{
    Micropolis copy;
    std::string error;
    int counts[city_events::kKindCount] = {0, 0, 0, 0};
    int nothing = 0;
    for (int t = 0; t < 400; ++t) {
        base_city(copy);
        commercial_strip(copy);
        place_special_building(&copy, 0, 60, 50, error);
        road(copy, 58, 52, 70, 52);
        place_special_building(&copy, prison_index(), 50, 70, error);
        copy.cityTime = 1000 + t;
        std::vector<Note> notes;
        city_events::random_event(&copy, notes);
        if (copy.cityEvents.riotSteps > 0) {
            ++counts[0];
        } else if (copy.cityEvents.zoneCount > 0) {
            ++counts[1];
        } else if (copy.cityEvents.lionActive != 0) {
            ++counts[2];
        } else if (copy.cityEvents.prisonSteps > 0) {
            ++counts[3];
        } else {
            ++nothing;
        }
    }
    for (int k = 0; k < city_events::kKindCount; ++k) {
        if (counts[k] < 20) {
            return fail(80, "random roll " + std::to_string(k) + " came up " + std::to_string(counts[k]) + " times");
        }
    }
    if (nothing < 120 || nothing > 280) {
        return fail(81, "the quiet half of the roll came up " + std::to_string(nothing) + " times in 400");
    }
    // No zoo, no lion: that roll stays quiet.
    Micropolis plain;
    base_city(plain);
    for (int t = 0; t < 200; ++t) {
        plain.cityTime = 1000 + t;
        std::vector<Note> notes;
        city_events::random_event(&plain, notes);
        if (plain.cityEvents.lionActive != 0 || plain.cityEvents.prisonSteps > 0) {
            return fail(82, "a random lion or prison break without the building");
        }
        city_events::clear(&plain);
    }
    return 0;
}

std::vector<unsigned char> read_all(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool has_magic(const std::vector<unsigned char> &bytes, const char *magic)
{
    for (std::size_t i = 27120; i + 4 <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, magic, 4) == 0) {
            return true;
        }
    }
    return false;
}

int save_tests(const std::string &dir)
{
    Micropolis sim;
    base_city(sim);
    sim.cityName = "Saved";
    const std::string quiet = dir + "/events-quiet.cty";
    if (!sim.saveFile(quiet.c_str())) {
        return fail(90, "could not save the quiet city");
    }
    const std::vector<unsigned char> quiet_bytes = read_all(quiet);
    if (has_magic(quiet_bytes, "LCD1") ||
        classify_city_bytes(quiet_bytes.data(), quiet_bytes.size()) != CityFileKind::Complete) {
        return fail(91, "a city with no disaster wrote an LCD1 trailer or reads as incomplete");
    }
    // Every kind active at once.
    std::string error;
    commercial_strip(sim);
    place_special_building(&sim, 0, 60, 50, error);
    road(sim, 58, 52, 70, 52);
    place_special_building(&sim, prison_index(), 50, 70, error);
    std::vector<Note> notes;
    if (!city_events::start(&sim, Kind::Riot, notes) || !city_events::start_zone_at(&sim, 9, 9, notes) ||
        !city_events::start(&sim, Kind::LionEscape, notes) || !city_events::start(&sim, Kind::PrisonEscape, notes)) {
        return fail(92, "could not start every disaster for the save test");
    }
    advance(sim, 9, notes);
    const CityEvents before = sim.cityEvents;
    const std::string busy = dir + "/events-busy.cty";
    if (!sim.saveFile(busy.c_str())) {
        return fail(93, "could not save the busy city");
    }
    const std::vector<unsigned char> busy_bytes = read_all(busy);
    if (!has_magic(busy_bytes, "LCD1") ||
        classify_city_bytes(busy_bytes.data(), busy_bytes.size()) != CityFileKind::Complete) {
        return fail(94, "the busy city has no LCD1 trailer or reads as incomplete");
    }
    Micropolis back;
    if (!back.loadFile(busy.c_str())) {
        return fail(95, "could not load the busy city: " + back.loadErrorDetail);
    }
    const CityEvents &after = back.cityEvents;
    if (after.riotPeak != before.riotPeak || after.riotSteps != before.riotSteps ||
        after.riotTotal != before.riotTotal || after.zoneCount != before.zoneCount ||
        after.zoneClock != before.zoneClock || after.lionActive != before.lionActive || after.lionX != before.lionX ||
        after.lionY != before.lionY || after.lionDir != before.lionDir || after.lionSteps != before.lionSteps ||
        after.lionMealClock != before.lionMealClock || after.lionEaten != before.lionEaten ||
        after.lionRng != before.lionRng || after.prisonCrime != before.prisonCrime ||
        after.prisonX != before.prisonX || after.prisonSteps != before.prisonSteps ||
        std::memcmp(after.zoneBlock, before.zoneBlock, sizeof(before.zoneBlock[0]) * before.zoneCount) != 0 ||
        after.lastStepTime != back.cityTime) {
        return fail(96, "the disaster state changed across save and load");
    }
    // Loading the quiet city over the busy one clears it.
    if (!back.loadFile(quiet.c_str()) || city_events::any_active(&back)) {
        return fail(97, "a city saved without disasters loaded with some");
    }
    // A cut-off trailer is incomplete, and loads without the state.
    std::vector<unsigned char> cut = busy_bytes;
    std::size_t at = 0;
    for (std::size_t i = 27120; i + 4 <= cut.size(); ++i) {
        if (std::memcmp(cut.data() + i, "LCD1", 4) == 0) {
            at = i;
        }
    }
    cut.resize(at + 20);
    if (classify_city_bytes(cut.data(), cut.size()) != CityFileKind::Incomplete) {
        return fail(98, "a cut LCD1 trailer reads as complete");
    }
    Micropolis torn;
    if (!torn.loadCityFromMemory("torn.cty", cut.data(), cut.size()) || city_events::any_active(&torn)) {
        return fail(99, "a cut LCD1 trailer did not load as a city without disasters");
    }
    return 0;
}

int session_tests(const std::string &dir)
{
    CitySession session;
    session.new_city("Zone Town", 7);
    // A home built across the coming border (centre one tile outside).
    int home_y = -1;
    for (int y = 49; y <= 54 && home_y < 0; ++y) {
        session.use_tool(TOOL_BULLDOZER, 46, y);
        session.use_tool(TOOL_BULLDOZER, 47, y);
        session.use_tool(TOOL_BULLDOZER, 48, y);
    }
    for (int y = 49; y <= 54 && home_y < 0; ++y) {
        session.use_tool(TOOL_RESIDENTIAL, 47, y);
        if ((session.map_value(47, y) & ZONEBIT) != 0) {
            home_y = y;
        }
    }
    if (session.events_state().zoneCount != 0 || !session.start_autonomous_zone_at(6, 6)) {
        return fail(110, "the session would not form a zone");
    }
    if (home_y >= 0) {
        session.use_tool(TOOL_BULLDOZER, 46, home_y);
        if ((session.map_value(47, home_y) & ZONEBIT) == 0 || !session.bulldoze_blocked(46, home_y) ||
            session.bulldoze_blocked(44, home_y)) {
            return fail(121, "a home reaching into the zone was bulldozed from outside");
        }
    } else {
        return fail(122, "could not build the test home by the zone border");
    }
    // Blocks (6,6),(7,6): tiles 48..63 x 48..55. Find a dirt tile inside.
    int ix = -1;
    int iy = -1;
    for (int y = 48; y < 56 && ix < 0; ++y) {
        for (int x = 48; x < 64; ++x) {
            if ((session.map_value(x, y) & LOMASK) == DIRT) {
                ix = x;
                iy = y;
                break;
            }
        }
    }
    if (ix < 0) {
        return fail(111, "no clear land inside the test zone");
    }
    session.use_tool(TOOL_ROAD, ix, iy);
    if ((session.map_value(ix, iy) & LOMASK) != DIRT ||
        session.message() != "Nothing can be built or bulldozed inside the Autonomous Zone.") {
        return fail(112, "a road went down inside the zone, or the refusal said \"" + session.message() + "\"");
    }
    session.use_tool(TOOL_BULLDOZER, 50, 50);
    if (session.message() != "Nothing can be built or bulldozed inside the Autonomous Zone.") {
        return fail(113, "the bulldozer was not refused inside the zone");
    }
    if (session.place_special(0, ix, iy) || !session.in_autonomous_zone(ix, iy)) {
        return fail(114, "a landmark was placed inside the zone");
    }
    if (session.inspect_tile(ix, iy).condition != "Autonomous Zone" ||
        session.inspect_facts(ix, iy).notes.empty() || session.inspect_tile(10, 10).condition == "Autonomous Zone") {
        return fail(120, "inspect does not say which tiles are in the zone");
    }
    // The menu and scenarios start these even with disasters turned off.
    session.set_disasters(false);
    if (!session.start_event_named("riot") || session.start_event_named("volcano") ||
        session.events_state().riotSteps != city_events::kRiotSteps) {
        return fail(115, "scenario names did not start the riot, or an unknown name did something");
    }
    const std::string path = dir + "/events-session.cty";
    if (!session.save_city_as(path)) {
        return fail(116, "the session could not save");
    }
    CitySession again;
    if (!again.load_city(path) || again.autonomous_zone_blocks() != session.autonomous_zone_blocks() ||
        again.events_state().riotSteps != session.events_state().riotSteps) {
        return fail(117, "the zone or riot did not survive save and load");
    }
    // A new city starts clean.
    again.new_city("Fresh", 3);
    if (again.events_state().zoneCount != 0 || again.events_state().riotSteps != 0) {
        return fail(118, "a new city kept the old city's disasters");
    }
    // Requirements show in the session too.
    if (again.event_unavailable(Kind::LionEscape).find("Zoo") == std::string::npos ||
        again.event_unavailable(Kind::PrisonEscape).find("Prison") == std::string::npos ||
        again.start_event(Kind::LionEscape) || again.message().find("Zoo") == std::string::npos) {
        return fail(119, "a city with no zoo or prison offered the lion or the prison break");
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    // The table: four rows, each with a scenario name and its notice.
    city_events::Kind kind = Kind::Riot;
    if (!city_events::kind_from_key("autonomous_zone", kind) || kind != Kind::AutonomousZone ||
        !city_events::kind_from_key("lion_escape", kind) || kind != Kind::LionEscape ||
        !city_events::kind_from_key("prison_escape", kind) || kind != Kind::PrisonEscape ||
        !city_events::kind_from_key("riot", kind) || kind != Kind::Riot || city_events::kind_from_key("", kind)) {
        return fail(1, "the scenario names do not map to the four disasters");
    }
    if (std::string(city_events::info(Kind::Riot).notice).find("Fiery, but mostly peaceful.") == std::string::npos ||
        std::string(city_events::info(Kind::AutonomousZone).notice) != "An Autonomous Zone has formed!") {
        return fail(2, "the riot or zone notice lost the editor's wording");
    }
    std::string dir = argc > 1 ? argv[1] : ".";
    if (int code = riot_tests()) {
        return code;
    }
    if (int code = zone_tests()) {
        return code;
    }
    if (int code = lion_tests()) {
        return code;
    }
    if (int code = prison_tests()) {
        return code;
    }
    if (int code = random_tests()) {
        return code;
    }
    if (int code = save_tests(dir)) {
        return code;
    }
    if (int code = session_tests(dir)) {
        return code;
    }
    return 0;
}

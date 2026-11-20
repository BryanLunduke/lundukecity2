// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Landmarks that would use electricity conduct and take power like a zone,
// flash the bolt only while unpowered, and count toward the score's
// powered-zone ratio. Open-air landmarks (Big Park, Fountain) never take
// power, never flash, and stay out of the count.

#include "special_buildings.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
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

struct Placed {
    int index = -1;
    int cx = 0;
    int cy = 0;
};

constexpr int kWireRow = 8;

// A coal plant feeding a long wire along kWireRow. Each landmark touches the
// wire, except the one named in loose, which sits alone in the far corner.
bool build_city(Micropolis &sim, const std::string &loose, std::vector<Placed> &placed, std::string &why)
{
    sim.generateSomeCity(21);
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            sim.map[x][y] = DIRT;
        }
    }
    sim.autoBulldoze = true;
    sim.enableDisasters = false;
    sim.setFunds(100000);
    // Unlock every landmark (the population gates are not under test).
    sim.censusSnapshotValid = true;
    sim.snapResPop = 50000;
    if (sim.toolDown(TOOL_COALPOWER, 8, 8) != TOOLRESULT_OK) {
        why = "could not place a coal plant";
        return false;
    }
    for (int x = 11; x <= 110; ++x) {
        if (sim.toolDown(TOOL_WIRE, static_cast<short>(x), kWireRow) != TOOLRESULT_OK) {
            why = "could not run the wire";
            return false;
        }
    }
    int left = 12;
    // The Landmarks dropdown rows. The Data Center and Micro Nuclear Plant
    // (toolbar buttons) have their own power test in datacenter_micro.cpp.
    for (int i = 0; i < special_menu_count(); ++i) {
        const SpecialBuilding &b = special_building(i);
        const bool marina = b.needs_south_water;
        int top = marina ? kWireRow + 1 : kWireRow - b.height;
        int at_left = left;
        if (loose == b.name) {
            at_left = 90;
            top = marina ? 90 : 90;
        }
        if (b.needs_road) {
            for (int x = at_left - 1; x <= at_left + b.width; ++x) {
                sim.toolDown(TOOL_ROAD, static_cast<short>(x), static_cast<short>(top - 1));
            }
        }
        if (marina) {
            for (int x = at_left - 1; x <= at_left + b.width; ++x) {
                for (int y = top + b.height; y <= top + b.height + 2; ++y) {
                    sim.map[x][y] = RIVER;
                }
            }
        }
        if (loose == b.name && b.needs_power) {
            // A short dead wire that reaches nothing: the prison places
            // either way, but stays unpowered.
            sim.toolDown(TOOL_WIRE, static_cast<short>(at_left - 1), static_cast<short>(top));
        }
        std::string error;
        const int cx = at_left + b.anchor_x;
        const int cy = top + b.anchor_y;
        if (!place_special_building(&sim, i, cx, cy, error)) {
            why = std::string("could not place ") + b.name + ": " + error;
            return false;
        }
        placed.push_back({i, cx, cy});
        left += b.width + 2;
    }
    return true;
}

// Runs the simulator until power has spread and one full census (map phases
// 1..8) has finished, so the zone counts are a complete picture.
void settle(Micropolis &sim)
{
    sim.simPaused = false;
    sim.simPasses = 1;
    sim.setSpeed(3);
    for (int i = 0; i < 400; ++i) {
        sim.simTick();
    }
    for (int i = 0; i < 400 && sim.liveCensusComplete; ++i) {
        sim.simTick();
    }
    for (int i = 0; i < 400 && !sim.liveCensusComplete; ++i) {
        sim.simTick();
    }
}

int powered_specials(const Micropolis &sim, const std::vector<Placed> &placed)
{
    int count = 0;
    for (const Placed &p : placed) {
        if (special_uses_power(p.index) && (sim.map[p.cx][p.cy] & PWRBIT) != 0) {
            ++count;
        }
    }
    return count;
}

} // namespace

int main()
{
    const char *const uses_power[] = {"Zoo", "Library", "Mayor's House", "Casino", "Amusement Park", "Prison",
                                      "Marina"};
    const char *const open_air[] = {"Big Park", "Fountain"};
    for (const char *name : uses_power) {
        const int i = index_named(name);
        if (i < 0 || !special_uses_power(i) || special_building(i).open_air ||
            special_tile_skips_power(special_building(i).center_tile)) {
            return fail(1, std::string(name) + " should need power");
        }
    }
    for (const char *name : open_air) {
        const int i = index_named(name);
        if (i < 0 || special_uses_power(i) || !special_building(i).open_air ||
            !special_tile_skips_power(special_building(i).center_tile)) {
            return fail(2, std::string(name) + " should stay open-air");
        }
    }
    if (static_cast<int>(sizeof(uses_power) / sizeof(uses_power[0]) + sizeof(open_air) / sizeof(open_air[0])) !=
        special_menu_count()) {
        return fail(3, "a special building is in neither power group");
    }

    // Placement: every power-using footprint conducts, open-air ones do not.
    std::vector<Placed> placed;
    Micropolis lit;
    std::string why;
    if (!build_city(lit, "", placed, why)) {
        return fail(4, why);
    }
    for (const Placed &p : placed) {
        const SpecialBuilding &b = special_building(p.index);
        for (int dy = 0; dy < b.height; ++dy) {
            for (int dx = 0; dx < b.width; ++dx) {
                const bool conducts = (lit.map[p.cx - b.anchor_x + dx][p.cy - b.anchor_y + dy] & CONDBIT) != 0;
                if (conducts == b.open_air) {
                    return fail(5, std::string(b.name) + " footprint conduction is wrong");
                }
            }
        }
    }

    settle(lit);
    for (const Placed &p : placed) {
        const SpecialBuilding &b = special_building(p.index);
        const int raw = static_cast<int>(lit.map[p.cx][p.cy]);
        const int center_cell = b.anchor_y * b.width + b.anchor_x;
        if (b.open_air) {
            if ((raw & PWRBIT) != 0) {
                return fail(6, std::string(b.name) + " took power");
            }
        } else if ((raw & PWRBIT) == 0) {
            return fail(7, std::string(b.name) + " is on a powered wire but did not power up");
        }
        if (special_skips_power_at(&lit, p.cx, p.cy) != b.open_air) {
            return fail(22, std::string(b.name) + " inspect card power line is in the wrong group");
        }
        if (special_center_needs_bolt(raw, p.index, center_cell, true)) {
            return fail(8, std::string(b.name) + " flashes no-power while connected");
        }
    }
    // Coal plant plus seven powered landmarks; nothing unpowered. Big Park
    // and Fountain are in neither count.
    if (lit.poweredZoneCount != 1 + 7 || lit.unpoweredZoneCount != 0) {
        std::fprintf(stderr, "powered %d unpowered %d\n", lit.poweredZoneCount, lit.unpoweredZoneCount);
        return fail(9, "connected landmarks were not counted as powered zones");
    }

    // The same city with the amusement park off the grid.
    std::vector<Placed> loose_placed;
    Micropolis dark;
    if (!build_city(dark, "Amusement Park", loose_placed, why)) {
        return fail(10, why);
    }
    settle(dark);
    const int amusement = index_named("Amusement Park");
    for (const Placed &p : loose_placed) {
        const SpecialBuilding &b = special_building(p.index);
        const int raw = static_cast<int>(dark.map[p.cx][p.cy]);
        const int center_cell = b.anchor_y * b.width + b.anchor_x;
        const bool flashes = special_center_needs_bolt(raw, p.index, center_cell, true);
        if (p.index == amusement) {
            if ((raw & PWRBIT) != 0 || !flashes) {
                return fail(11, "a disconnected amusement park did not flash no-power");
            }
            if (special_center_needs_bolt(raw, p.index, center_cell, false)) {
                return fail(12, "the bolt showed during the blink-off half");
            }
            if (special_skips_power_at(&dark, p.cx, p.cy)) {
                return fail(21, "the inspect card would say an amusement park needs no power");
            }
        } else if (flashes) {
            return fail(13, std::string(b.name) + " flashed although connected or open-air");
        }
    }
    if (dark.poweredZoneCount != 1 + 6 || dark.unpoweredZoneCount != 1) {
        std::fprintf(stderr, "powered %d unpowered %d\n", dark.poweredZoneCount, dark.unpoweredZoneCount);
        return fail(14, "a disconnected landmark was not counted as unpowered");
    }
    if (powered_specials(dark, loose_placed) != 6) {
        return fail(15, "the wrong landmarks powered up");
    }

    // The evaluation uses those counts: an unpowered landmark costs score.
    // (The evaluation only scores a city with people in it.)
    lit.totalPop = 1;
    dark.totalPop = 1;
    lit.cityScore = 500;
    dark.cityScore = 500;
    lit.cityEvaluation();
    dark.cityEvaluation();
    if (lit.cityScore <= dark.cityScore) {
        std::fprintf(stderr, "score lit %d dark %d\n", lit.cityScore, dark.cityScore);
        return fail(16, "an unpowered landmark scored at least as well as a powered one");
    }

    // Old saves: footprints without CONDBIT are rewired after load.
    Micropolis old_save;
    std::vector<Placed> old_placed;
    if (!build_city(old_save, "", old_placed, why)) {
        return fail(17, why);
    }
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (special_index_for_tile(old_save.map[x][y] & LOMASK) >= 0 ||
                (old_save.map[x][y] & LOMASK) == kSpecialBodyTile) {
                old_save.map[x][y] &= static_cast<unsigned short>(~CONDBIT);
            }
        }
    }
    const int park = index_named("Big Park");
    for (const Placed &p : old_placed) {
        if (p.index == park) {
            old_save.map[p.cx][p.cy] |= CONDBIT; // a stale bit on an open-air one
        }
    }
    if (wire_special_buildings(&old_save) <= 0 || wire_special_buildings(&old_save) != 0) {
        return fail(18, "wiring an old save changed nothing, or was not idempotent");
    }
    settle(old_save);
    if (powered_specials(old_save, old_placed) != 7) {
        return fail(19, "landmarks from an old save did not power up after wiring");
    }
    for (const Placed &p : old_placed) {
        if (p.index == park && (old_save.map[p.cx][p.cy] & (CONDBIT | PWRBIT)) != 0) {
            return fail(20, "the open-air park kept a stale conductive bit");
        }
    }

    std::printf("landmark power ok (score lit %d, dark %d)\n", lit.cityScore, dark.cityScore);
    return 0;
}

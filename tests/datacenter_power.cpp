// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// A Data Center draws three times the power of a standard 3x3 zone.
//
// The power scan spends one unit per conducting tile, so a standard 3x3 zone
// costs 9 and a Data Center costs 27 (DATACENTER_POWER_MULTIPLIER). A micro
// nuclear plant gives 222 units and a coal plant 700, so a micro powers 24
// standard zones but only 8 Data Centers, and a coal plant 77 zones but only
// 25 Data Centers. A Data Center with a body tile missing counts as no zone
// and draws no more than its tiles. Nothing is saved: old saves load and
// simply draw the new amount on their next scan.

#include <cstdio>
#include <string>
#include <vector>

// The power scan is private; the test calls it directly.
#define private public
#include "micropolis.h"
#undef private

#include "special_buildings.hpp"
#include "win_compat.hpp"

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

int index_named(const char *name, int center_tile)
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).name) == name && special_building(i).center_tile == center_tile) {
            return i;
        }
    }
    return -1;
}

void blank_city(Micropolis &sim)
{
    sim.generateSomeCity(5);
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            sim.map[x][y] = DIRT;
        }
    }
    sim.autoBulldoze = true;
    sim.enableDisasters = false;
    sim.setFunds(1000000);
    sim.coalPowerPop = 0;
    sim.nuclearPowerPop = 0;
    sim.microNuclearPop = 0;
}

// A wire tile at (5,20) feeding a row of `count` 3x3 buildings that touch each
// other and the wire, powered by `micro` micro nuclear plants' worth and
// `coal` coal plants. True when every tile of every building has power.
bool all_powered(int index, int count, int micro, int coal)
{
    Micropolis sim;
    blank_city(sim);
    sim.microNuclearPop = static_cast<short>(micro);
    sim.coalPowerPop = static_cast<short>(coal);
    sim.map[5][20] = LHPOWER | CONDBIT | BURNBIT | BULLBIT;
    std::string error;
    for (int i = 0; i < count; ++i) {
        if (!place_special_building(&sim, index, 7 + 3 * i, 20, error)) {
            std::fprintf(stderr, "place %d: %s\n", i, error.c_str());
            return false;
        }
    }
    sim.pushPowerStack(Position(5, 20));
    sim.doPowerScan();
    for (int i = 0; i < count; ++i) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (sim.powerGridMap.worldGet(7 + 3 * i + dx, 20 + dy) == 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The most buildings in the row (up to 30) that the plants can power.
int most_powered(int index, int micro, int coal)
{
    int best = 0;
    for (int count = 1; count <= 30; ++count) {
        if (!all_powered(index, count, micro, coal)) {
            break;
        }
        best = count;
    }
    return best;
}

} // namespace

int main()
{
    static_assert(DATACENTER_POWER_MULTIPLIER == 3, "a Data Center draws three times a zone");
    static_assert(kDataCenterPowerTiles == 27, "27 units for a Data Center");
    const int zoo = index_named("Zoo", SPECIAL_ZOO);
    const int lot = special_datacenter_lot_index();
    const int built = special_datacenter_index();
    if (zoo < 0 || lot < 0 || built < 0) {
        return fail(1, "a building row is missing");
    }

    // Micro plant: 222 units. The wire tile costs 1, a standard 3x3 zone 9, a
    // Data Center 27: (222 - 1) / 9 = 24 zones, (222 - 1) / 27 = 8 Data Centers.
    const int zoos = most_powered(zoo, 1, 0);
    const int lots = most_powered(lot, 1, 0);
    const int dcs = most_powered(built, 1, 0);
    std::printf("micro plant: standard zones %d, vacant lots %d, Data Centers %d\n", zoos, lots, dcs);
    if (zoos != 24) {
        return fail(2, "a micro plant should power 24 standard 3x3 zones in a row");
    }
    if (dcs != 8 || lots != 8) {
        return fail(3, "a micro plant should power 8 Data Centers (3x a zone), lot or built");
    }

    // Coal plant: 700 units. All 30 zones of the row need 271; Data Centers
    // fit (700 - 1) / 27 = 25 of them.
    const int zoos_coal = most_powered(zoo, 0, 1);
    const int dcs_coal = most_powered(built, 0, 1);
    std::printf("one coal plant: standard zones %d of 30, Data Centers %d of 30\n", zoos_coal, dcs_coal);
    if (zoos_coal != 30 || dcs_coal != 25) {
        return fail(4, "a coal plant should power 25 of 30 Data Centers but all 30 standard zones");
    }

    // A Data Center lot, which opens on power alone, still
    // opens from a micro plant (it needs 27 of its 222 units).
    {
        Micropolis sim;
        blank_city(sim);
        sim.microNuclearPop = 1;
        sim.map[5][20] = LHPOWER | CONDBIT | BURNBIT | BULLBIT;
        std::string error;
        if (!place_special_building(&sim, lot, 7, 20, error)) {
            return fail(5, error);
        }
        sim.pushPowerStack(Position(5, 20));
        sim.doPowerScan();
        if (sim.powerGridMap.worldGet(7, 20) == 0) {
            return fail(6, "a lone Data Center was not powered by a micro plant");
        }
    }

    std::printf("datacenter_power ok\n");
    return 0;
}

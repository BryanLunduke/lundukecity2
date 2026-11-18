// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The Data Center and the Micro Nuclear Plant.
//
// A Data Center is a 3x3 zone. Its vacant lot opens once it has power, and
// a powered Data Center counts as one industrial plus one commercial zone.
// It makes no pollution. A micro nuclear plant is 1x1, supplies 1/9 of a
// nuclear plant's power (nine of them equal one plant exactly), counts 1/9
// toward the assessed value, and cannot melt down.

#include "city_glance.hpp"
#include "night_light.hpp"
#include "special_buildings.hpp"
#include "tools.hpp"

#include "micropolis.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>
#include "win_compat.hpp"

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
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
    sim.censusSnapshotValid = true;
    sim.snapResPop = 50000;
}

// Runs until power has spread and one full census has finished.
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

int powered_cells(Micropolis &sim)
{
    int count = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (sim.powerGridMap.worldGet(x, y) != 0) {
                ++count;
            }
        }
    }
    return count;
}

// One long unbranched wire: rows 2, 4, ... joined at alternating ends, so
// each tile powers exactly one more and the powered count is the capacity.
void snake_wire(Micropolis &sim)
{
    for (int y = 2; y <= 96; y += 2) {
        for (int x = 2; x <= 117; ++x) {
            sim.map[x][y] = LHPOWER | CONDBIT | BURNBIT | BULLBIT;
        }
        if (y + 2 <= 96) {
            const int join = ((y / 2) % 2 == 1) ? 117 : 2;
            sim.map[join][y + 1] = LVPOWER | CONDBIT | BURNBIT | BULLBIT;
        }
    }
}

bool place(Micropolis &sim, int index, int cx, int cy, std::string &why)
{
    std::string error;
    if (!place_special_building(&sim, index, cx, cy, error)) {
        why = std::string("could not place ") + special_building(index).name + ": " + error;
        return false;
    }
    return true;
}

bool no_pollution(Micropolis &sim)
{
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            if (sim.pollutionDensityMap.worldGet(x, y) != 0) {
                return false;
            }
        }
    }
    return sim.pollutionAverage == 0;
}

const ToolDef *tool_for_tile(int tile)
{
    for (int i = 0; i < kToolCount; ++i) {
        if (kTools[i].special_tile == tile) {
            return &kTools[i];
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    const int lot = special_datacenter_lot_index();
    const int built = special_datacenter_index();
    const int micro = special_micro_nuclear_index();
    if (lot < 0 || built < 0 || micro < 0) {
        return fail(1, "a Data Center or micro nuclear row is missing");
    }

    // Prices and footprints.
    if (special_building(lot).cost != 400 || special_building(built).cost != 400 ||
        special_building(micro).cost != 1000) {
        return fail(2, "Data Center should cost $400 and micro nuclear $1,000");
    }
    const ToolDef *dc_tool = tool_for_tile(SPECIAL_DATACENTER_LOT);
    const ToolDef *mn_tool = tool_for_tile(SPECIAL_MICRONUKE);
    if (dc_tool == nullptr || mn_tool == nullptr || dc_tool->cost != 400 || mn_tool->cost != 1000) {
        return fail(3, "toolbar prices do not match");
    }
    if (std::string(dc_tool->hint).find("$400") == std::string::npos ||
        std::string(mn_tool->hint).find("$1,000") == std::string::npos) {
        return fail(4, "tooltips do not show the prices");
    }
    if (special_building(lot).width != 3 || special_building(lot).height != 3 ||
        special_building(micro).width != 1 || special_building(micro).height != 1) {
        return fail(5, "wrong footprints");
    }
    if (special_building(lot).center_tile != SPECIAL_DATACENTER_LOT ||
        special_building(built).center_tile != SPECIAL_DATACENTER ||
        special_building(micro).center_tile != SPECIAL_MICRONUKE) {
        return fail(6, "wrong centre tiles");
    }
    if (lot < special_menu_count() || built < special_menu_count() || micro < special_menu_count()) {
        return fail(7, "the toolbar buildings should not be in the Landmarks dropdown");
    }
    if (special_building_count() > kSpecialSpriteSlots) {
        return fail(8, "too many special sprites for the slots");
    }

    // A Data Center lot, charged $400. Unpowered it stays a vacant lot and
    // counts as nothing.
    std::string why;
    {
        Micropolis sim;
        blank_city(sim);
        const long long before = sim.totalFunds;
        if (!place(sim, lot, 50, 50, why)) {
            return fail(10, why);
        }
        if (before - sim.totalFunds != 400) {
            return fail(11, "placing a Data Center did not charge $400");
        }
        settle(sim);
        if ((sim.map[50][50] & LOMASK) != SPECIAL_DATACENTER_LOT) {
            return fail(12, "an unpowered Data Center lot opened");
        }
        if (sim.comZonePop != 0 || sim.indZonePop != 0 || sim.comPop != 0 || sim.indPop != 0) {
            return fail(13, "an unpowered Data Center counted as a zone");
        }
        if (!no_pollution(sim)) {
            return fail(9, "a vacant Data Center lot made pollution");
        }
    }

    // A lot placed where the last power scan saw power (here a powered
    // zone that was bulldozed) does not open on that stale grid.
    {
        Micropolis sim;
        blank_city(sim);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                sim.powerGridMap.worldSet(60 + dx, 60 + dy, 1);
            }
        }
        if (!place(sim, lot, 60, 60, why)) {
            return fail(43, why);
        }
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (sim.powerGridMap.worldGet(60 + dx, 60 + dy) != 0) {
                    return fail(44, "a new lot kept the stale power grid");
                }
            }
        }
        settle(sim);
        if ((sim.map[60][60] & LOMASK) != SPECIAL_DATACENTER_LOT) {
            return fail(45, "a lot opened on a stale power grid");
        }
    }

    // Powered by a micro plant on a wire: the lot opens, counts as one
    // industrial and one commercial zone, and the city stays clean.
    {
        Micropolis sim;
        blank_city(sim);
        if (!place(sim, lot, 50, 50, why)) {
            return fail(14, why);
        }
        const long long before = sim.totalFunds;
        if (!place(sim, micro, 44, 50, why)) {
            return fail(15, why);
        }
        if (before - sim.totalFunds != 1000) {
            return fail(16, "placing a micro nuclear plant did not charge $1,000");
        }
        for (int x = 45; x <= 48; ++x) {
            if (sim.toolDown(TOOL_WIRE, static_cast<short>(x), 50) != TOOLRESULT_OK) {
                return fail(17, "could not run the wire");
            }
        }
        settle(sim);
        settle(sim);
        if ((sim.map[50][50] & LOMASK) != SPECIAL_DATACENTER || (sim.map[50][50] & PWRBIT) == 0) {
            return fail(18, "a powered Data Center lot did not open");
        }
        if (sim.comZonePop != 1 || sim.indZonePop != 1) {
            std::fprintf(stderr, "com zones %d ind zones %d\n", sim.comZonePop, sim.indZonePop);
            return fail(19, "a powered Data Center is not one commercial plus one industrial zone");
        }
        if (sim.comPop != DATACENTER_ZONE_DENSITY || sim.indPop != DATACENTER_ZONE_DENSITY) {
            return fail(20, "a powered Data Center has the wrong density");
        }
        // Review nov18 B2: the jobs count for demand and tax, but nobody
        // lives in a Data Center, so it adds no population.
        if (sim.dataCenterPop != DATACENTER_ZONE_DENSITY || cityPopulationOf(sim.resPop, sim.comPop, sim.indPop, sim.dataCenterPop) != 0 ||
            special_population(&sim) != 0) {
            std::fprintf(stderr, "dc %d population %lld special %d\n", static_cast<int>(sim.dataCenterPop),
                         cityPopulationOf(sim.resPop, sim.comPop, sim.indPop, sim.dataCenterPop), special_population(&sim));
            return fail(70, "a Data Center counted as population");
        }
        if (sim.microNuclearPop != 1 || sim.nuclearPowerPop != 0) {
            return fail(21, "the micro plant was not counted");
        }
        if (sim.unpoweredZoneCount != 0) {
            return fail(22, "something is unpowered");
        }
        if (!no_pollution(sim)) {
            return fail(23, "a Data Center and a micro plant made pollution");
        }

        // A1: a damaged Data Center (a body tile lost to fire or rubble)
        // stops counting as commercial and industrial, like a landmark, and
        // its night glass stays dark (A3). Rebuilding the body restores it.
        {
            const unsigned short body = sim.map[49][49];
            sim.map[49][49] = RUBBLE | BULLBIT;
            settle(sim);
            settle(sim);
            if ((sim.map[50][50] & LOMASK) != SPECIAL_DATACENTER) {
                return fail(46, "a damaged Data Center lost its centre");
            }
            if (sim.comZonePop != 0 || sim.indZonePop != 0 || sim.comPop != 0 || sim.indPop != 0) {
                std::fprintf(stderr, "com %d ind %d\n", sim.comZonePop, sim.indZonePop);
                return fail(47, "a damaged Data Center still counts as commercial and industrial");
            }
            std::vector<int> raw(static_cast<std::size_t>(WORLD_W * WORLD_H));
            for (int y = 0; y < WORLD_H; ++y) {
                for (int x = 0; x < WORLD_W; ++x) {
                    raw[static_cast<std::size_t>(y * WORLD_W + x)] = sim.map[x][y];
                }
            }
            std::vector<night_light::MaskRows> sheet(static_cast<std::size_t>(9 * night_light::kTile), 0x0f0f);
            night_light::CellGlass glass;
            night_light::stamp_glass(raw, WORLD_W, WORLD_H, SPECIAL_DATACENTER, kSpecialBodyTile, 3, 1, sheet, glass);
            for (night_light::MaskRows row : glass) {
                if (row != 0) {
                    return fail(48, "a damaged Data Center (counted as closed) lit up at night");
                }
            }
            sim.map[49][49] = body;
            settle(sim);
            settle(sim);
            if (sim.comZonePop != 1 || sim.indZonePop != 1) {
                return fail(49, "a repaired Data Center did not count again");
            }
        }

        // Save and load: the built Data Center and the micro plant survive.
        char dir_template[] = "/tmp/lunduke-dcmicro-XXXXXX";
        char *dir = mkdtemp(dir_template);
        if (dir == nullptr) {
            return fail(24, "no scratch directory");
        }
        const std::string path = std::string(dir) + "/dc.cty";
        if (!sim.saveFile(path.c_str())) {
            return fail(25, "could not save");
        }
        Micropolis back;
        if (!back.loadFile(path.c_str())) {
            return fail(26, "could not load: " + back.loadErrorDetail);
        }
        if ((back.map[50][50] & LOMASK) != SPECIAL_DATACENTER || (back.map[44][50] & LOMASK) != SPECIAL_MICRONUKE ||
            (back.map[49][49] & LOMASK) != kSpecialBodyTile) {
            return fail(27, "the buildings did not survive save and load");
        }
        // Review nov18 B2: a powered Data Center is jobs, not people. The
        // city glance, like the census population, counts no one for it.
        const CityGlance glance = city_glance(path);
        if (!glance.ok) {
            return fail(28, "city glance could not read the file");
        }
        if (glance.population != 0) {
            std::fprintf(stderr, "glance population %lld\n", static_cast<long long>(glance.population));
            return fail(29, "city glance counted people for a Data Center");
        }
        // The vacant lot saves and loads too, and stays vacant: the scan
        // right after a load must not open it on the previous city's grid
        // (here `back`, where the built Data Center at 50,50 had power).
        Micropolis vacant;
        blank_city(vacant);
        if (!place(vacant, lot, 50, 50, why)) {
            return fail(30, why);
        }
        const std::string lot_path = std::string(dir) + "/lot.cty";
        if (!vacant.saveFile(lot_path.c_str())) {
            return fail(31, "could not save the lot");
        }
        Micropolis &lot_back = back;
        if (!lot_back.loadFile(lot_path.c_str()) || (lot_back.map[50][50] & LOMASK) != SPECIAL_DATACENTER_LOT) {
            std::fprintf(stderr, "tile %d: %s\n", lot_back.map[50][50] & LOMASK, lot_back.loadErrorDetail.c_str());
            return fail(32, "the vacant lot did not survive save and load");
        }
        settle(lot_back);
        if ((lot_back.map[50][50] & LOMASK) != SPECIAL_DATACENTER_LOT) {
            return fail(42, "an unpowered lot opened after loading");
        }
        const CityGlance empty = city_glance(lot_path);
        if (!empty.ok || empty.population != 0) {
            return fail(33, "a vacant Data Center lot counted as population");
        }
        unlink(path.c_str());
        unlink(lot_path.c_str());
        rmdir(dir);
    }

    // Capacity: one micro plant powers 2000/9 tiles; nine micro plants power
    // exactly what one nuclear plant does (2000).
    {
        Micropolis one;
        blank_city(one);
        snake_wire(one);
        one.map[1][2] = static_cast<unsigned short>(SPECIAL_MICRONUKE | ZONEBIT | CONDBIT | BURNBIT | BULLBIT);
        settle(one);
        settle(one);
        if (one.microNuclearPop != 1) {
            return fail(34, "the micro plant was not counted");
        }
        if (powered_cells(one) != kMicroNuclearPowerTiles || kMicroNuclearPowerTiles != 222) {
            std::fprintf(stderr, "powered %d\n", powered_cells(one));
            return fail(35, "one micro plant should power 222 tiles");
        }
        Micropolis nine;
        blank_city(nine);
        snake_wire(nine);
        nine.map[1][2] = static_cast<unsigned short>(SPECIAL_MICRONUKE | ZONEBIT | CONDBIT | BURNBIT | BULLBIT);
        // Eight more, off the wire; each powers only its own tile.
        for (int k = 0; k < 8; ++k) {
            nine.map[4 + k * 4][98] = static_cast<unsigned short>(SPECIAL_MICRONUKE | ZONEBIT | CONDBIT | BURNBIT | BULLBIT);
        }
        settle(nine);
        settle(nine);
        if (nine.microNuclearPop != 9) {
            return fail(36, "nine micro plants were not counted");
        }
        if (powered_cells(nine) != 2000) {
            std::fprintf(stderr, "powered %d\n", powered_cells(nine));
            return fail(37, "nine micro plants should power what one nuclear plant does");
        }

        // Assessed value: a ninth of a nuclear plant each, nine equal one.
        nine.totalPop = 1;
        nine.cityEvaluation();
        const Quad nine_value = nine.cityAssessedValue;
        nine.microNuclearPop = 0;
        nine.nuclearPowerPop = 1;
        nine.totalPop = 1;
        nine.cityEvaluation();
        if (nine_value != nine.cityAssessedValue) {
            return fail(38, "nine micro plants should be assessed like one nuclear plant");
        }
    }

    // No meltdown: with disasters on at the hardest level, a city of micro
    // plants never melts down (no radiation appears), and a forced
    // meltdown finds no plant to melt.
    {
        Micropolis sim;
        blank_city(sim);
        sim.enableDisasters = true;
        sim.gameLevel = LEVEL_HARD;
        for (int k = 0; k < 9; ++k) {
            sim.map[10 + k * 3][10] = static_cast<unsigned short>(SPECIAL_MICRONUKE | ZONEBIT | CONDBIT | BURNBIT | BULLBIT);
        }
        sim.makeMeltdown();
        for (int round = 0; round < 40; ++round) {
            settle(sim);
        }
        for (int x = 0; x < WORLD_W; ++x) {
            for (int y = 0; y < WORLD_H; ++y) {
                if ((sim.map[x][y] & LOMASK) == RADTILE) {
                    return fail(39, "a micro plant melted down");
                }
            }
        }
    }

    // Night: the glass stamps onto an intact Data Center only.
    {
        const int w = 6;
        const int h = 4;
        std::vector<int> raw(static_cast<std::size_t>(w * h), DIRT);
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                raw[static_cast<std::size_t>(dy * w + dx)] = kSpecialBodyTile;
            }
        }
        raw[static_cast<std::size_t>(1 * w + 1)] = SPECIAL_DATACENTER | ZONEBIT | PWRBIT;
        std::vector<night_light::MaskRows> sheet(static_cast<std::size_t>(9 * night_light::kTile), 0x0f0f);
        night_light::CellGlass glass;
        night_light::stamp_glass(raw, w, h, SPECIAL_DATACENTER, kSpecialBodyTile, 3, 1, sheet, glass);
        if (glass.size() != static_cast<std::size_t>(w * h * night_light::kTile) || glass[0] != 0x0f0f ||
            glass[static_cast<std::size_t>(5 * night_light::kTile)] != 0) {
            return fail(40, "the Data Center glass did not stamp");
        }
        raw[static_cast<std::size_t>(2 * w + 2)] = DIRT; // a bulldozed corner
        night_light::stamp_glass(raw, w, h, SPECIAL_DATACENTER, kSpecialBodyTile, 3, 1, sheet, glass);
        for (night_light::MaskRows row : glass) {
            if (row != 0) {
                return fail(41, "a broken Data Center still lit its glass");
            }
        }
    }

    // Night: the micro plant's atom lights (nucleus warm, electrons cool)
    // only on an intact, powered micro plant.
    {
        const int w = 3;
        const int h = 1;
        night_light::TileLamp lamp;
        lamp.tile = SPECIAL_MICRONUKE;
        lamp.pixels.assign(static_cast<std::size_t>(night_light::kTile * night_light::kTile), 0);
        const int nucleus = 6 * night_light::kTile + 9;
        const int ring = 2 * night_light::kTile + 6;
        lamp.pixels[static_cast<std::size_t>(nucleus)] = 0xFFFFA85Cu;
        lamp.pixels[static_cast<std::size_t>(ring)] = 0x7880C4F0u;
        const int on = SPECIAL_MICRONUKE | ZONEBIT | PWRBIT | CONDBIT;
        std::vector<int> raw = {on, SPECIAL_MICRONUKE | ZONEBIT | CONDBIT, RUBBLE};
        if (!night_light::lamp_lit(&lamp, raw[0]) || night_light::lamp_lit(&lamp, raw[1]) ||
            night_light::lamp_lit(&lamp, raw[2]) || night_light::lamp_lit(&lamp, SPECIAL_MICRONUKE | PWRBIT) ||
            night_light::lamp_lit(nullptr, raw[0])) {
            return fail(46, "the micro plant lamp lights on the wrong tiles");
        }
        const int kt = night_light::kTile;
        const int stride = w * kt * 4;
        std::vector<unsigned char> windows(static_cast<std::size_t>(stride * h * kt), 0);
        const std::vector<night_light::MaskRows> no_masks;
        night_light::build_windows(raw, w, h, no_masks, windows.data(), stride, nullptr, nullptr, &lamp);
        auto pixel = [&](int x, int y) {
            return *reinterpret_cast<const std::uint32_t *>(&windows[static_cast<std::size_t>(y * stride + x * 4)]);
        };
        const std::uint32_t lit_nucleus = pixel(9, 6);
        const std::uint32_t lit_ring = pixel(6, 2);
        if ((lit_nucleus >> 24) != 0xFF || ((lit_nucleus >> 16) & 0xFF) <= (lit_nucleus & 0xFF)) {
            return fail(47, "the nucleus should light warm at full strength");
        }
        if ((lit_ring >> 24) != 0x78 || (lit_ring & 0xFF) <= ((lit_ring >> 16) & 0xFF)) {
            return fail(48, "the orbit ring should light faint and cool");
        }
        if (pixel(0, 0) != 0 || pixel(kt + 9, 6) != 0 || pixel(2 * kt + 9, 6) != 0) {
            return fail(49, "an unpowered or wrecked micro plant lit up");
        }
        // The glow layer gets a pool around the powered plant only.
        const int lw = w * night_light::kPx;
        const int lstride = lw * 4;
        std::vector<unsigned char> glow(static_cast<std::size_t>(lstride * h * night_light::kPx), 0);
        night_light::build(raw, w, h, glow.data(), lstride, nullptr, nullptr, &lamp);
        auto glow_at = [&](int x, int y) {
            return *reinterpret_cast<const std::uint32_t *>(&glow[static_cast<std::size_t>(y * lstride + x * 4)]);
        };
        if ((glow_at(2, 1) >> 24) == 0) {
            return fail(50, "a powered micro plant casts no glow");
        }
        std::vector<int> dark = {raw[1], raw[1], raw[2]};
        std::vector<unsigned char> none(glow.size(), 0);
        night_light::build(dark, w, h, none.data(), lstride, nullptr, nullptr, &lamp);
        for (unsigned char byte : none) {
            if (byte != 0) {
                return fail(51, "an unpowered micro plant casts a glow");
            }
        }
    }

    std::puts("datacenter_micro ok");
    return 0;
}

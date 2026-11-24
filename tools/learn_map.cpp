// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/brookside.cty for Learn to Play. The landscape
// comes from City 2's built-in terrain generator (generateMap), the same
// path New City uses for a random map. This tool only calls that
// generator; it does not change it. Existing tiles only.
//
// Seed 6363, New City default knobs (island, rivers, lakes, trees = -1):
// a winding river, natural woods, and a large dirt clearing for the
// first tutorial road. Rebuild with:
//
//   meson compile -C build learn_map
//   build/tools/learn_map data/scenarios/brookside.cty [preview.ppm]

#include "map_builder.hpp"
#include "tutorial_goals.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int write_trimmed(Micropolis &sim, const std::string &out)
{
    if (!sim.saveFile(out.c_str(), true)) {
        std::fprintf(stderr, "learn_map: could not write %s\n", out.c_str());
        return 1;
    }
    std::ifstream in(out, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (bytes.size() < 27120) {
        std::fprintf(stderr, "learn_map: short save\n");
        return 1;
    }
    bytes.resize(27120);
    std::ofstream trimmed(out, std::ios::binary | std::ios::trunc);
    trimmed.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return trimmed.good() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output.cty> [preview.ppm]\n", argv[0]);
        return 2;
    }

    Micropolis sim;
    sim.setEnableDisasters(false);
    sim.setGameLevel(LEVEL_EASY);
    sim.terrainCreateIsland = tutorial::kMapTerrainDefault;
    sim.terrainCurveLevel = tutorial::kMapTerrainDefault;
    sim.terrainLakeLevel = tutorial::kMapTerrainDefault;
    sim.terrainTreeLevel = tutorial::kMapTerrainDefault;
    sim.generateMap(tutorial::kMapSeed);
    sim.setFunds(LEARN_TO_PLAY_FUNDS);
    sim.setCleanCityName("Brookside");
    sim.cityTime = ((1900 - 1900) * 48) + 2;

    int land = 0, water = 0, trees = 0, parks = 0;
    for (int x = 0; x < WORLD_W; ++x) {
        for (int y = 0; y < WORLD_H; ++y) {
            const MapTile t = static_cast<MapTile>(sim.map[x][y] & LOMASK);
            if (t >= WATER_LOW && t <= WATER_HIGH) {
                ++water;
            } else if (t >= TREEBASE && t <= WOODS) {
                ++trees;
            } else if (t == FOUNTAIN || (t >= WOODS2 && t <= WOODS5)) {
                ++parks;
            } else if (t == DIRT) {
                ++land;
            }
        }
    }
    if (parks != 0) {
        std::fprintf(stderr, "learn_map: starting woods spilled into park tiles\n");
        return 1;
    }
    if (land < 2000 || water < 200 || trees < 200) {
        std::fprintf(stderr, "learn_map: terrain looks wrong (land %d water %d trees %d)\n", land, water, trees);
        return 1;
    }

    auto is_dirt = [&](int x, int y) {
        return Micropolis::testBounds(x, y) && (sim.map[x][y] & LOMASK) == DIRT;
    };
    int field = 0;
    int pad = 0;
    for (int x = tutorial::kPadX0; x <= tutorial::kPadX1; ++x) {
        for (int y = tutorial::kPadY0; y <= tutorial::kPadY1; ++y) {
            ++pad;
            if (is_dirt(x, y)) {
                ++field;
            }
        }
    }
    if (field != pad || pad == 0) {
        std::fprintf(stderr, "learn_map: opening pad is not clear dirt (%d of %d)\n", field, pad);
        return 1;
    }
    for (int x = tutorial::kRoadX0; x <= tutorial::kRoadX1; ++x) {
        if (!is_dirt(x, tutorial::kRoadY)) {
            std::fprintf(stderr, "learn_map: starter road line is not dirt at %d,%d\n", x, tutorial::kRoadY);
            return 1;
        }
    }
    if (!is_dirt(tutorial::kParkX, tutorial::kParkY) || !is_dirt(tutorial::kPlantX, tutorial::kPlantY)) {
        std::fprintf(stderr, "learn_map: park or power-plant site is not dirt\n");
        return 1;
    }

    int vis_trees = 0;
    int vis_water = 0;
    int vis_tiles = 0;
    const int vx0 = tutorial::kFieldX - 34;
    const int vx1 = tutorial::kFieldX + 38;
    const int vy0 = tutorial::kFieldY - 20;
    const int vy1 = tutorial::kFieldY + 24;
    for (int x = vx0; x <= vx1; ++x) {
        for (int y = vy0; y <= vy1; ++y) {
            if (!Micropolis::testBounds(x, y)) {
                continue;
            }
            ++vis_tiles;
            const MapTile t = static_cast<MapTile>(sim.map[x][y] & LOMASK);
            if (t >= TREEBASE && t <= WOODS) {
                ++vis_trees;
            } else if (t >= WATER_LOW && t <= WATER_HIGH) {
                ++vis_water;
            }
        }
    }
    const int vis_tree_pct = vis_tiles > 0 ? (vis_trees * 100) / vis_tiles : 0;
    const int vis_water_pct = vis_tiles > 0 ? (vis_water * 100) / vis_tiles : 0;
    std::fprintf(stderr,
                 "learn_map: seed %d, knobs island/rivers/lakes/trees = %d/%d/%d/%d\n"
                 "learn_map: land %d, water %d, trees %d, pad %d dirt at (%d,%d)-(%d,%d)\n"
                 "learn_map: opening view trees %d/%d (%d%%), water %d/%d (%d%%)\n",
                 tutorial::kMapSeed, tutorial::kMapTerrainDefault, tutorial::kMapTerrainDefault,
                 tutorial::kMapTerrainDefault, tutorial::kMapTerrainDefault, land, water, trees, field,
                 tutorial::kPadX0, tutorial::kPadY0, tutorial::kPadX1, tutorial::kPadY1, vis_trees, vis_tiles,
                 vis_tree_pct, vis_water, vis_tiles, vis_water_pct);

    if (argc > 2 && argv[2][0] != '\0') {
        mapgen::write_preview(sim, argv[2]);
    }
    return write_trimmed(sim, argv[1]);
}

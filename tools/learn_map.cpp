// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/brookside.cty, the Learn to Play map: a winding
// river, a pond, woods around a friendly opening, and a large open field. Existing tiles only.
// Deterministic: no simulator random numbers.
//
//   meson compile -C build learn_map
//   build/tools/learn_map data/scenarios/brookside.cty [preview.ppm]

#include "map_builder.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using mapgen::H;
using mapgen::W;
using mapgen::in_ellipse;
using mapgen::in_line;
using mapgen::LinePoint;

// A tiny fixed hash so tree scatter never touches the sim RNG.
unsigned hash32(int x, int y, unsigned salt)
{
    unsigned h = 2166136261u ^ salt;
    h ^= static_cast<unsigned>(x * 374761393 + y * 668265263);
    h *= 16777619u;
    return h;
}

bool in_river(int x, int y)
{
    static const std::vector<LinePoint> main = {
        {0, 30, 4.0},   {22, 26, 4.2}, {40, 34, 3.6}, {58, 42, 3.8},
        {76, 48, 3.4},  {96, 52, 3.6}, {119, 56, 3.8},
    };
    static const std::vector<LinePoint> creek = {
        {78, 0, 1.6}, {74, 18, 1.6}, {70, 36, 1.8}, {68, 46, 2.0},
    };
    if (in_line(main, x, y) || in_line(creek, x, y)) {
        return true;
    }
    // A small pond that joins the river on the west bank.
    return in_ellipse(x, y, 16, 24, 6.5, 5.0);
}

// Dirt reserved for the tutorial buildings and a first wave of zones.
// Trees frame this pad but do not sit on it, so the opening view stays
// buildable and auto-bulldoze does not spend the player's first dollars.
bool in_build_pad(int x, int y)
{
    return x >= 48 && x <= 76 && y >= 51 && y <= 70;
}

bool in_woods(int x, int y)
{
    if (in_build_pad(x, y) || in_river(x, y)) {
        return false;
    }
    if (in_ellipse(x, y, 104, 14, 16, 11)) {
        return true;
    }
    if (in_ellipse(x, y, 14, 86, 15, 11)) {
        return true;
    }
    if (in_ellipse(x, y, 18, 10, 6, 5) || in_ellipse(x, y, 102, 82, 7, 5)) {
        return true;
    }
    // Copses around the opening view: west, east, and along the south edge.
    if (in_ellipse(x, y, 36, 64, 8, 7) || in_ellipse(x, y, 90, 66, 9, 7) ||
        in_ellipse(x, y, 72, 80, 8, 6) || in_ellipse(x, y, 44, 78, 6, 5)) {
        return true;
    }
    // A light scatter along the north bank, skipping the open field.
    if (y < 50 && y > 20 && x > 30 && x < 95) {
        const bool bank = in_river(x, y + 2) && !in_river(x, y) && !in_river(x, y - 1);
        if (bank && (hash32(x, y, 0x51ed) % 5) == 0) {
            return true;
        }
    }
    // Trees on the south bank, just above the build pad, so the river edge
    // in the opening view is green.
    if (y >= 44 && y <= 50 && x >= 28 && x <= 95) {
        const bool bank = in_river(x, y - 2) && !in_river(x, y);
        // Leave a clear gap around the park site at (58, 52).
        const bool park_gap = x >= 56 && x <= 60 && y >= 49;
        if (bank && !park_gap && (hash32(x, y, 0x0a11) % 3) != 0) {
            return true;
        }
    }
    // Loose trees along the west and east sides of the opening view.
    if (x >= 28 && x <= 46 && y >= 52 && y <= 76 && (hash32(x, y, 0x7e11) % 7) == 0) {
        return true;
    }
    if (x >= 78 && x <= 100 && y >= 52 && y <= 76 && (hash32(x, y, 0x33a0) % 6) == 0) {
        return true;
    }
    return false;
}

char terrain_at(int x, int y)
{
    if (in_river(x, y)) {
        return 'W';
    }
    if (in_woods(x, y)) {
        return 'T';
    }
    return 'L';
}

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
    mapgen::Builder b;
    b.name = "learn_map";
    b.terrain_at = terrain_at;
    b.sim.setEnableDisasters(false);
    b.sim.setGameLevel(LEVEL_EASY);
    b.sim.setFunds(LEARN_TO_PLAY_FUNDS);
    b.sim.setCleanCityName("Brookside");
    b.sim.cityTime = ((1900 - 1900) * 48) + 2;
    b.paint_terrain();

    int land = 0, water = 0, trees = 0, parks = 0;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            const MapTile t = b.tile(x, y);
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
    if (land < 4000 || water < 200 || trees < 200) {
        std::fprintf(stderr, "learn_map: terrain looks wrong (land %d water %d trees %d)\n", land, water, trees);
        return 1;
    }
    // The reserved pad south of the river must stay buildable dirt.
    int field = 0;
    int pad = 0;
    for (int x = 48; x <= 76; ++x) {
        for (int y = 51; y <= 70; ++y) {
            ++pad;
            if (b.dirt(x, y)) {
                ++field;
            }
        }
    }
    if (field < 400 || field != pad) {
        std::fprintf(stderr, "learn_map: open field is too tight or has non-dirt tiles (%d of %d)\n", field, pad);
        return 1;
    }
    std::fprintf(stderr, "learn_map: land %d, water %d, trees %d, field %d\n", land, water, trees, field);

    if (argc > 2 && argv[2][0] != '\0') {
        mapgen::write_preview(b.sim, argv[2]);
    }
    return write_trimmed(b.sim, argv[1]);
}

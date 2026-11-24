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
#include <cstddef>
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

    const auto grove = [](int x, int y, int cx, int cy, int rx, int ry, unsigned salt) {
        const unsigned h = hash32(x, y, salt);
        const double rx2 = static_cast<double>(rx) + static_cast<int>(h % 5) - 2;
        const double ry2 = static_cast<double>(ry) + static_cast<int>((h / 5u) % 5) - 2;
        if (rx2 <= 0.5 || ry2 <= 0.5) {
            return false;
        }
        return in_ellipse(x, y, cx, cy, rx2, ry2);
    };

    // Dense, irregular fill of the four map corners and the far edges.
    if (grove(x, y, 10, 8, 16, 12, 0x21u) || grove(x, y, 22, 6, 10, 8, 0x22u) || grove(x, y, 8, 22, 10, 10, 0x23u) ||
        grove(x, y, 110, 8, 16, 12, 0x24u) || grove(x, y, 98, 6, 12, 8, 0x25u) || grove(x, y, 112, 24, 10, 10, 0x26u) ||
        grove(x, y, 10, 92, 16, 12, 0x27u) || grove(x, y, 24, 94, 10, 8, 0x28u) || grove(x, y, 8, 78, 10, 10, 0x29u) ||
        grove(x, y, 110, 92, 16, 12, 0x2au) || grove(x, y, 96, 94, 12, 8, 0x2bu) || grove(x, y, 112, 78, 10, 10, 0x2cu)) {
        return true;
    }
    if ((x <= 10 || x >= 109 || y <= 6 || y >= 93) && (hash32(x, y, 0x2du) % 5) < 4) {
        return true;
    }

    // Opening-view copses of mixed sizes, kept off the reserved field.
    if (grove(x, y, 36, 64, 7, 6, 0x31u) || grove(x, y, 30, 58, 5, 4, 0x32u) || grove(x, y, 40, 74, 6, 5, 0x33u) ||
        grove(x, y, 28, 70, 4, 4, 0x34u) || grove(x, y, 24, 80, 6, 5, 0x35u) || grove(x, y, 42, 80, 5, 4, 0x36u) ||
        grove(x, y, 32, 50, 5, 4, 0x37u) || grove(x, y, 22, 54, 4, 3, 0x38u) || grove(x, y, 38, 44, 4, 3, 0x39u)) {
        return true;
    }
    if (grove(x, y, 90, 64, 7, 6, 0x41u) || grove(x, y, 96, 58, 5, 4, 0x42u) || grove(x, y, 86, 74, 6, 5, 0x43u) ||
        grove(x, y, 98, 70, 4, 4, 0x44u) || grove(x, y, 102, 80, 6, 5, 0x45u) || grove(x, y, 84, 80, 5, 4, 0x46u) ||
        grove(x, y, 94, 50, 5, 4, 0x47u) || grove(x, y, 104, 54, 4, 3, 0x48u) || grove(x, y, 80, 54, 4, 3, 0x49u)) {
        return true;
    }
    if (grove(x, y, 58, 80, 6, 5, 0x51u) || grove(x, y, 70, 82, 5, 4, 0x52u) || grove(x, y, 50, 84, 6, 5, 0x53u) ||
        grove(x, y, 78, 78, 5, 4, 0x54u) || grove(x, y, 44, 78, 4, 3, 0x55u) || grove(x, y, 64, 88, 5, 4, 0x56u) ||
        grove(x, y, 82, 86, 4, 3, 0x57u) || grove(x, y, 54, 76, 4, 3, 0x58u)) {
        return true;
    }
    if (grove(x, y, 40, 34, 6, 5, 0x61u) || grove(x, y, 70, 32, 6, 5, 0x62u) || grove(x, y, 54, 30, 5, 4, 0x63u) ||
        grove(x, y, 86, 36, 5, 4, 0x64u) || grove(x, y, 28, 32, 5, 4, 0x65u) || grove(x, y, 98, 30, 5, 4, 0x66u) ||
        grove(x, y, 16, 36, 4, 3, 0x67u)) {
        return true;
    }

    // Scattered small groves (2x2 / 3x3 stamps) around the opening, off the pad.
    if ((x >= 18 && x <= 46 && y >= 44 && y <= 90) || (x >= 80 && x <= 108 && y >= 44 && y <= 90) ||
        (x >= 46 && x <= 80 && y >= 72 && y <= 94) || (x >= 16 && x <= 104 && y >= 20 && y <= 42)) {
        const int sx = (x / 3) * 3;
        const int sy = (y / 3) * 3;
        if ((hash32(sx, sy, 0x73u) % 7) == 0) {
            const int span = 1 + static_cast<int>(hash32(sx, sy, 0x75u) % 2); // 1 or 2 => 2x2 or 3x3
            if (x >= sx && x <= sx + span && y >= sy && y <= sy + span && !in_build_pad(sx, sy) &&
                !in_river(sx, sy)) {
                return true;
            }
        }
    }

    // River-bank belt, 1-3 tiles wide with noisy width, leaving a gap at
    // the teaching park site north of the field.
    const bool park_gap = x >= 56 && x <= 60 && y >= 49 && y <= 52;
    if (!park_gap) {
        int bank_d = 0;
        for (int d = 1; d <= 3; ++d) {
            if (in_river(x, y - d) || in_river(x, y + d) || in_river(x - d, y) || in_river(x + d, y) ||
                in_river(x - d, y - d) || in_river(x + d, y - d) || in_river(x - d, y + d) || in_river(x + d, y + d)) {
                bank_d = d;
                break;
            }
        }
        if (bank_d == 1) {
            return true;
        }
        if (bank_d == 2 && (hash32(x, y, 0x51edu) % 5) < 4) {
            return true;
        }
        if (bank_d == 3 && (hash32(x, y, 0x0a11u) % 3) != 0) {
            return true;
        }
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

    // Knock back plus-shaped one-tile stubs left by compact ellipses and
    // the cardinal autotile table, then re-run edge smoothing.
    auto is_tree = [&](int x, int y) {
        if (!Micropolis::testBounds(x, y)) {
            return false;
        }
        const MapTile t = b.tile(x, y);
        return t >= TREEBASE && t <= WOODS;
    };
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<int> drop;
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                if (!is_tree(x, y)) {
                    continue;
                }
                const int n = (is_tree(x, y - 1) ? 1 : 0) + (is_tree(x, y + 1) ? 1 : 0) + (is_tree(x - 1, y) ? 1 : 0) +
                              (is_tree(x + 1, y) ? 1 : 0);
                if (n < 2) {
                    drop.push_back(y * W + x);
                }
            }
        }
        if (drop.empty()) {
            break;
        }
        for (int at : drop) {
            b.sim.map[at % W][at / W] = DIRT;
        }
    }
    {
        std::vector<unsigned short> tiles(static_cast<std::size_t>(W * H), DIRT);
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                tiles[static_cast<std::size_t>(y * W + x)] = static_cast<unsigned short>(b.sim.map[x][y] & LOMASK);
            }
        }
        terrain_paint::fix_edges(tiles, W, H, 0, 0, W - 1, H - 1);
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                if (in_build_pad(x, y)) {
                    b.sim.map[x][y] = DIRT;
                } else {
                    b.sim.map[x][y] = tiles[static_cast<std::size_t>(y * W + x)];
                }
            }
        }
    }

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
    int vis_trees = 0;
    int vis_tiles = 0;
    for (int x = 24; x < 97; ++x) {
        for (int y = 38; y < 83; ++y) {
            ++vis_tiles;
            const MapTile t = b.tile(x, y);
            if (t >= TREEBASE && t <= WOODS) {
                ++vis_trees;
            }
        }
    }
    const int vis_pct = vis_tiles > 0 ? (vis_trees * 100) / vis_tiles : 0;
    std::fprintf(stderr, "learn_map: land %d, water %d, trees %d, field %d, view-green %d/%d (%d%%)\n", land, water,
                 trees, field, vis_trees, vis_tiles, vis_pct);

    if (argc > 2 && argv[2][0] != '\0') {
        mapgen::write_preview(b.sim, argv[2]);
    }
    return write_trimmed(b.sim, argv[1]);
}

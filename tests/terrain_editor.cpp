// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Terrain editor: the brush lays each terrain kind, redraws shores and
// forest edges with the generator's tables, and a painted map is the land
// a new city starts on.

#include "city_session.hpp"
#include "terrain_history.hpp"
#include "terrain_paint.hpp"

#include "micropolis.h"

#include <cstdio>
#include <vector>

namespace {

constexpr int kW = CitySession::kWorldW;
constexpr int kH = CitySession::kWorldH;

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

unsigned short tile_at(const std::vector<unsigned short> &tiles, int x, int y)
{
    return tiles[static_cast<std::size_t>(y * kW + x)];
}

std::vector<unsigned short> generated(int seed, int island, int rivers, int lakes, int trees)
{
    CitySession scratch;
    std::vector<unsigned short> tiles;
    scratch.preview_terrain(seed, island, rivers, lakes, trees, tiles);
    return tiles;
}

} // namespace

int main()
{
    using terrain_paint::Piece;

    // 1. Running the edge fixer over a generated map: every water tile,
    // shore and map edge included, comes out exactly as the generator drew
    // it. Forest edges follow the generator's table too; the only tiles that
    // change are the few its in-place pass left facing a tree it cleared
    // later in the same pass. A second run changes nothing.
    {
        struct Case {
            int seed, island, rivers, lakes, trees;
        };
        const Case cases[] = {
            {42, -1, -1, -1, -1}, {7, 1, -1, -1, -1},  {1234, 0, 200, 20, 197}, {99, -1, 0, 0, 197},
            {5150, 1, 200, -1, -1}, {5, -1, -1, -1, -1}, {8, 0, -1, -1, -1},
        };
        for (const Case &c : cases) {
            const std::vector<unsigned short> base = generated(c.seed, c.island, c.rivers, c.lakes, c.trees);
            if (base.size() != static_cast<std::size_t>(kW * kH)) {
                return fail(1, "no generated map");
            }
            std::vector<unsigned short> again = base;
            terrain_paint::fix_edges(again, kW, kH, 0, 0, kW - 1, kH - 1);
            int trees_moved = 0;
            for (std::size_t i = 0; i < base.size(); ++i) {
                if (base[i] == again[i]) {
                    continue;
                }
                if (!terrain_paint::is_tree(base[i]) || !terrain_paint::is_tree(again[i])) {
                    std::fprintf(stderr, "seed %d: tile %d,%d changed %d -> %d\n", c.seed, static_cast<int>(i) % kW,
                                 static_cast<int>(i) / kW, base[i], again[i]);
                    return fail(2, "edge fixer changed generated water or land");
                }
                ++trees_moved;
            }
            if (trees_moved > kW * kH / 200) {
                return fail(3, "edge fixer redrew too much generated forest");
            }
            std::vector<unsigned short> third = again;
            terrain_paint::fix_edges(third, kW, kH, 0, 0, kW - 1, kH - 1);
            if (third != again) {
                return fail(4, "edge fixer is not settled after one run");
            }
        }
    }

    // 2. A 3x3 forest on clear land: the generator's forest-edge table,
    // with the alternate drawing on odd squares.
    {
        std::vector<unsigned short> tiles(static_cast<std::size_t>(kW * kH), DIRT);
        const int changed = terrain_paint::paint(tiles, kW, kH, 11, 11, Piece::Trees, terrain_paint::kBrushLarge);
        if (changed != 9) {
            return fail(10, "3x3 forest did not change nine tiles");
        }
        const int want[3][3] = {{36, 21, 30}, {27, 37, 23}, {34, 25, 32}};
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                const unsigned short raw = tile_at(tiles, 10 + dx, 10 + dy);
                if ((raw & LOMASK) != want[dy][dx] || (raw & BLBNBIT) != BLBNBIT) {
                    std::fprintf(stderr, "forest %d,%d is %d\n", 10 + dx, 10 + dy, raw);
                    return fail(11, "forest edge does not match the generator table");
                }
            }
        }
        if (tile_at(tiles, 9, 11) != DIRT || tile_at(tiles, 13, 11) != DIRT) {
            return fail(12, "forest brush spilled outside its square");
        }
        // The same paint again changes nothing.
        if (terrain_paint::paint(tiles, kW, kH, 11, 11, Piece::Trees, terrain_paint::kBrushLarge) != 0) {
            return fail(13, "repainting forest changed tiles");
        }
        // A single tree stays: the generator's second pass would clear it,
        // but the brush keeps it like the engine's forest tool does.
        terrain_paint::paint(tiles, kW, kH, 40, 40, Piece::Trees, terrain_paint::kBrushSmall);
        if ((tile_at(tiles, 40, 40) & LOMASK) != WOODS) {
            return fail(14, "a lone painted tree did not stay");
        }
    }

    // 3. A 3x3 pond on clear land: smoothRiver's shore table, open water
    // in the middle, either drawing of each edge.
    {
        std::vector<unsigned short> tiles(static_cast<std::size_t>(kW * kH), DIRT);
        terrain_paint::paint(tiles, kW, kH, 21, 21, Piece::Water, terrain_paint::kBrushLarge);
        const int want[3][3] = {{19, 5, 7}, {17, RIVER, 9}, {15, 13, 11}};
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                const unsigned short raw = tile_at(tiles, 20 + dx, 20 + dy);
                const int tile = raw & LOMASK;
                const int base = want[dy][dx];
                const bool ok = base == RIVER ? raw == RIVER
                                              : ((tile == base || tile == base + 1) && (raw & BULLBIT) != 0);
                if (!ok) {
                    std::fprintf(stderr, "pond %d,%d is %d\n", 20 + dx, 20 + dy, raw);
                    return fail(20, "shore does not match the generator table");
                }
            }
        }
        // Clear land over the east shore tile: the open water west of it
        // gets a shore of its own.
        terrain_paint::paint(tiles, kW, kH, 22, 21, Piece::Ground, terrain_paint::kBrushSmall);
        if (tile_at(tiles, 22, 21) != DIRT) {
            return fail(21, "ground did not replace water");
        }
        if (tile_at(tiles, 21, 21) == RIVER || !terrain_paint::is_water(tile_at(tiles, 21, 21))) {
            return fail(22, "water beside new land kept no shore");
        }
        // Channel is laid as is and never given an edge.
        terrain_paint::paint(tiles, kW, kH, 21, 21, Piece::Channel, terrain_paint::kBrushSmall);
        if (tile_at(tiles, 21, 21) != CHANNEL) {
            return fail(23, "channel was not laid");
        }
        // Water over forest: the trees around it redraw their edges.
        std::vector<unsigned short> wood(static_cast<std::size_t>(kW * kH), DIRT);
        terrain_paint::paint(wood, kW, kH, 51, 51, Piece::Trees, terrain_paint::kBrushLarge);
        terrain_paint::paint(wood, kW, kH, 52, 51, Piece::Water, terrain_paint::kBrushSmall);
        if (!terrain_paint::is_water(tile_at(wood, 52, 51)) || (tile_at(wood, 51, 51) & LOMASK) == WOODS) {
            return fail(24, "forest next to new water kept its solid middle");
        }
    }

    // 4. The brush is clipped to the map, and kinds read back.
    {
        std::vector<unsigned short> tiles(static_cast<std::size_t>(kW * kH), DIRT);
        const int corner = terrain_paint::paint(tiles, kW, kH, 0, 0, Piece::Water, terrain_paint::kBrushLarge);
        if (corner != 4) {
            return fail(30, "a corner 3x3 brush did not paint the four tiles on the map");
        }
        if (terrain_paint::paint(tiles, kW, kH, -5, -5, Piece::Water, terrain_paint::kBrushLarge) != 0) {
            return fail(31, "a brush off the map painted");
        }
        terrain_paint::paint(tiles, kW, kH, kW - 1, kH - 1, Piece::Trees, terrain_paint::kBrushLarge);
        if (terrain_paint::piece_of(tile_at(tiles, kW - 1, kH - 1)) != Piece::Trees ||
            terrain_paint::piece_of(tile_at(tiles, 0, 0)) != Piece::Water ||
            terrain_paint::piece_of(CHANNEL) != Piece::Channel || terrain_paint::piece_of(DIRT) != Piece::Ground) {
            return fail(32, "piece_of misread a tile");
        }
        std::vector<unsigned short> wrong(10, DIRT);
        if (terrain_paint::paint(wrong, kW, kH, 1, 1, Piece::Water, 1) != 0) {
            return fail(33, "a wrong-size map was painted");
        }
        for (int i = 0; i < terrain_paint::kPieceCount; ++i) {
            const auto piece = static_cast<Piece>(i);
            if (terrain_paint::piece_name(piece)[0] == '\0' || terrain_paint::piece_hint(piece)[0] == '\0' ||
                terrain_paint::piece_of(terrain_paint::piece_tile(piece)) != piece) {
                return fail(34, "a terrain piece has no name, hint or tile");
            }
        }
    }

    // 5. A painted map is the land a new city starts on. Funds, name and
    // difficulty are those of the same city unpainted.
    {
        std::vector<unsigned short> land = generated(77, -1, -1, -1, -1);
        terrain_paint::paint(land, kW, kH, 60, 50, Piece::Water, terrain_paint::kBrushLarge);
        terrain_paint::paint(land, kW, kH, 10, 10, Piece::Trees, terrain_paint::kBrushLarge);
        terrain_paint::paint(land, kW, kH, 100, 80, Piece::Channel, terrain_paint::kBrushSmall);
        CitySession::NewCitySpec spec;
        spec.name = "Painted";
        spec.seed = 77;
        spec.seed_was_set = true;
        spec.difficulty = CitySession::kLevelMedium;
        CitySession plain;
        plain.new_city(spec);
        spec.terrain = land;
        CitySession painted;
        painted.new_city("Before", 5);
        painted.new_city(spec);
        if (painted.terrain_tiles() != land) {
            return fail(40, "the new city is not on the painted land");
        }
        if (painted.funds() != plain.funds() || painted.city_name() != plain.city_name() ||
            painted.funds() != CitySession::starting_funds(CitySession::kLevelMedium)) {
            return fail(41, "painted land changed the new city's funds or name");
        }
        // A map of the wrong size is ignored: the seed's own land.
        spec.terrain.assign(12, DIRT);
        CitySession odd;
        odd.new_city(spec);
        if (odd.terrain_tiles() != plain.terrain_tiles()) {
            return fail(42, "a wrong-size painted map was used");
        }
        // The editor's own session takes painted land and reads it back.
        CitySession editor;
        editor.new_city("Editor", 77);
        const unsigned serial = editor.map_serial();
        editor.set_terrain_tiles(land);
        if (editor.terrain_tiles() != land || editor.map_serial() == serial) {
            return fail(43, "set_terrain_tiles did not replace the map");
        }
        editor.set_terrain_tiles(std::vector<unsigned short>(3, DIRT));
        if (editor.terrain_tiles() != land) {
            return fail(44, "a wrong-size set_terrain_tiles was used");
        }
    }

    // 6. Undo and redo, one step per brush stroke, the way the editor
    // drives TerrainHistory: begin_stroke on button down, a snapshot before
    // the stroke's first change, Ctrl+Z / Ctrl+Y.
    {
        std::vector<unsigned short> work(static_cast<std::size_t>(kW * kH), DIRT);
        const std::vector<unsigned short> opened = work;
        TerrainHistory history;
        auto stroke = [&](int x0, int x1, int y, Piece piece, int size) {
            history.begin_stroke();
            for (int x = x0; x <= x1; ++x) {
                const bool first = history.wants_snapshot();
                std::vector<unsigned short> before;
                if (first) {
                    before = work;
                }
                if (terrain_paint::paint(work, kW, kH, x, y, piece, size) > 0 && first) {
                    history.record(std::move(before));
                }
            }
        };
        stroke(10, 20, 30, Piece::Water, terrain_paint::kBrushLarge);
        const std::vector<unsigned short> after_one = work;
        stroke(40, 45, 50, Piece::Trees, terrain_paint::kBrushSmall);
        const std::vector<unsigned short> after_two = work;
        if (history.undo_steps() != 2 || after_one == opened || after_two == after_one) {
            return fail(50, "two strokes did not make two undo steps");
        }
        // A click that changes nothing (water on water) is not a step.
        stroke(15, 15, 30, Piece::Water, terrain_paint::kBrushSmall);
        if (history.undo_steps() != 2 || work != after_two) {
            return fail(51, "a stroke that changed nothing made an undo step");
        }
        if (!history.undo(work) || work != after_one || !history.undo(work) || work != opened) {
            return fail(52, "undo did not put the map back stroke by stroke");
        }
        if (history.undo(work) || work != opened) {
            return fail(53, "undo with nothing left changed the map");
        }
        if (!history.redo(work) || work != after_one || !history.redo(work) || work != after_two ||
            history.redo(work)) {
            return fail(54, "redo did not lay the strokes again");
        }
        // A new stroke after an undo drops what could be redone.
        history.undo(work);
        stroke(60, 62, 20, Piece::Channel, terrain_paint::kBrushSmall);
        if (history.can_redo() || history.undo_steps() != 2) {
            return fail(55, "a new stroke kept the redo steps");
        }
        // The stack is bounded: the oldest steps fall off.
        TerrainHistory capped;
        std::vector<unsigned short> map(static_cast<std::size_t>(kW * kH), DIRT);
        for (int i = 0; i < 40; ++i) {
            capped.begin_stroke();
            std::vector<unsigned short> before = map;
            terrain_paint::paint(map, kW, kH, 2 + i * 2, 2, Piece::Trees, terrain_paint::kBrushSmall);
            capped.record(std::move(before));
        }
        int undone = 0;
        while (capped.undo(map)) {
            ++undone;
        }
        if (undone != static_cast<int>(TerrainHistory::kMaxSteps) || capped.undo_steps() != 0 ||
            capped.redo_steps() != TerrainHistory::kMaxSteps) {
            return fail(56, "the undo stack is not bounded at kMaxSteps");
        }
    }
    return 0;
}

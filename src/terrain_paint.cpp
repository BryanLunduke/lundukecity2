// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "terrain_paint.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cstddef>

namespace terrain_paint {

namespace {

// West, south, east, north: the order the generator reads neighbours in.
constexpr int kDx[4] = {-1, 0, 1, 0};
constexpr int kDy[4] = {0, 1, 0, -1};

std::size_t at(int w, int x, int y)
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
}

// Micropolis::smoothRiver counts a neighbour as water when it is neither
// clear land nor forest. Off the map counts as land there too.
bool shore_wet(unsigned short raw)
{
    const int tile = raw & LOMASK;
    return tile != DIRT && (tile < WOODS_LOW || tile > WOODS_HIGH);
}

bool shore_matches(int tile, unsigned short want)
{
    const int base = want & LOMASK;
    return tile == base || (want != RIVER && tile == base + 1);
}

void fix_shore(std::vector<unsigned short> &tiles, int w, int h, int x, int y)
{
    // Micropolis::smoothRiver's REdTab, by the west-south-east-north bits.
    static const unsigned short kShore[16] = {
        13 | BULLBIT, 13 | BULLBIT, 17 | BULLBIT, 15 | BULLBIT, 5 | BULLBIT, RIVER,        19 | BULLBIT, 17 | BULLBIT,
        9 | BULLBIT,  11 | BULLBIT, RIVER,        13 | BULLBIT, 7 | BULLBIT, 9 | BULLBIT,  5 | BULLBIT,  RIVER,
    };
    const unsigned short now = tiles[at(w, x, y)];
    const int tile = now & LOMASK;
    // Channel is the deep middle of a river; the generator never edges it.
    if (tile == CHANNEL || tile < WATER_LOW || tile > WATER_HIGH) {
        return;
    }
    // smoothRiver reads off the map as land, but only for the shore ring
    // it laid; open water running off the edge (an island's sea, a river
    // mouth) stays open. So on the outer ring either reading is right, and
    // a tile that already fits one is left alone. A changed tile there
    // reads the edge as water, so painted water runs off the map.
    int dry = 0;
    int wet = 0;
    for (int z = 0; z < 4; ++z) {
        dry <<= 1;
        wet <<= 1;
        const int nx = x + kDx[z];
        const int ny = y + kDy[z];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
            ++wet;
        } else if (shore_wet(tiles[at(w, nx, ny)])) {
            ++dry;
            ++wet;
        }
    }
    if (dry != wet && shore_matches(tile, kShore[dry & 15])) {
        return;
    }
    unsigned short next = kShore[wet & 15];
    if (next != RIVER) {
        // The generator picks one of two drawings of each edge at random.
        // Keep the one already there; a new edge takes one from its place.
        const int base = next & LOMASK;
        if (tile == base + 1 || (tile != base && (((x * 73 + y * 151) >> 2) & 1) != 0)) {
            ++next;
        }
    }
    tiles[at(w, x, y)] = next;
}

void fix_forest(std::vector<unsigned short> &tiles, int w, int h, int x, int y)
{
    // Micropolis::smoothTreesAt's table, by the west-south-east-north bits.
    static const unsigned short kForest[16] = {
        0, 0, 0, 34, 0, 0, 36, 35, 0, 32, 0, 33, 30, 31, 29, 37,
    };
    if (!is_tree(tiles[at(w, x, y)])) {
        return;
    }
    int bits = 0;
    for (int z = 0; z < 4; ++z) {
        bits <<= 1;
        const int nx = x + kDx[z];
        const int ny = y + kDy[z];
        if (nx >= 0 && ny >= 0 && nx < w && ny < h && is_tree(tiles[at(w, nx, ny)])) {
            ++bits;
        }
    }
    int next = kForest[bits & 15];
    if (next == 0) {
        // A lone tree or a one-wide line. The generator's second pass clears
        // these; the engine's own forest tool (preserve) keeps them, and so
        // does the brush, or a single painted tree would vanish.
        return;
    }
    if (next != WOODS && ((x + y) & 1) != 0) {
        next -= 8;
    }
    tiles[at(w, x, y)] = static_cast<unsigned short>(next | BLBNBIT);
}

// True when any of the eight tiles around (x, y) on the map is not water.
// Off the map does not count: channel may run off the edge (boats come in
// that way).
bool touches_land(const std::vector<unsigned short> &tiles, int w, int h, int x, int y)
{
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) {
                continue;
            }
            const int tile = tiles[at(w, nx, ny)] & LOMASK;
            if (tile < WATER_LOW || tile > WATER_HIGH) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

const char *piece_name(Piece piece)
{
    switch (piece) {
    case Piece::Ground:
        return "Ground";
    case Piece::Water:
        return "Water";
    case Piece::Channel:
        return "Channel";
    case Piece::Trees:
        return "Trees";
    }
    return "Ground";
}

const char *piece_hint(Piece piece)
{
    switch (piece) {
    case Piece::Ground:
        return "Clear land. Paints over water and trees.";
    case Piece::Water:
        return "River and lake water. Shores form on their own.";
    case Piece::Channel:
        return "Deep river channel. Boats sail in from channel at the map edge.";
    case Piece::Trees:
        return "Forest. Edges thin out on their own.";
    }
    return "";
}

unsigned short piece_tile(Piece piece)
{
    switch (piece) {
    case Piece::Ground:
        return DIRT;
    case Piece::Water:
        return RIVER;
    case Piece::Channel:
        return CHANNEL;
    case Piece::Trees:
        return static_cast<unsigned short>(WOODS | BLBNBIT);
    }
    return DIRT;
}

bool is_water(unsigned short raw)
{
    const int tile = raw & LOMASK;
    return tile >= WATER_LOW && tile <= WATER_HIGH;
}

bool is_tree(unsigned short raw)
{
    const int tile = raw & LOMASK;
    return tile >= WOODS_LOW && tile <= WOODS_HIGH;
}

Piece piece_of(unsigned short raw)
{
    if ((raw & LOMASK) == CHANNEL) {
        return Piece::Channel;
    }
    if (is_water(raw)) {
        return Piece::Water;
    }
    if (is_tree(raw)) {
        return Piece::Trees;
    }
    return Piece::Ground;
}

void fix_edges(std::vector<unsigned short> &tiles, int w, int h, int x0, int y0, int x1, int y1)
{
    if (w < 1 || h < 1 || tiles.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
        return;
    }
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w - 1, x1);
    y1 = std::min(h - 1, y1);
    // Each edge depends only on which neighbours are water (or forest), not
    // on their drawings, so one pass in any order settles the rectangle.
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            fix_shore(tiles, w, h, x, y);
            fix_forest(tiles, w, h, x, y);
        }
    }
}

int paint(std::vector<unsigned short> &tiles, int w, int h, int cx, int cy, Piece piece, int size)
{
    if (w < 1 || h < 1 || tiles.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
        return 0;
    }
    size = size >= kBrushLarge ? kBrushLarge : kBrushSmall;
    const int x0 = cx - size / 2;
    const int y0 = cy - size / 2;
    const int x1 = x0 + size - 1;
    const int y1 = y0 + size - 1;
    if (x1 < 0 || y1 < 0 || x0 >= w || y0 >= h) {
        return 0;
    }
    const std::vector<unsigned short> before(tiles.begin(), tiles.end());
    const unsigned short laid = piece_tile(piece);
    for (int y = std::max(0, y0); y <= std::min(h - 1, y1); ++y) {
        for (int x = std::max(0, x0); x <= std::min(w - 1, x1); ++x) {
            const unsigned short now = tiles[at(w, x, y)];
            // Painting a kind over itself keeps the edge already drawn there.
            if (piece_of(now) == piece) {
                continue;
            }
            tiles[at(w, x, y)] = laid;
        }
    }
    // Channel is the deep middle of a river: the generator always lays
    // water around it and never draws it a shore. So channel painted on
    // land brings a ring of water with it, and channel left touching land
    // (even corner to corner) becomes plain water, which then takes a
    // shore below. Otherwise channel on land showed flat, edgeless sides.
    if (piece == Piece::Channel) {
        for (int y = std::max(0, y0 - 1); y <= std::min(h - 1, y1 + 1); ++y) {
            for (int x = std::max(0, x0 - 1); x <= std::min(w - 1, x1 + 1); ++x) {
                if (!is_water(tiles[at(w, x, y)])) {
                    tiles[at(w, x, y)] = RIVER;
                }
            }
        }
    }
    const int ex0 = std::max(0, x0 - 2);
    const int ey0 = std::max(0, y0 - 2);
    const int ex1 = std::min(w - 1, x1 + 2);
    const int ey1 = std::min(h - 1, y1 + 2);
    for (int y = ey0; y <= ey1; ++y) {
        for (int x = ex0; x <= ex1; ++x) {
            if ((tiles[at(w, x, y)] & LOMASK) == CHANNEL && touches_land(tiles, w, h, x, y)) {
                tiles[at(w, x, y)] = RIVER;
            }
        }
    }
    // The painted square, the ring around it and the ring beyond that (a
    // channel's water ring): their edges may now face a different neighbour.
    fix_edges(tiles, w, h, ex0, ey0, ex1, ey1);
    int changed = 0;
    for (int y = ey0; y <= ey1; ++y) {
        for (int x = ex0; x <= ex1; ++x) {
            if (tiles[at(w, x, y)] != before[at(w, x, y)]) {
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace terrain_paint

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <vector>

// Terrain editor brush: paints land, water, channel or forest onto a whole
// map held in rows (index y * w + x, raw engine map words), then redraws the
// shore and forest edges around the stroke with the same tables the map
// generator uses (Micropolis::smoothRiver and smoothTreesAt).
namespace terrain_paint {

enum class Piece : int {
    Ground = 0,
    Water,
    Channel,
    Trees,
};

inline constexpr int kPieceCount = 4;

// Brush sizes the toolbar offers: one tile, or a 3x3 square.
inline constexpr int kBrushSmall = 1;
inline constexpr int kBrushLarge = 3;

// Toolbar name ("Water") and a one-line description for the status line.
const char *piece_name(Piece piece);
const char *piece_hint(Piece piece);

// The engine tile the brush lays before the edges are fixed.
unsigned short piece_tile(Piece piece);

bool is_water(unsigned short raw);
bool is_tree(unsigned short raw);
// The kind of terrain a map word is, for the editor's "under the brush" line.
Piece piece_of(unsigned short raw);

// Paints a size x size square (1 or 3) centred on (cx, cy), clipped to the
// map, and fixes the edges it touched. Channel brings a ring of water onto
// any land around it, and channel left touching land turns to plain water,
// as the generator never lets the two meet. Returns how many map words
// changed.
int paint(std::vector<unsigned short> &tiles, int w, int h, int cx, int cy, Piece piece, int size);

// Redraws shore and forest edges for every water or tree tile in the
// inclusive rectangle (clipped to the map). Other tiles are left alone.
void fix_edges(std::vector<unsigned short> &tiles, int w, int h, int x0, int y0, int x1, int y1);

} // namespace terrain_paint

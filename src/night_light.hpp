// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cstdint>
#include <vector>

// Night mode's light layer. It is built from the map words alone and drawn
// as a separate surface over the darkened map, so the tile atlases and the
// special-building art are never changed. Lit windows need power: a zone
// whose center has lost power goes dark.
namespace night_light {

// Glow-layer pixels per map tile. The glow layer holds the street lamps,
// fires, the stadium and the soft bloom around lit windows; it is scaled
// up with a bilinear filter.
constexpr int kPx = 4;

// The window layer is at full tile-art resolution: one light pixel per
// atlas pixel, so a lit window sits exactly on a window in the art.
constexpr int kTile = 16;

// A tile's window mask: bit x of rows[y] is set when atlas pixel (x, y)
// is a window. Masks for a whole atlas are kept as 16 rows per tile.
using MaskRows = std::uint16_t;

enum class Glow { None, Home, Shop, Works, Civic, Plant, Stadium, Port, Fire, Road };

Glow glow_for_tile(int tile);

// Same value for the same tile and salt on every run and platform.
inline std::uint32_t hash(int x, int y, int salt)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^
                      static_cast<std::uint32_t>(salt) * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

// True for building tiles whose art can have lit windows. Animated
// chimney and smoke frames are left out so the windows do not flicker.
bool wants_windows(int tile);

// Finds the windows in one 16x16 tile of art (native ARGB32, row-major):
// a small dark pane with lighter facade on all four sides, the dotted
// window grids the Classic and SVGA sheets draw on their buildings.
// Grass, trees and dirt are never windows. Fills rows[0..15].
void window_mask(const std::uint32_t *tile_pixels, MaskRows rows[kTile]);

// Masks for every tile, 16 rows per tile, from a packed tile sheet
// (16*16 pixels per tile, `tiles` tiles). Tiles that do not want windows
// get an empty mask.
void window_masks(const std::uint32_t *sheet, int tiles, std::vector<MaskRows> &out);

// Fills the glow layer `out` (premultiplied ARGB32, width w*kPx, height
// h*kPx, `stride` bytes per row) from the raw map words in `raw`
// (row-major, w*h). With `masks`, lit windows add a faint bloom.
void build(const std::vector<int> &raw, int w, int h, unsigned char *out, int stride,
           const std::vector<MaskRows> *masks = nullptr);

// Fills the window layer `out` (premultiplied ARGB32, width w*kTile,
// height h*kTile). Only mask pixels of powered buildings light, and each
// building lights its own share of windows, floor by floor. `keys` (may
// be null) remembers what each tile was drawn from, so a later call only
// repaints tiles that changed. Returns the number of tiles repainted.
int build_windows(const std::vector<int> &raw, int w, int h, const std::vector<MaskRows> &masks, unsigned char *out,
                  int stride, std::vector<int> *keys = nullptr);

// Flattens the two layers into `out` at window-layer size (w*kTile by
// h*kTile): the glow scaled up 4x with a bilinear filter (the same
// sampling as Cairo's) plus the windows, saturating. Rows [row0, row1)
// only, so the work can be split. All buffers are premultiplied ARGB32.
void flatten(const unsigned char *glow, int glow_stride, const unsigned char *windows, int window_stride, int w, int h,
             unsigned char *out, int out_stride, int row0, int row1);

// Whether window pixel (px, py) of the tile at (x, y) is lit, for a
// building `owner` (the zone centre index). Shared by both layers.
bool window_lit(int owner, int x, int y, int px, int py);

} // namespace night_light

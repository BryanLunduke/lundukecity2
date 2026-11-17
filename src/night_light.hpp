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

// Glass that lights as a whole, per map cell rather than per atlas tile:
// the Data Center's facade is special-building art over the shared body
// tile, so its panes cannot come from the atlas masks. `cells` holds
// kTile rows per map cell (w*h*kTile); a set bit is a glass pixel that
// lights, all of it, whenever its building has power.
using CellGlass = std::vector<MaskRows>;

// Stamps a building's glass sheet onto every intact copy of it in `raw`.
// `sheet` is span*span cells of kTile rows (row-major cells); the centre
// tile is `center_tile` at (anchor, anchor) and the other cells must be
// `body_tile`, so wreckage and a half-bulldozed lot stay dark. `out` is
// resized to w*h*kTile and cleared first when `clear` is set.
void stamp_glass(const std::vector<int> &raw, int w, int h, int center_tile, int body_tile, int span, int anchor,
                 const std::vector<MaskRows> &sheet, CellGlass &out, bool clear = true);

// A 1x1 building lit pixel by pixel from its own light sheet: the micro
// nuclear plant's atom. `pixels` is kTile*kTile straight ARGB (0xAARRGGBB,
// row-major); alpha is the light's strength and 0 is dark. It lights on
// an intact building (its centre tile with ZONEBIT) that has power.
struct TileLamp {
    int tile = -1;
    std::vector<std::uint32_t> pixels;
};

// Fills the glow layer `out` (premultiplied ARGB32, width w*kPx, height
// h*kPx, `stride` bytes per row) from the raw map words in `raw`
// (row-major, w*h). With `masks`, lit windows add a faint bloom; lit
// `glass` does too. A lit `lamp` adds a small soft pool of its own colour
// around each of its pixels.
void build(const std::vector<int> &raw, int w, int h, unsigned char *out, int stride,
           const std::vector<MaskRows> *masks = nullptr, const CellGlass *glass = nullptr,
           const TileLamp *lamp = nullptr);

// Fills the window layer `out` (premultiplied ARGB32, width w*kTile,
// height h*kTile). Only mask pixels of powered buildings light, and each
// building lights its own share of windows, floor by floor. `keys` (may
// be null) remembers what each tile was drawn from, so a later call only
// repaints tiles that changed. Returns the number of tiles repainted.
// With `glass`, every glass pixel of a powered building lights too, in a
// cool server-room white. With `lamp`, a lit lamp tile paints its sheet.
int build_windows(const std::vector<int> &raw, int w, int h, const std::vector<MaskRows> &masks, unsigned char *out,
                  int stride, std::vector<int> *keys = nullptr, const CellGlass *glass = nullptr,
                  const TileLamp *lamp = nullptr);

// True when `lamp` lights the map word `word` (its tile, ZONEBIT, PWRBIT).
bool lamp_lit(const TileLamp *lamp, int word);

// Colour of one lit glass pixel (art pixel ax, ay of the map) as 0..1 RGB.
void glass_rgb(int ax, int ay, float &r, float &g, float &b);

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

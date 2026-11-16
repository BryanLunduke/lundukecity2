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

// Light-layer pixels per map tile. The layer is scaled up with a bilinear
// filter, so each lit window becomes a small soft glow.
constexpr int kPx = 4;

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

// Fills `out` (premultiplied ARGB32, width w*kPx, height h*kPx, `stride`
// bytes per row) from the raw map words in `raw` (row-major, w*h).
void build(const std::vector<int> &raw, int w, int h, unsigned char *out, int stride);

} // namespace night_light

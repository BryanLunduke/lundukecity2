// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "night_light.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace night_light {

Glow glow_for_tile(int tile)
{
    if (tile >= FIREBASE && tile <= LASTFIRE) {
        return Glow::Fire;
    }
    if (tile >= ROADBASE && tile <= LASTROAD) {
        return Glow::Road;
    }
    if (tile >= HOUSE && tile < COMBASE) {
        return Glow::Home;
    }
    if (tile >= COMBASE + 9 && tile < INDBASE) {
        return Glow::Shop;
    }
    if ((tile >= INDBASE + 9 && tile < PORTBASE) || (tile >= INDBASE2 && tile < TINYEXP)) {
        return Glow::Works;
    }
    if (tile >= PORTBASE && tile < COALBASE) {
        return Glow::Port;
    }
    if ((tile >= COALBASE && tile < FIRESTBASE) || (tile >= NUCLEARBASE && tile <= LASTZONE)) {
        return Glow::Plant;
    }
    if (tile >= FIRESTBASE && tile < STADIUMBASE) {
        return Glow::Civic;
    }
    if (tile >= STADIUMBASE && tile < NUCLEARBASE) {
        return Glow::Stadium;
    }
    return Glow::None;
}

namespace {

// Tiles a powered zone center lights, as [first, last] around the center.
// 3x3 zones sit one tile around the center; 4x4 and 6x6 buildings put the
// center one tile in from the top-left corner.
int zone_reach(int tile)
{
    if (tile >= AIRPORTBASE && tile < COALBASE) {
        return 4;
    }
    if ((tile >= PORTBASE && tile < AIRPORTBASE) || (tile >= COALBASE && tile < FIRESTBASE) ||
        (tile >= STADIUMBASE && tile <= LASTZONE)) {
        return 2;
    }
    return 1;
}

// How much one lit pane adds to the window bloom.
constexpr float kBloom = 0.55f;

struct Rgb {
    float r;
    float g;
    float b;
};

const Rgb kWarm{1.00f, 0.80f, 0.48f};
const Rgb kWhite{0.86f, 0.90f, 1.00f};
// Office light: a warm white, between home lamps and the cool civic white.
const Rgb kOffice{1.00f, 0.92f, 0.70f};
const Rgb kAmber{1.00f, 0.62f, 0.30f};

Rgb window_color(Glow kind)
{
    switch (kind) {
    case Glow::Shop:
        return kOffice;
    case Glow::Civic:
    case Glow::Port:
        return kWhite;
    case Glow::Works:
    case Glow::Plant:
        return kAmber;
    default:
        return kWarm;
    }
}

// Which tiles a powered building lights, and which building owns each
// tile (the centre's index, or the tile itself for a lone house), so a
// building's windows share one lit share.
void power_map(const std::vector<int> &raw, int w, int h, std::vector<unsigned char> &powered, std::vector<int> &owner)
{
    powered.assign(static_cast<std::size_t>(w * h), 0);
    owner.assign(static_cast<std::size_t>(w * h), -1);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int word = raw[static_cast<std::size_t>(y * w + x)];
            if (word < 0 || (word & ZONEBIT) == 0) {
                continue;
            }
            const int reach = zone_reach(word & LOMASK);
            const bool on = (word & PWRBIT) != 0;
            for (int ty = y - 1; ty <= y + reach; ++ty) {
                for (int tx = x - 1; tx <= x + reach; ++tx) {
                    if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
                        continue;
                    }
                    if (reach == 1 && (tx > x + 1 || ty > y + 1)) {
                        continue;
                    }
                    const std::size_t at = static_cast<std::size_t>(ty * w + tx);
                    if (on) {
                        powered[at] = 1;
                    }
                    owner[at] = y * w + x;
                }
            }
        }
    }
    for (int i = 0; i < w * h; ++i) {
        const int word = raw[static_cast<std::size_t>(i)];
        const int tile = word < 0 ? 0 : (word & LOMASK);
        if (tile >= HOUSE && tile < HOUSE + 12) {
            owner[static_cast<std::size_t>(i)] = i;
        }
    }
}

float luma(std::uint32_t p)
{
    const int r = static_cast<int>((p >> 16) & 0xFF);
    const int g = static_cast<int>((p >> 8) & 0xFF);
    const int b = static_cast<int>(p & 0xFF);
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

// Facade or roof, not grass, trees or bare dirt.
bool built_surface(std::uint32_t p)
{
    const int r = static_cast<int>((p >> 16) & 0xFF);
    const int g = static_cast<int>((p >> 8) & 0xFF);
    const int b = static_cast<int>(p & 0xFF);
    if (g > r + 25 && g > b + 25) {
        return false;
    }
    if (r > g + 20 && r > b + 40 && g > b + 10) {
        return false;
    }
    return true;
}

} // namespace

bool wants_windows(int tile)
{
    if (tile >= INDBASE2 || tile < HOUSE) {
        return false;
    }
    switch (glow_for_tile(tile)) {
    case Glow::Home:
    case Glow::Shop:
    case Glow::Works:
    case Glow::Civic:
    case Glow::Port:
    case Glow::Plant:
        return true;
    default:
        return false;
    }
}

void window_mask(const std::uint32_t *tile_pixels, MaskRows rows[kTile])
{
    for (int y = 0; y < kTile; ++y) {
        rows[y] = 0;
    }
    if (tile_pixels == nullptr) {
        return;
    }
    auto at = [&](int x, int y) { return tile_pixels[y * kTile + x]; };
    for (int y = 0; y < kTile; ++y) {
        for (int x = 0; x < kTile; ++x) {
            const std::uint32_t p = at(x, y);
            if (!built_surface(p)) {
                continue;
            }
            const float l = luma(p);
            // A pane is darker than the facade on every side. A dark line
            // (a roof edge or a shadow) has a dark neighbour along it and
            // is left out. Pixels on the tile edge need three sides.
            int sides = 0;
            bool pane = true;
            const int dx[4] = {-1, 1, 0, 0};
            const int dy[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4 && pane; ++k) {
                const int nx = x + dx[k];
                const int ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= kTile || ny >= kTile) {
                    continue;
                }
                const std::uint32_t n = at(nx, ny);
                if (!built_surface(n) || luma(n) - l <= 24.0f) {
                    pane = false;
                }
                ++sides;
            }
            if (pane && sides >= 3) {
                rows[y] = static_cast<MaskRows>(rows[y] | (1u << x));
            }
        }
    }
}

void window_masks(const std::uint32_t *sheet, int tiles, std::vector<MaskRows> &out)
{
    out.assign(static_cast<std::size_t>(std::max(0, tiles) * kTile), 0);
    if (sheet == nullptr) {
        return;
    }
    for (int t = 0; t < tiles; ++t) {
        if (!wants_windows(t)) {
            continue;
        }
        window_mask(sheet + static_cast<std::size_t>(t) * kTile * kTile, &out[static_cast<std::size_t>(t) * kTile]);
    }
}

void stamp_glass(const std::vector<int> &raw, int w, int h, int center_tile, int body_tile, int span, int anchor,
                 const std::vector<MaskRows> &sheet, CellGlass &out, bool clear)
{
    const std::size_t cells = static_cast<std::size_t>(std::max(0, w * h));
    if (clear || out.size() != cells * kTile) {
        out.assign(cells * kTile, 0);
    }
    if (w <= 0 || h <= 0 || span <= 0 || static_cast<int>(raw.size()) < w * h ||
        sheet.size() < static_cast<std::size_t>(span * span * kTile)) {
        return;
    }
    for (int cy = 0; cy < h; ++cy) {
        for (int cx = 0; cx < w; ++cx) {
            const int word = raw[static_cast<std::size_t>(cy * w + cx)];
            if (word < 0 || (word & ZONEBIT) == 0 || (word & LOMASK) != center_tile) {
                continue;
            }
            const int left = cx - anchor;
            const int top = cy - anchor;
            if (left < 0 || top < 0 || left + span > w || top + span > h) {
                continue;
            }
            bool intact = true;
            for (int dy = 0; dy < span && intact; ++dy) {
                for (int dx = 0; dx < span; ++dx) {
                    if (dx == anchor && dy == anchor) {
                        continue;
                    }
                    const int body = raw[static_cast<std::size_t>((top + dy) * w + left + dx)];
                    if (body < 0 || (body & LOMASK) != body_tile) {
                        intact = false;
                        break;
                    }
                }
            }
            if (!intact) {
                // A3: a damaged Data Center counts as no zone in the census
                // (zone.cpp, A1), so its windows stay dark too.
                continue;
            }
            for (int dy = 0; dy < span; ++dy) {
                for (int dx = 0; dx < span; ++dx) {
                    const MaskRows *from = &sheet[static_cast<std::size_t>((dy * span + dx) * kTile)];
                    MaskRows *to = &out[static_cast<std::size_t>(((top + dy) * w + left + dx) * kTile)];
                    for (int py = 0; py < kTile; ++py) {
                        to[py] = static_cast<MaskRows>(to[py] | from[py]);
                    }
                }
            }
        }
    }
}

void glass_rgb(int ax, int ay, float &r, float &g, float &b)
{
    // Cool white with a faint cyan, brighter and darker in two-pixel bands
    // so a lit facade reads as floors of server halls, not a flat sheet,
    // and never as a speckle.
    const float k = 0.80f + 0.16f * static_cast<float>(hash(ax / 6, ay / 2, 211) & 0xFF) / 255.0f;
    r = 0.74f * k;
    g = 0.90f * k;
    b = 1.00f * k;
}

bool window_lit(int owner, int x, int y, int px, int py)
{
    // Each building has its own share of lit floors (25-80%). A lit floor
    // (a row of panes) has most windows on and a dark floor a few, so
    // lights come in bands the way an office or apartment block does at
    // night instead of a uniform sprinkle.
    const float share = 0.25f + 0.55f * static_cast<float>(hash(owner, 0, 77) & 0xFF) / 255.0f;
    const int row = y * kTile + py;
    const bool floor_on = static_cast<float>(hash(owner, row, 91) & 0xFF) < share * 256.0f;
    const float chance = floor_on ? 0.82f : 0.10f;
    return static_cast<float>(hash(x * kTile + px, row, owner) & 0xFF) < chance * 256.0f;
}

namespace {

// The lit windows of one tile, as (px, py) pairs, for a powered building.
template <typename Fn> void each_lit_window(const std::vector<MaskRows> &masks, int tile, int owner, int x, int y, Fn fn)
{
    if (!wants_windows(tile) || static_cast<std::size_t>((tile + 1) * kTile) > masks.size()) {
        return;
    }
    const MaskRows *rows = &masks[static_cast<std::size_t>(tile) * kTile];
    // Factories, plants and docks work a thinner night shift than offices
    // and homes, so fewer of their panes light.
    const Glow kind = glow_for_tile(tile);
    const bool thin = kind == Glow::Works || kind == Glow::Plant || kind == Glow::Port;
    for (int py = 0; py < kTile; ++py) {
        const MaskRows row = rows[py];
        if (row == 0) {
            continue;
        }
        for (int px = 0; px < kTile; ++px) {
            if ((row & (1u << px)) == 0 || !window_lit(owner, x, y, px, py)) {
                continue;
            }
            if (thin && (hash(x * kTile + px, y * kTile + py, 13) & 0xFF) >= 140) {
                continue;
            }
            fn(px, py);
        }
    }
}

} // namespace

bool lamp_lit(const TileLamp *lamp, int word)
{
    return lamp != nullptr && word >= 0 && lamp->tile >= 0 && (word & LOMASK) == lamp->tile && (word & ZONEBIT) != 0 &&
           (word & PWRBIT) != 0 && lamp->pixels.size() == static_cast<std::size_t>(kTile * kTile);
}

void build(const std::vector<int> &raw, int w, int h, unsigned char *out, int stride, const std::vector<MaskRows> *masks,
           const CellGlass *glass, const TileLamp *lamp)
{
    if (w <= 0 || h <= 0 || out == nullptr || static_cast<int>(raw.size()) < w * h) {
        return;
    }
    const int lw = w * kPx;
    const int lh = h * kPx;
    std::vector<float> light(static_cast<std::size_t>(lw * lh * 3), 0.0f);
    std::vector<float> bloom;
    std::vector<unsigned char> powered;
    std::vector<int> owner;
    power_map(raw, w, h, powered, owner);
    auto add = [&](int px, int py, const Rgb &c, float k) {
        if (px < 0 || py < 0 || px >= lw || py >= lh || k <= 0.0f) {
            return;
        }
        float *cell = &light[static_cast<std::size_t>((py * lw + px) * 3)];
        cell[0] += c.r * k;
        cell[1] += c.g * k;
        cell[2] += c.b * k;
    };
    // Light pools are stamped from a falloff worked out once per radius.
    // Every pool here is centred on a glow-pixel corner, so one stamp fits
    // every placement.
    struct Stamp {
        double radius = 0.0;
        int reach = 0;
        std::vector<float> fall;
    };
    Stamp stamps[3];
    auto stamp_for = [&](double radius) -> const Stamp & {
        for (Stamp &stamp : stamps) {
            if (stamp.radius == radius) {
                return stamp;
            }
        }
        Stamp *slot = &stamps[0];
        for (Stamp &stamp : stamps) {
            if (stamp.radius == 0.0) {
                slot = &stamp;
                break;
            }
        }
        slot->radius = radius;
        slot->reach = static_cast<int>(std::ceil(radius));
        const int side = 2 * slot->reach;
        slot->fall.assign(static_cast<std::size_t>(side * side), 0.0f);
        for (int j = 0; j < side; ++j) {
            for (int i = 0; i < side; ++i) {
                const double dx = i - slot->reach + 0.5;
                const double dy = j - slot->reach + 0.5;
                const double d = std::sqrt(dx * dx + dy * dy) / radius;
                if (d < 1.0) {
                    slot->fall[static_cast<std::size_t>(j * side + i)] = static_cast<float>((1.0 - d) * (1.0 - d));
                }
            }
        }
        return *slot;
    };
    // (cx, cy) is a whole glow-pixel position: the pool's centre.
    auto glow = [&](int cx, int cy, double radius, const Rgb &c, float strength) {
        const Stamp &stamp = stamp_for(radius);
        const int side = 2 * stamp.reach;
        for (int j = 0; j < side; ++j) {
            const int py = cy - stamp.reach + j;
            if (py < 0 || py >= lh) {
                continue;
            }
            for (int i = 0; i < side; ++i) {
                const int px = cx - stamp.reach + i;
                const float k = stamp.fall[static_cast<std::size_t>(j * side + i)];
                if (k <= 0.0f || px < 0 || px >= lw) {
                    continue;
                }
                float *cell = &light[static_cast<std::size_t>((py * lw + px) * 3)];
                cell[0] += c.r * strength * k;
                cell[1] += c.g * strength * k;
                cell[2] += c.b * strength * k;
            }
        }
    };
    const Rgb white = kWhite;
    const Rgb sodium{1.00f, 0.70f, 0.36f};
    const Rgb flame{1.00f, 0.45f, 0.12f};
    const bool with_glass = glass != nullptr && glass->size() == static_cast<std::size_t>(w * h * kTile);
    if (masks != nullptr || with_glass) {
        bloom.assign(static_cast<std::size_t>(lw * lh), 0.0f);
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * w + x);
            const int word = raw[at];
            if (word < 0) {
                continue;
            }
            const int tile = word & LOMASK;
            if (lamp_lit(lamp, word)) {
                // Each lit pixel of the lamp feeds a small pool of its own
                // colour; a full-strength pixel (nucleus, electron) a clear
                // one, a faint one (orbit ring) only a breath.
                for (int py = 0; py < kTile; ++py) {
                    for (int px = 0; px < kTile; ++px) {
                        const std::uint32_t v = lamp->pixels[static_cast<std::size_t>(py * kTile + px)];
                        const float a = static_cast<float>(v >> 24) / 255.0f;
                        if (a <= 0.0f) {
                            continue;
                        }
                        const Rgb c{static_cast<float>((v >> 16) & 0xFF) / 255.0f,
                                    static_cast<float>((v >> 8) & 0xFF) / 255.0f,
                                    static_cast<float>(v & 0xFF) / 255.0f};
                        glow(x * kPx + px * kPx / kTile, y * kPx + py * kPx / kTile, kPx * 0.9, c,
                             a >= 0.99f ? 0.20f : 0.004f * a);
                    }
                }
            }
            if (with_glass && powered[at] != 0) {
                // Lit glass glows like a wall of windows: every pane feeds
                // the halo, a little less each than a lone window.
                const MaskRows *rows = &(*glass)[at * kTile];
                for (int py = 0; py < kTile; ++py) {
                    for (int px = 0; px < kTile; ++px) {
                        if ((rows[py] & (1u << px)) != 0) {
                            bloom[static_cast<std::size_t>((y * kPx + py * kPx / kTile) * lw + x * kPx +
                                                           px * kPx / kTile)] += 0.6f;
                        }
                    }
                }
            }
            const Glow kind = glow_for_tile(tile);
            if (kind == Glow::None) {
                continue;
            }
            const int bx = x * kPx;
            const int by = y * kPx;
            if (kind == Glow::Fire) {
                glow(bx + kPx / 2, by + kPx / 2, kPx * 1.6, flame, 0.95f);
                continue;
            }
            if (kind == Glow::Road) {
                // A lamp on every fourth road tile, on a diagonal lattice so
                // a straight street gets an even row of pools of light.
                if (((x + 2 * y) % 4) == 0) {
                    glow(bx + kPx / 2, by + kPx / 2, kPx * 1.3, sodium, 0.34f);
                    add(bx + kPx / 2, by + kPx / 2, sodium, 0.45f);
                }
                continue;
            }
            const bool lit = powered[at] != 0 || (word & PWRBIT) != 0;
            if (!lit) {
                continue;
            }
            if (kind == Glow::Stadium) {
                if ((word & ZONEBIT) != 0) {
                    glow(bx + kPx * 3 / 2, by + kPx * 3 / 2, kPx * 3.2, white, 0.30f);
                }
                continue;
            }
            if (masks == nullptr) {
                continue;
            }
            // Each lit pane feeds a little light into the glow cell under
            // it; the blur below spreads it into a soft halo.
            const int who = owner[at] >= 0 ? owner[at] : static_cast<int>(at);
            each_lit_window(*masks, tile, who, x, y, [&](int px, int py) {
                bloom[static_cast<std::size_t>((by + py * kPx / kTile) * lw + bx + px * kPx / kTile)] += 1.0f;
            });
        }
    }
    if (!bloom.empty()) {
        // A 7-tap [1 6 15 20 15 6 1] blur each way (three [1 2 1] passes):
        // a halo about two glow cells (eight art pixels) wide, much fainter
        // than a lamp. Empty rows are skipped; most of a map has no panes.
        static const float kTap[7] = {1 / 64.0f, 6 / 64.0f, 15 / 64.0f, 20 / 64.0f, 15 / 64.0f, 6 / 64.0f, 1 / 64.0f};
        std::vector<float> tmp(bloom.size(), 0.0f);
        std::vector<unsigned char> row_on(static_cast<std::size_t>(lh), 0);
        for (int py = 0; py < lh; ++py) {
            const float *in = &bloom[static_cast<std::size_t>(py * lw)];
            bool any = false;
            for (int px = 0; px < lw && !any; ++px) {
                any = in[px] != 0.0f;
            }
            if (!any) {
                continue;
            }
            row_on[static_cast<std::size_t>(py)] = 1;
            float *mid = &tmp[static_cast<std::size_t>(py * lw)];
            for (int px = 0; px < lw; ++px) {
                const float v = in[px];
                if (v == 0.0f) {
                    continue;
                }
                for (int t = -3; t <= 3; ++t) {
                    const int qx = px + t;
                    if (qx >= 0 && qx < lw) {
                        mid[qx] += v * kTap[t + 3];
                    }
                }
            }
        }
        const Rgb tint{1.00f, 0.86f, 0.62f};
        for (int py = 0; py < lh; ++py) {
            bool rows_close = false;
            for (int t = -3; t <= 3 && !rows_close; ++t) {
                const int qy = py + t;
                rows_close = qy >= 0 && qy < lh && row_on[static_cast<std::size_t>(qy)] != 0;
            }
            if (!rows_close) {
                continue;
            }
            float *cell = &light[static_cast<std::size_t>(py * lw * 3)];
            for (int px = 0; px < lw; ++px) {
                float sum = 0.0f;
                for (int t = -3; t <= 3; ++t) {
                    const int qy = py + t;
                    if (qy >= 0 && qy < lh && row_on[static_cast<std::size_t>(qy)] != 0) {
                        sum += tmp[static_cast<std::size_t>(qy * lw + px)] * kTap[t + 3];
                    }
                }
                if (sum <= 0.0f) {
                    continue;
                }
                const float k = std::min(0.28f, sum * kBloom);
                cell[px * 3] += tint.r * k;
                cell[px * 3 + 1] += tint.g * k;
                cell[px * 3 + 2] += tint.b * k;
            }
        }
    }
    for (int py = 0; py < lh; ++py) {
        auto *row = reinterpret_cast<std::uint32_t *>(out + py * stride);
        for (int px = 0; px < lw; ++px) {
            const float *cell = &light[static_cast<std::size_t>((py * lw + px) * 3)];
            const int r = static_cast<int>(std::min(1.0f, cell[0]) * 255.0f + 0.5f);
            const int g = static_cast<int>(std::min(1.0f, cell[1]) * 255.0f + 0.5f);
            const int b = static_cast<int>(std::min(1.0f, cell[2]) * 255.0f + 0.5f);
            const int a = std::max(r, std::max(g, b));
            row[px] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                      (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
        }
    }
}

int build_windows(const std::vector<int> &raw, int w, int h, const std::vector<MaskRows> &masks, unsigned char *out,
                  int stride, std::vector<int> *keys, const CellGlass *glass, const TileLamp *lamp)
{
    const bool with_glass = glass != nullptr && glass->size() == static_cast<std::size_t>(w * h * kTile);
    if (w <= 0 || h <= 0 || out == nullptr || static_cast<int>(raw.size()) < w * h) {
        return 0;
    }
    std::vector<unsigned char> powered;
    std::vector<int> owner;
    power_map(raw, w, h, powered, owner);
    if (keys != nullptr && static_cast<int>(keys->size()) != w * h) {
        keys->assign(static_cast<std::size_t>(w * h), -1);
    }
    int painted = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * w + x);
            const int word = raw[at];
            const int tile = word < 0 ? 0 : (word & LOMASK);
            const bool lit = word >= 0 && (powered[at] != 0 || (word & PWRBIT) != 0) && wants_windows(tile);
            const int who = owner[at] >= 0 ? owner[at] : static_cast<int>(at);
            const MaskRows *glass_rows = with_glass ? &(*glass)[at * kTile] : nullptr;
            bool glass_on = false;
            if (glass_rows != nullptr && word >= 0 && powered[at] != 0) {
                for (int py = 0; py < kTile && !glass_on; ++py) {
                    glass_on = glass_rows[py] != 0;
                }
            }
            // What this tile's windows depend on: the tile, power and owner.
            // Lit glass is keyed apart (negative) so a lot that turns into a
            // Data Center, or loses power, repaints.
            const bool lamp_on = lamp_lit(lamp, word);
            const int key = (glass_on || lamp_on) ? -(tile + 1024 * (1 + who)) - 1 : (lit ? tile + 1024 * (1 + who) : 0);
            if (keys != nullptr) {
                int &was = (*keys)[at];
                if (was == key) {
                    continue;
                }
                was = key;
            }
            ++painted;
            for (int py = 0; py < kTile; ++py) {
                auto *row = reinterpret_cast<std::uint32_t *>(out + (y * kTile + py) * stride) + x * kTile;
                std::memset(row, 0, sizeof(std::uint32_t) * kTile);
            }
            if (lamp_on) {
                for (int py = 0; py < kTile; ++py) {
                    auto *row = reinterpret_cast<std::uint32_t *>(out + (y * kTile + py) * stride) + x * kTile;
                    for (int px = 0; px < kTile; ++px) {
                        const std::uint32_t v = lamp->pixels[static_cast<std::size_t>(py * kTile + px)];
                        const std::uint32_t a = v >> 24;
                        if (a == 0) {
                            continue;
                        }
                        // Premultiplied, like every layer here.
                        const std::uint32_t r = ((v >> 16) & 0xFF) * a / 255;
                        const std::uint32_t g = ((v >> 8) & 0xFF) * a / 255;
                        const std::uint32_t b = (v & 0xFF) * a / 255;
                        row[px] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
                continue;
            }
            if (glass_on) {
                for (int py = 0; py < kTile; ++py) {
                    auto *row = reinterpret_cast<std::uint32_t *>(out + (y * kTile + py) * stride) + x * kTile;
                    for (int px = 0; px < kTile; ++px) {
                        if ((glass_rows[py] & (1u << px)) == 0) {
                            continue;
                        }
                        float fr = 0.0f;
                        float fg = 0.0f;
                        float fb = 0.0f;
                        glass_rgb(x * kTile + px, y * kTile + py, fr, fg, fb);
                        const int r = static_cast<int>(std::min(1.0f, fr) * 255.0f + 0.5f);
                        const int g = static_cast<int>(std::min(1.0f, fg) * 255.0f + 0.5f);
                        const int b = static_cast<int>(std::min(1.0f, fb) * 255.0f + 0.5f);
                        const int a = std::max(r, std::max(g, b));
                        row[px] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                                  (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
                    }
                }
            }
            if (!lit) {
                continue;
            }
            const Rgb base = window_color(glow_for_tile(tile));
            each_lit_window(masks, tile, who, x, y, [&](int px, int py) {
                // Some spread in brightness and tint so a lit facade is not
                // flat: most panes match the building, a few run warmer.
                const std::uint32_t v = hash(x * kTile + px, y * kTile + py, 5);
                const float k = 0.62f + 0.30f * static_cast<float>(v & 0xFF) / 255.0f;
                const Rgb c = ((v >> 8) & 3) == 0 ? kWarm : base;
                const int r = static_cast<int>(std::min(1.0f, c.r * k) * 255.0f + 0.5f);
                const int g = static_cast<int>(std::min(1.0f, c.g * k) * 255.0f + 0.5f);
                const int b = static_cast<int>(std::min(1.0f, c.b * k) * 255.0f + 0.5f);
                const int a = std::max(r, std::max(g, b));
                auto *row = reinterpret_cast<std::uint32_t *>(out + (y * kTile + py) * stride) + x * kTile;
                row[px] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                          (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
            });
        }
    }
    return painted;
}

namespace {

// a*(8-k) + b*k over 8, for all four channels at once (two per lane).
inline std::uint32_t mix8(std::uint32_t a, std::uint32_t b, std::uint32_t k)
{
    const std::uint32_t j = 8 - k;
    const std::uint32_t lo = (((a & 0x00FF00FFu) * j + (b & 0x00FF00FFu) * k + 0x00040004u) >> 3) & 0x00FF00FFu;
    const std::uint32_t hi = ((((a >> 8) & 0x00FF00FFu) * j + ((b >> 8) & 0x00FF00FFu) * k + 0x00040004u) >> 3) & 0x00FF00FFu;
    return lo | (hi << 8);
}

// Bilinear weight (in 8ths) of the next glow pixel for output pixel o, and
// the glow pixel it starts from. Output pixel o samples the glow at
// o/4 - 3/8: phases -3/8, -1/8, +1/8, +3/8 around a glow pixel.
inline void tap(int o, int &base, std::uint32_t &weight)
{
    const int phase = o & 3;
    base = (o >> 2) - (phase < 2 ? 1 : 0);
    weight = static_cast<std::uint32_t>(phase < 2 ? 5 + 2 * phase : 2 * phase - 3);
}

} // namespace

void flatten(const unsigned char *glow, int glow_stride, const unsigned char *windows, int window_stride, int w, int h,
             unsigned char *out, int out_stride, int row0, int row1)
{
    static_assert(kTile == 4 * kPx, "flatten scales the glow by four");
    const int lw = w * kPx;
    const int lh = h * kPx;
    const int ow = w * kTile;
    const int oh = h * kTile;
    row0 = std::max(0, row0);
    row1 = std::min(oh, row1);
    if (row0 >= row1) {
        return;
    }
    // Widen each glow row the rows need once, then blend two widened rows
    // per output row. Outside the layer counts as clear.
    const int first = std::max(0, (row0 >> 2) - 1);
    const int last = std::min(lh - 1, ((row1 - 1) >> 2) + 1);
    std::vector<std::uint32_t> wide(static_cast<std::size_t>(last - first + 1) * static_cast<std::size_t>(ow), 0);
    std::vector<unsigned char> busy(static_cast<std::size_t>(last - first + 1), 0);
    for (int gy = first; gy <= last; ++gy) {
        const auto *src = reinterpret_cast<const std::uint32_t *>(glow + gy * glow_stride);
        std::uint32_t *dst = &wide[static_cast<std::size_t>(gy - first) * static_cast<std::size_t>(ow)];
        bool any = false;
        for (int gx = 0; gx < lw && !any; ++gx) {
            any = src[gx] != 0;
        }
        if (!any) {
            continue;
        }
        busy[static_cast<std::size_t>(gy - first)] = 1;
        for (int ox = 0; ox < ow; ++ox) {
            int gx = 0;
            std::uint32_t k = 0;
            tap(ox, gx, k);
            const std::uint32_t a = gx >= 0 ? src[gx] : 0;
            const std::uint32_t b = gx + 1 < lw ? src[gx + 1] : 0;
            dst[ox] = (a | b) == 0 ? 0 : mix8(a, b, k);
        }
    }
    const std::vector<std::uint32_t> clear(static_cast<std::size_t>(ow), 0);
    for (int oy = row0; oy < row1; ++oy) {
        int gy = 0;
        std::uint32_t k = 0;
        tap(oy, gy, k);
        const bool top_on = gy >= first && gy <= last && busy[static_cast<std::size_t>(gy - first)] != 0;
        const bool bottom_on = gy + 1 >= first && gy + 1 <= last && busy[static_cast<std::size_t>(gy + 1 - first)] != 0;
        const std::uint32_t *top =
            top_on ? &wide[static_cast<std::size_t>(gy - first) * static_cast<std::size_t>(ow)] : clear.data();
        const std::uint32_t *bottom =
            bottom_on ? &wide[static_cast<std::size_t>(gy + 1 - first) * static_cast<std::size_t>(ow)] : clear.data();
        const auto *win = reinterpret_cast<const std::uint32_t *>(windows + oy * window_stride);
        auto *dst = reinterpret_cast<std::uint32_t *>(out + oy * out_stride);
        if (!top_on && !bottom_on) {
            std::memcpy(dst, win, sizeof(std::uint32_t) * static_cast<std::size_t>(ow));
            continue;
        }
        for (int ox = 0; ox < ow; ++ox) {
            const std::uint32_t a = top[ox];
            const std::uint32_t b = bottom[ox];
            const std::uint32_t g = (a | b) == 0 ? 0 : mix8(a, b, k);
            const std::uint32_t pw = win[ox];
            if (pw == 0) {
                dst[ox] = g;
                continue;
            }
            std::uint32_t result = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const std::uint32_t sum = ((g >> shift) & 0xFF) + ((pw >> shift) & 0xFF);
                result |= (sum > 255 ? 255u : sum) << shift;
            }
            dst[ox] = result;
        }
    }
}

} // namespace night_light

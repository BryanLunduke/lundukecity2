// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "night_light.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>

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

struct Rgb {
    float r;
    float g;
    float b;
};

} // namespace

void build(const std::vector<int> &raw, int w, int h, unsigned char *out, int stride)
{
    if (w <= 0 || h <= 0 || out == nullptr || static_cast<int>(raw.size()) < w * h) {
        return;
    }
    const int lw = w * kPx;
    const int lh = h * kPx;
    std::vector<float> light(static_cast<std::size_t>(lw * lh * 3), 0.0f);
    std::vector<unsigned char> powered(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int word = raw[static_cast<std::size_t>(y * w + x)];
            if (word < 0 || (word & ZONEBIT) == 0 || (word & PWRBIT) == 0) {
                continue;
            }
            const int reach = zone_reach(word & LOMASK);
            for (int ty = y - 1; ty <= y + reach; ++ty) {
                for (int tx = x - 1; tx <= x + reach; ++tx) {
                    if (tx < 0 || ty < 0 || tx >= w || ty >= h) {
                        continue;
                    }
                    if (reach == 1 && (tx > x + 1 || ty > y + 1)) {
                        continue;
                    }
                    powered[static_cast<std::size_t>(ty * w + tx)] = 1;
                }
            }
        }
    }
    auto add = [&](int px, int py, const Rgb &c, float k) {
        if (px < 0 || py < 0 || px >= lw || py >= lh || k <= 0.0f) {
            return;
        }
        float *cell = &light[static_cast<std::size_t>((py * lw + px) * 3)];
        cell[0] += c.r * k;
        cell[1] += c.g * k;
        cell[2] += c.b * k;
    };
    auto glow = [&](double cx, double cy, double radius, const Rgb &c, float strength) {
        const int x0 = static_cast<int>(std::floor(cx - radius));
        const int x1 = static_cast<int>(std::ceil(cx + radius));
        const int y0 = static_cast<int>(std::floor(cy - radius));
        const int y1 = static_cast<int>(std::ceil(cy + radius));
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const double dx = px + 0.5 - cx;
                const double dy = py + 0.5 - cy;
                const double d = std::sqrt(dx * dx + dy * dy) / radius;
                if (d >= 1.0) {
                    continue;
                }
                const double fall = (1.0 - d) * (1.0 - d);
                add(px, py, c, static_cast<float>(strength * fall));
            }
        }
    };
    const Rgb warm{1.00f, 0.80f, 0.48f};
    const Rgb white{0.86f, 0.90f, 1.00f};
    const Rgb amber{1.00f, 0.62f, 0.30f};
    const Rgb sodium{1.00f, 0.70f, 0.36f};
    const Rgb flame{1.00f, 0.45f, 0.12f};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int word = raw[static_cast<std::size_t>(y * w + x)];
            if (word < 0) {
                continue;
            }
            const int tile = word & LOMASK;
            const Glow kind = glow_for_tile(tile);
            if (kind == Glow::None) {
                continue;
            }
            const bool lit = powered[static_cast<std::size_t>(y * w + x)] != 0 || (word & PWRBIT) != 0;
            const int bx = x * kPx;
            const int by = y * kPx;
            if (kind == Glow::Fire) {
                glow(bx + kPx * 0.5, by + kPx * 0.5, kPx * 1.6, flame, 0.95f);
                continue;
            }
            if (kind == Glow::Road) {
                // A lamp on every fourth road tile, on a diagonal lattice so
                // a straight street gets an even row of pools of light.
                if (((x + 2 * y) % 4) == 0) {
                    glow(bx + kPx * 0.5, by + kPx * 0.5, kPx * 1.3, sodium, 0.34f);
                    add(bx + kPx / 2, by + kPx / 2, sodium, 0.45f);
                }
                continue;
            }
            if (!lit) {
                continue;
            }
            Rgb color = warm;
            unsigned chance = 70;
            float strength = 0.55f;
            switch (kind) {
            case Glow::Home:
                color = warm;
                chance = 96;
                break;
            case Glow::Shop:
                color = white;
                chance = 120;
                strength = 0.5f;
                break;
            case Glow::Works:
                color = amber;
                chance = 64;
                break;
            case Glow::Port:
                color = white;
                chance = 48;
                strength = 0.5f;
                break;
            case Glow::Plant:
                color = amber;
                chance = 56;
                break;
            case Glow::Civic:
                color = white;
                chance = 110;
                strength = 0.5f;
                break;
            case Glow::Stadium:
                color = white;
                chance = 0;
                if ((word & ZONEBIT) != 0) {
                    glow(bx + kPx * 1.5, by + kPx * 1.5, kPx * 3.2, white, 0.30f);
                }
                break;
            default:
                break;
            }
            // Windows sit inside the tile, so a row of lit buildings still
            // shows dark seams between them and the day art reads through.
            for (int j = 1; j < kPx; ++j) {
                for (int i = 1; i < kPx; ++i) {
                    if ((hash(bx + i, by + j, tile) & 0xFF) < chance) {
                        add(bx + i, by + j, color, strength);
                    }
                }
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

} // namespace night_light

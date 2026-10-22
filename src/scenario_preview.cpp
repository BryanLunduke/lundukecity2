// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "scenario_preview.hpp"

#include "city_session.hpp"
#include "tile_atlas.hpp"

#include "micropolis.h"

#include <cstdint>
#include <vector>

namespace {

struct Cache {
    bool built = false;
    int look = -1;
    std::vector<cairo_surface_t *> images;

    ~Cache()
    {
        for (cairo_surface_t *surface : images) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
        }
    }
};

Cache &cache()
{
    static Cache stored;
    return stored;
}

void paint_map(CitySession &session, cairo_surface_t *surface)
{
    unsigned char *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    TileAtlas &atlas = tile_atlas();
    const bool tiles = atlas.loaded();
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            const int raw = session.map_value(x, y);
            double r = 0.55;
            double g = 0.42;
            double b = 0.24;
            if (tiles) {
                atlas.average_color(raw & LOMASK, r, g, b);
            }
            auto channel = [](double value) {
                const int byte = static_cast<int>(value * 255.0 + 0.5);
                if (byte < 0) {
                    return 0u;
                }
                if (byte > 255) {
                    return 255u;
                }
                return static_cast<unsigned>(byte);
            };
            const std::uint32_t pixel = (channel(r) << 16) | (channel(g) << 8) | channel(b);
            auto *word = reinterpret_cast<std::uint32_t *>(data + y * stride + x * 4);
            *word = pixel;
        }
    }
    cairo_surface_mark_dirty(surface);
}

void build()
{
    Cache &stored = cache();
    const int look = tile_atlas().look_generation();
    if (stored.built && stored.look == look) {
        return;
    }
    for (cairo_surface_t *surface : stored.images) {
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
    }
    stored.images.clear();
    stored.built = true;
    stored.look = look;
    CitySession session;
    session.set_speed(0);
    stored.images.reserve(static_cast<std::size_t>(CitySession::kScenarioCount));
    for (int i = 0; i < CitySession::kScenarioCount; ++i) {
        cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_RGB24, CitySession::kWorldW, CitySession::kWorldH);
        if (session.load_scenario(CitySession::scenario_def(i).id)) {
            paint_map(session, surface);
        }
        stored.images.push_back(surface);
    }
}

} // namespace

cairo_surface_t *scenario_preview(int index)
{
    build();
    Cache &stored = cache();
    if (index < 0 || index >= static_cast<int>(stored.images.size())) {
        return nullptr;
    }
    return stored.images[static_cast<std::size_t>(index)];
}

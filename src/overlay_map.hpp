// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairo.h>

#include <string>

// On-map data view. One sample per city tile, drawn with the map's own
// tile size so the picture keeps the 120:100 shape. Zero is transparent.
// Magnitude layers use one fixed scale and the same yellow-to-red ramp.

class OverlayMap {
public:
    OverlayMap() = default;
    ~OverlayMap();
    OverlayMap(const OverlayMap &) = delete;
    OverlayMap &operator=(const OverlayMap &) = delete;

    void set_layer(CitySession::MapLayer layer);
    CitySession::MapLayer layer() const { return layer_; }
    void set_session(CitySession *session);
    void sync();
    // Paints in map space. (0, 0) is tile (0, 0). Each tile is tile_size pixels.
    void draw_at(cairo_t *cr, int tile_size);
    // Letterboxed into a box, integer nearest scale, aspect kept.
    void draw(cairo_t *cr, int width, int height);

    const char *title() const;
    const char *legend() const;
    // Gradient layers (pollution, crime, land value, traffic, coverage,
    // population, growth) get the smooth heat picture on the map. On/off
    // layers (power, zones, water, transportation) stay crisp tiles.
    bool smooth() const;
    void set_smooth_enabled(bool on) { smooth_enabled_ = on; }

private:
    void rebuild();
    void rebuild_smooth();

    CitySession::MapLayer layer_ = CitySession::MapLayer::Power;
    CitySession *session_ = nullptr;
    cairo_surface_t *image_ = nullptr;
    cairo_surface_t *smooth_ = nullptr;
    bool smooth_enabled_ = true;
    unsigned map_serial_ = 0;
};

const char *overlay_title(CitySession::MapLayer layer);
const char *overlay_legend(CitySession::MapLayer layer);

// Unscaled width of the on/off legend chip. Commercial and Industrial are
// one 28px bar. Residential is the 14+28 pair, transportation is 34+28.
// 0 means the ramp uses the full legend width. The outline uses this width.
inline int legend_chip_px(CitySession::MapLayer layer)
{
    switch (layer) {
    case CitySession::MapLayer::Residential:
        return 42;
    case CitySession::MapLayer::Commercial:
    case CitySession::MapLayer::Industrial:
        return 28;
    case CitySession::MapLayer::Transportation:
        return 62;
    default:
        return 0;
    }
}

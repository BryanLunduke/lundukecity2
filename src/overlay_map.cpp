// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "overlay_map.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

constexpr int kW = CitySession::kWorldW;
constexpr int kH = CitySession::kWorldH;

// A fully funded, powered station deposits MAX_POLICE_STATION_EFFECT (1000)
// into one coverage cluster. The engine then smooths that map three times.
// On the 8-tile clusters the centre of that spike peaks at 218, so dividing
// by 1000 never reached red. 218 is the fixed top of the ramp: full coverage
// is the top colour, and a weaker or unpowered station stays below it.
constexpr int kStationCoverageFull = 218;

void heat(double t, unsigned char &r, unsigned char &g, unsigned char &b, unsigned char &a)
{
    if (t <= 0.0) {
        r = g = b = a = 0;
        return;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    r = 255;
    g = static_cast<unsigned char>(220.0 * (1.0 - t));
    b = static_cast<unsigned char>(32.0 * (1.0 - t));
    a = static_cast<unsigned char>(150.0 + 90.0 * t);
}

void cool(double t, unsigned char &r, unsigned char &g, unsigned char &b, unsigned char &a)
{
    if (t <= 0.0) {
        r = g = b = a = 0;
        return;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    r = static_cast<unsigned char>(40.0 * (1.0 - t));
    g = static_cast<unsigned char>(120.0 + 40.0 * t);
    b = 230;
    a = static_cast<unsigned char>(150.0 + 90.0 * t);
}

void solid(unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned char *pixel)
{
    pixel[0] = b;
    pixel[1] = g;
    pixel[2] = r;
    pixel[3] = a;
}

} // namespace

const char *overlay_title(CitySession::MapLayer layer)
{
    switch (layer) {
    case CitySession::MapLayer::Power:
        return "Power";
    case CitySession::MapLayer::Water:
        return "Water";
    case CitySession::MapLayer::Pollution:
        return "Pollution";
    case CitySession::MapLayer::Crime:
        return "Crime";
    case CitySession::MapLayer::LandValue:
        return "Land value";
    case CitySession::MapLayer::Traffic:
        return "Traffic";
    case CitySession::MapLayer::Police:
        return "Police coverage";
    case CitySession::MapLayer::Fire:
        return "Fire coverage";
    case CitySession::MapLayer::Population:
        return "Population";
    case CitySession::MapLayer::Growth:
        return "Growth";
    case CitySession::MapLayer::Zones:
        return "Zones";
    }
    return "Map";
}

const char *overlay_legend(CitySession::MapLayer layer)
{
    switch (layer) {
    case CitySession::MapLayer::Power:
        return "Yellow zones have power. Red zones do not. Orange is a power line.";
    case CitySession::MapLayer::Water:
        return "Blue is river, water, and flood. Land stays clear.";
    case CitySession::MapLayer::Pollution:
        return "Red means heavy pollution. Clear means none. Build parks and spread industry.";
    case CitySession::MapLayer::Crime:
        return "Red means high crime. Clear means none. Build police stations nearby.";
    case CitySession::MapLayer::LandValue:
        return "Red means expensive land. Clear means little value. Parks and water raise it.";
    case CitySession::MapLayer::Traffic:
        return "Red means heavy traffic. Clear means quiet streets. Add roads or rail.";
    case CitySession::MapLayer::Police:
        return "Red means a police station covers this block. Clear means no coverage.";
    case CitySession::MapLayer::Fire:
        return "Red means a fire station covers this block. Clear means no coverage.";
    case CitySession::MapLayer::Population:
        return "Red means a crowded block. Clear means nobody lives or works there.";
    case CitySession::MapLayer::Growth:
        return "Red means the block is growing. Blue means it is shrinking. Clear means no change.";
    case CitySession::MapLayer::Zones:
        return "Green homes, blue shops, yellow industry, gray roads, brown rail, blue water.";
    }
    return "";
}

void OverlayMap::set_layer(CitySession::MapLayer layer)
{
    if (layer_ != layer) {
        layer_ = layer;
        map_serial_ = 0;
    }
}

void OverlayMap::set_session(CitySession *session)
{
    session_ = session;
}

const char *OverlayMap::title() const
{
    return overlay_title(layer_);
}

const char *OverlayMap::legend() const
{
    return overlay_legend(layer_);
}

void OverlayMap::sync()
{
    if (session_ == nullptr) {
        return;
    }
    const unsigned serial = session_->map_serial();
    if (image_ != nullptr && map_serial_ == serial) {
        return;
    }
    map_serial_ = serial;
    rebuild();
}

void OverlayMap::rebuild()
{
    if (image_ == nullptr || cairo_image_surface_get_width(image_) != kW ||
        cairo_image_surface_get_height(image_) != kH ||
        cairo_image_surface_get_format(image_) != CAIRO_FORMAT_ARGB32) {
        if (image_ != nullptr) {
            cairo_surface_destroy(image_);
        }
        image_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kW, kH);
    }
    unsigned char *data = cairo_image_surface_get_data(image_);
    const int stride = cairo_image_surface_get_stride(image_);
    std::fill(data, data + stride * kH, 0);
    if (session_ == nullptr) {
        cairo_surface_mark_dirty(image_);
        return;
    }
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            unsigned char *pixel = data + y * stride + x * 4;
            const int value = session_->layer_value(layer_, x, y);
            unsigned char r = 0;
            unsigned char g = 0;
            unsigned char b = 0;
            unsigned char a = 0;
            switch (layer_) {
            case CitySession::MapLayer::Power:
                if (value == 3) {
                    solid(250, 220, 40, 210, pixel);
                } else if (value == 2) {
                    solid(220, 40, 32, 210, pixel);
                } else if (value == 4) {
                    solid(240, 140, 24, 210, pixel);
                }
                continue;
            case CitySession::MapLayer::Water:
                if (value > 0) {
                    solid(40, 110, 230, 180, pixel);
                }
                continue;
            case CitySession::MapLayer::Zones:
                if (value == 1) {
                    solid(40, 170, 48, 200, pixel);
                } else if (value == 2) {
                    solid(48, 96, 210, 200, pixel);
                } else if (value == 3) {
                    solid(230, 190, 32, 200, pixel);
                } else if (value == 4) {
                    solid(48, 48, 52, 200, pixel);
                } else if (value == 5) {
                    solid(140, 84, 36, 200, pixel);
                } else if (value == 8) {
                    solid(40, 110, 230, 160, pixel);
                }
                continue;
            case CitySession::MapLayer::Growth: {
                const double t = static_cast<double>(value) / 200.0;
                if (t < 0.0) {
                    cool(-t, r, g, b, a);
                } else {
                    heat(t, r, g, b, a);
                }
                break;
            }
            case CitySession::MapLayer::Police:
            case CitySession::MapLayer::Fire:
                heat(static_cast<double>(value) / static_cast<double>(kStationCoverageFull), r, g, b, a);
                break;
            default:
                heat(static_cast<double>(value) / 255.0, r, g, b, a);
                break;
            }
            if (a == 0) {
                continue;
            }
            solid(r, g, b, a, pixel);
        }
    }
    cairo_surface_mark_dirty(image_);
}

OverlayMap::~OverlayMap()
{
    if (image_ != nullptr) {
        cairo_surface_destroy(image_);
        image_ = nullptr;
    }
}

void OverlayMap::draw_at(cairo_t *cr, int tile_size)
{
    if (image_ == nullptr) {
        sync();
    }
    if (image_ == nullptr || tile_size < 1) {
        return;
    }
    cairo_save(cr);
    cairo_scale(cr, static_cast<double>(tile_size), static_cast<double>(tile_size));
    cairo_set_source_surface(cr, image_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

void OverlayMap::draw(cairo_t *cr, int width, int height)
{
    if (image_ == nullptr) {
        sync();
    }
    if (image_ == nullptr || width < 2 || height < 2) {
        return;
    }
    const int scale = std::max(1, std::min(width / kW, height / kH));
    const int dw = kW * scale;
    const int dh = kH * scale;
    cairo_save(cr);
    cairo_translate(cr, (width - dw) / 2.0, (height - dh) / 2.0);
    cairo_scale(cr, static_cast<double>(scale), static_cast<double>(scale));
    cairo_set_source_surface(cr, image_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

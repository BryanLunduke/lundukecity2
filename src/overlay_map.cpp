// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "overlay_map.hpp"

#include <algorithm>
#include <cstdint>

namespace {

struct Rgb {
    double r, g, b;
};

Rgb heat(CitySession::MapLayer layer, double t)
{
    t = std::max(0.0, std::min(1.0, t));
    switch (layer) {
    case CitySession::MapLayer::Pollution:
        return {0.15 + 0.75 * t, 0.16 + 0.16 * t, 0.14 + 0.10 * t};
    case CitySession::MapLayer::Crime:
        return {0.20 + 0.75 * t, 0.05, 0.12 + 0.15 * t};
    case CitySession::MapLayer::LandValue:
        return {0.05 + 0.25 * t, 0.18 + 0.62 * t, 0.08 + 0.15 * t};
    case CitySession::MapLayer::Traffic:
        return {0.25 + 0.70 * t, 0.12 + 0.35 * t, 0.05};
    default:
        return {t, t, t};
    }
}

} // namespace

void OverlayMap::set_layer(CitySession::MapLayer layer)
{
    if (layer_ != layer) {
        layer_ = layer;
        cells_.clear();
        map_serial_ = 0;
    }
}

void OverlayMap::set_session(CitySession *session)
{
    session_ = session;
}

const char *OverlayMap::title() const
{
    switch (layer_) {
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
    }
    return "Map";
}

const char *OverlayMap::legend() const
{
    switch (layer_) {
    case CitySession::MapLayer::Power:
        return "Yellow powered zones, red unpowered zones, orange lines, blue water.";
    case CitySession::MapLayer::Water:
        return "Blue is water and flood. The engine stores water as map tiles.";
    case CitySession::MapLayer::Pollution:
        return "Brighter gray means higher pollution on the engine's density map.";
    case CitySession::MapLayer::Crime:
        return "Brighter red means a higher crime rate on the engine's crime map.";
    case CitySession::MapLayer::LandValue:
        return "Brighter green means higher land value on the engine's land-value map.";
    case CitySession::MapLayer::Traffic:
        return "Brighter orange means heavier traffic on the engine's traffic map.";
    }
    return "";
}

bool OverlayMap::density_layer() const
{
    return layer_ != CitySession::MapLayer::Power && layer_ != CitySession::MapLayer::Water;
}

void OverlayMap::sync()
{
    if (session_ == nullptr) {
        return;
    }
    const bool density = density_layer();
    const int cells_w = density ? CitySession::kWorldW / 2 : CitySession::kWorldW;
    const int cells_h = density ? CitySession::kWorldH / 2 : CitySession::kWorldH;
    const int count = cells_w * cells_h;
    std::vector<int> next(static_cast<std::size_t>(count));
    int peak = 1;
    for (int y = 0; y < cells_h; ++y) {
        for (int x = 0; x < cells_w; ++x) {
            const int tile_x = density ? x * 2 : x;
            const int tile_y = density ? y * 2 : y;
            int water = session_->layer_value(CitySession::MapLayer::Water, tile_x, tile_y);
            if (density) {
                for (int dy = 0; dy < 2 && water == 0; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        if (session_->layer_value(CitySession::MapLayer::Water, tile_x + dx, tile_y + dy) > 0) {
                            water = 1;
                            break;
                        }
                    }
                }
            }
            int value = 0;
            if (layer_ != CitySession::MapLayer::Water) {
                value = session_->layer_value(layer_, tile_x, tile_y);
            }
            if (density) {
                peak = std::max(peak, value);
            }
            next[static_cast<std::size_t>(y * cells_w + x)] = water > 0 ? -value - 1 : value;
        }
    }
    const unsigned serial = session_->map_serial();
    if (!cells_.empty() && cells_ == next && peak_ == peak && map_serial_ == serial && image_ != nullptr) {
        return;
    }
    cells_ = std::move(next);
    peak_ = peak;
    map_serial_ = serial;
    rebuild();
}

void OverlayMap::rebuild()
{
    const bool density = density_layer();
    const int cells_w = density ? CitySession::kWorldW / 2 : CitySession::kWorldW;
    const int cells_h = density ? CitySession::kWorldH / 2 : CitySession::kWorldH;
    if (image_ == nullptr || cairo_image_surface_get_width(image_) != cells_w ||
        cairo_image_surface_get_height(image_) != cells_h) {
        if (image_ != nullptr) {
            cairo_surface_destroy(image_);
        }
        image_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cells_w, cells_h);
    }
    unsigned char *data = cairo_image_surface_get_data(image_);
    const int stride = cairo_image_surface_get_stride(image_);
    auto pack = [](const Rgb &color) {
        auto channel = [](double value) {
            const int byte = static_cast<int>(value * 255.0 + 0.5);
            return static_cast<std::uint32_t>(std::max(0, std::min(255, byte)));
        };
        return (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
    };
    for (int y = 0; y < cells_h; ++y) {
        for (int x = 0; x < cells_w; ++x) {
            const int packed = cells_[static_cast<std::size_t>(y * cells_w + x)];
            const bool water = packed < 0;
            const int value = water ? -packed - 1 : packed;
            Rgb color{0.55, 0.42, 0.28};
            if (water && layer_ != CitySession::MapLayer::Water) {
                color = {0.10, 0.28, 0.72};
            }
            if (layer_ == CitySession::MapLayer::Power && !water) {
                switch (value) {
                case 2:
                    color = {0.85, 0.12, 0.10};
                    break;
                case 3:
                    color = {0.98, 0.86, 0.15};
                    break;
                case 4:
                    color = {0.95, 0.55, 0.10};
                    break;
                default:
                    break;
                }
            } else if (layer_ == CitySession::MapLayer::Water) {
                color = water ? Rgb{0.15, 0.45, 0.95} : Rgb{0.45, 0.36, 0.24};
            } else if (!water && layer_ != CitySession::MapLayer::Power) {
                const double t = value / static_cast<double>(std::max(peak_, 1));
                if (t > 0) {
                    color = heat(layer_, t);
                }
            }
            auto *pixel = reinterpret_cast<std::uint32_t *>(data + y * stride + x * 4);
            *pixel = pack(color);
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

void OverlayMap::draw(cairo_t *cr, int width, int height)
{
    if (image_ == nullptr) {
        sync();
    }
    if (image_ == nullptr || width < 2 || height < 2) {
        return;
    }
    cairo_save(cr);
    cairo_scale(cr, static_cast<double>(width) / cairo_image_surface_get_width(image_),
                static_cast<double>(height) / cairo_image_surface_get_height(image_));
    cairo_set_source_surface(cr, image_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

bool OverlayMap::click(double x, double y, int width, int height, double &fx, double &fy) const
{
    if (width < 2 || height < 2) {
        return false;
    }
    fx = std::max(0.0, std::min(1.0, x / width));
    fy = std::max(0.0, std::min(1.0, y / height));
    return true;
}

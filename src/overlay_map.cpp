// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "overlay_map.hpp"

#include "heat_field.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kW = CitySession::kWorldW;
constexpr int kH = CitySession::kWorldH;
// Smooth heat picture: pixels per tile before the bilinear scale to the map.
constexpr int kSmoothPx = 4;

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
    case CitySession::MapLayer::Residential:
        return "Residential";
    case CitySession::MapLayer::Commercial:
        return "Commercial";
    case CitySession::MapLayer::Industrial:
        return "Industrial";
    case CitySession::MapLayer::Transportation:
        return "Transportation";
    }
    return "Map";
}

// Shops are purple, not blue, so they never read as water (40, 110, 230).
constexpr unsigned char kShopR = 168;
constexpr unsigned char kShopG = 72;
constexpr unsigned char kShopB = 200;

const char *overlay_legend(CitySession::MapLayer layer)
{
    switch (layer) {
    case CitySession::MapLayer::Power:
        return "Yellow buildings have power. Red ones do not. Orange is a power line.";
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
        return "Green homes, purple shops, yellow industry, gray roads, brown rail, blue water.";
    case CitySession::MapLayer::Residential:
        return "Green is a residential zone. Everything else stays clear.";
    case CitySession::MapLayer::Commercial:
        return "Purple is a commercial zone. Everything else stays clear.";
    case CitySession::MapLayer::Industrial:
        return "Yellow is an industrial zone. Everything else stays clear.";
    case CitySession::MapLayer::Transportation:
        return "Gray is a road. Brown is rail. Land and water stay clear.";
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
    rebuild_smooth();
}

bool OverlayMap::smooth() const
{
    if (!smooth_enabled_) {
        return false;
    }
    switch (layer_) {
    case CitySession::MapLayer::Pollution:
    case CitySession::MapLayer::Crime:
    case CitySession::MapLayer::LandValue:
    case CitySession::MapLayer::Traffic:
    case CitySession::MapLayer::Police:
    case CitySession::MapLayer::Fire:
    case CitySession::MapLayer::Population:
    case CitySession::MapLayer::Growth:
        return true;
    default:
        return false;
    }
}

void OverlayMap::rebuild_smooth()
{
    if (!smooth() || session_ == nullptr) {
        return;
    }
    const int sw = kW * kSmoothPx;
    const int sh = kH * kSmoothPx;
    if (smooth_ == nullptr) {
        smooth_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    }
    cairo_surface_flush(smooth_);
    unsigned char *data = cairo_image_surface_get_data(smooth_);
    const int stride = cairo_image_surface_get_stride(smooth_);
    std::vector<float> field(static_cast<std::size_t>(kW * kH), 0.0f);
    const bool signed_field = layer_ == CitySession::MapLayer::Growth;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const int value = session_->layer_value(layer_, x, y);
            double t = 0.0;
            if (signed_field) {
                t = static_cast<double>(value) / 200.0;
            } else if (layer_ == CitySession::MapLayer::Police || layer_ == CitySession::MapLayer::Fire) {
                t = static_cast<double>(value) / static_cast<double>(kStationCoverageFull);
            } else {
                t = static_cast<double>(value) / 255.0;
            }
            field[static_cast<std::size_t>(y * kW + x)] = static_cast<float>(std::max(-1.0, std::min(1.0, t)));
        }
    }
    heat_field::blur(field, kW, kH, 1.1);
    // A Data Center makes no pollution: its footprint stays clear instead
    // of taking the blur from the land around it, and the contour pass
    // below outlines it.
    std::vector<unsigned char> clear;
    if (layer_ == CitySession::MapLayer::Pollution) {
        clear.assign(static_cast<std::size_t>(kW * kH), 0);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                clear[static_cast<std::size_t>(y * kW + x)] = session_->datacenter_tile(x, y) ? 1 : 0;
            }
        }
    }
    std::vector<float> fine(static_cast<std::size_t>(sw * sh), 0.0f);
    for (int py = 0; py < sh; ++py) {
        for (int px = 0; px < sw; ++px) {
            if (!clear.empty() && clear[static_cast<std::size_t>((py / kSmoothPx) * kW + px / kSmoothPx)] != 0) {
                continue;
            }
            fine[static_cast<std::size_t>(py * sw + px)] =
                heat_field::sample(field, kW, kH, (px + 0.5) / kSmoothPx, (py + 0.5) / kSmoothPx);
        }
    }
    constexpr int kLevels = 5;
    for (int py = 0; py < sh; ++py) {
        auto *row = reinterpret_cast<std::uint32_t *>(data + py * stride);
        for (int px = 0; px < sw; ++px) {
            const float v = fine[static_cast<std::size_t>(py * sw + px)];
            const float t = std::fabs(v);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            if (v < 0.0f) {
                heat_field::cool_ramp(t, r, g, b, a);
            } else {
                heat_field::ramp(t, r, g, b, a);
            }
            if (a > 0.0f) {
                const int here = heat_field::band(t, kLevels);
                const float right = px + 1 < sw ? std::fabs(fine[static_cast<std::size_t>(py * sw + px + 1)]) : t;
                const float below = py + 1 < sh ? std::fabs(fine[static_cast<std::size_t>((py + 1) * sw + px)]) : t;
                if (here > 0 && (heat_field::band(right, kLevels) != here || heat_field::band(below, kLevels) != here)) {
                    // Contour line: the same hue, darker and nearly opaque.
                    r *= 0.55f;
                    g *= 0.45f;
                    b *= 0.45f;
                    a = std::min(1.0f, a + 0.35f);
                }
            }
            const auto c = [&](float channel) {
                return static_cast<std::uint32_t>(std::min(255.0f, channel * a * 255.0f + 0.5f));
            };
            row[px] = (static_cast<std::uint32_t>(std::min(255.0f, a * 255.0f + 0.5f)) << 24) | (c(r) << 16) |
                      (c(g) << 8) | c(b);
        }
    }
    cairo_surface_mark_dirty(smooth_);
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
                    solid(kShopR, kShopG, kShopB, 200, pixel);
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
            case CitySession::MapLayer::Residential:
                // Classic lawns are already this green. A lighter fill with a
                // dark edge stays readable on those tiles without new art.
                if (value > 0) {
                    const bool edge = session_->layer_value(layer_, x - 1, y) == 0 ||
                                      session_->layer_value(layer_, x + 1, y) == 0 ||
                                      session_->layer_value(layer_, x, y - 1) == 0 ||
                                      session_->layer_value(layer_, x, y + 1) == 0;
                    if (edge) {
                        solid(16, 72, 16, 235, pixel);
                    } else {
                        solid(186, 255, 64, 215, pixel);
                    }
                }
                continue;
            case CitySession::MapLayer::Commercial:
                if (value > 0) {
                    solid(kShopR, kShopG, kShopB, 200, pixel);
                }
                continue;
            case CitySession::MapLayer::Industrial:
                if (value > 0) {
                    solid(230, 190, 32, 200, pixel);
                }
                continue;
            case CitySession::MapLayer::Transportation:
                if (value == 1) {
                    solid(48, 48, 52, 200, pixel);
                } else if (value == 2) {
                    solid(140, 84, 36, 200, pixel);
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
    if (smooth_ != nullptr) {
        cairo_surface_destroy(smooth_);
        smooth_ = nullptr;
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
    if (smooth()) {
        if (smooth_ == nullptr) {
            rebuild_smooth();
        }
        if (smooth_ != nullptr) {
            cairo_save(cr);
            const double up = static_cast<double>(tile_size) / kSmoothPx;
            cairo_scale(cr, up, up);
            cairo_set_source_surface(cr, smooth_, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
            cairo_paint(cr);
            cairo_restore(cr);
            return;
        }
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

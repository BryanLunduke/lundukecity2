// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "side_widgets.hpp"

#include "city_session.hpp"
#include "special_buildings.hpp"
#include "map_fit.hpp"
#include "tile_atlas.hpp"
#include "ui_scale.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>

namespace {

void fill(cairo_t *cr, double x, double y, double w, double h, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}


std::uint32_t pack_rgb(double r, double g, double b)
{
    auto channel = [](double value) {
        const int byte = static_cast<int>(value * 255.0 + 0.5);
        return static_cast<std::uint32_t>(std::max(0, std::min(255, byte)));
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

} // namespace

void minimap_tile_rgb(int raw, double &r, double &g, double &b)
{
    // Special buildings sit on ids the atlas averages wrongly (old trash and
    // explosion art) or does not have at all (1020-1023 are past its 960
    // tiles), so they take their colour from the building table instead.
    if (special_minimap_rgb(raw & LOMASK, r, g, b)) {
        return;
    }
    const TileAtlas &atlas = tile_atlas();
    if (atlas.loaded()) {
        atlas.average_color(raw & LOMASK, r, g, b);
        return;
    }
    const int t = raw & LOMASK;
    if (t >= RIVER && t <= WATER_HIGH) {
        r = 0.06;
        g = 0.24;
        b = 0.78;
    } else if (t >= TREEBASE && t <= WOODS5) {
        r = 0.05;
        g = 0.48;
        b = 0.10;
    } else if (t >= RESBASE && t < COMBASE) {
        r = 0.16;
        g = 0.62;
        b = 0.16;
    } else if (t >= COMBASE && t < INDBASE) {
        r = 0.28;
        g = 0.42;
        b = 0.86;
    } else if (t >= INDBASE && t < PORTBASE) {
        r = 0.92;
        g = 0.76;
        b = 0.08;
    } else if ((t >= ROADBASE && t <= BRWXXX7) || t == ROADVPOWERH) {
        r = 0.16;
        g = 0.16;
        b = 0.18;
    } else if (t >= RAILBASE && t <= LASTRAIL) {
        r = 0.45;
        g = 0.28;
        b = 0.10;
    } else if (t >= POWERBASE && t <= LASTPOWER) {
        r = 1.0;
        g = 0.92;
        b = 0.05;
    } else if (t >= FIREBASE && t <= LASTFIRE) {
        r = 0.92;
        g = 0.18;
        b = 0.06;
    } else {
        r = 0.86;
        g = 0.58;
        b = 0.26;
    }
}

void MinimapView::set_session(CitySession *session)
{
    session_ = session;
    cached_raw_.clear();
    engraved_primed_ = false;
}

void MinimapView::set_viewport_provider(std::function<void(double &, double &, double &, double &)> provider)
{
    viewport_ = std::move(provider);
}

AspectBox MinimapView::land_box(int width, int height) const
{
    AspectBox box = largest_aspect_box(width, height, 6.0, 5.0);
    if (pin_top_ && box.y > 2.0) {
        box.y = 2.0;
    }
    return box;
}

void MinimapView::set_overlay(bool on, int layer)
{
    overlay_on_ = on;
    overlay_layer_ = layer;
}

namespace {

double steady_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

engraved::Plate plate_for(CitySession::MapLayer layer)
{
    using L = CitySession::MapLayer;
    switch (layer) {
    case L::Power:
        return engraved::Plate::Power;
    case L::Water:
        return engraved::Plate::Water;
    case L::Zones:
        return engraved::Plate::Zones;
    case L::Residential:
        return engraved::Plate::Res;
    case L::Commercial:
        return engraved::Plate::Com;
    case L::Industrial:
        return engraved::Plate::Ind;
    case L::Transportation:
        return engraved::Plate::Transport;
    case L::Growth:
        return engraved::Plate::Growth;
    default:
        return engraved::Plate::Heat;
    }
}

} // namespace

void MinimapView::sync_engraved()
{
    const double now = steady_seconds();
    const unsigned serial = session_ != nullptr ? session_->map_serial() : 0;
    if (!engraved_primed_ || serial != engraved_serial_) {
        engraved_serial_ = serial;
        engraved_gate_.mark();
    }
    // The first picture, and a new city, draw at once. Later changes wait
    // for the throttle so a busy city redraws about once a second.
    if (!engraved_primed_ || engraved_gate_.due(now)) {
        engraved_primed_ = true;
        std::vector<int> raw(static_cast<std::size_t>(engraved::kW * engraved::kH));
        for (int y = 0; y < engraved::kH; ++y) {
            for (int x = 0; x < engraved::kW; ++x) {
                raw[static_cast<std::size_t>(y * engraved::kW + x)] =
                    session_ != nullptr ? session_->map_value(x, y) : 0;
            }
        }
        engraved_map_.update_tiles(raw.data());
    }
    if (!overlay_on_ || session_ == nullptr) {
        engraved_map_.clear_plate();
        plate_layer_seen_ = -1;
        return;
    }
    // Overlay data changes without a map change (crime, traffic), so the
    // plate is read again every two seconds, and at once on a new layer.
    if (overlay_layer_ != plate_layer_seen_ || now - plate_read_at_ >= 2.0) {
        plate_layer_seen_ = overlay_layer_;
        plate_read_at_ = now;
        const auto layer = static_cast<CitySession::MapLayer>(overlay_layer_);
        const engraved::Plate plate = plate_for(layer);
        std::vector<float> values(static_cast<std::size_t>(engraved::kW * engraved::kH), 0.0f);
        for (int y = 0; y < engraved::kH; ++y) {
            for (int x = 0; x < engraved::kW; ++x) {
                const int v = session_->layer_value(layer, x, y);
                float f = static_cast<float>(v);
                if (plate == engraved::Plate::Growth) {
                    f = static_cast<float>(std::max(-1.0, std::min(1.0, v / 200.0)));
                } else if (plate == engraved::Plate::Heat) {
                    // Same scales as the big map's overlay.
                    const double top = (layer == CitySession::MapLayer::Police || layer == CitySession::MapLayer::Fire)
                                           ? 218.0
                                           : 255.0;
                    f = static_cast<float>(std::max(0.0, std::min(1.0, v / top)));
                } else if (plate == engraved::Plate::Water || plate == engraved::Plate::Res ||
                           plate == engraved::Plate::Com || plate == engraved::Plate::Ind) {
                    f = v > 0 ? 1.0f : 0.0f;
                }
                values[static_cast<std::size_t>(y * engraved::kW + x)] = f;
            }
        }
        engraved_map_.set_plate(plate, values);
    }
}

void MinimapView::draw_engraved(cairo_t *cr, int width, int height)
{
    const AspectBox box = land_box(width, height);
    if (box.width < 2.0 || box.height < 2.0) {
        return;
    }
    const std::string title = session_ != nullptr ? session_->city_name() : std::string();
    engraved_map_.set_geometry(static_cast<int>(std::floor(box.width + 0.01)),
                               static_cast<int>(std::floor(box.height + 0.01)), unit_, title);
    engraved_map_.set_night(night_);
    sync_engraved();
    engraved_map_.compose();
    cairo_surface_t *sheet = engraved_map_.sheet();
    if (sheet == nullptr) {
        return;
    }
    const engraved::SheetGeom &g = engraved_map_.geom();
    const double sx = std::round(box.x) - g.margin;
    const double sy = std::round(box.y) - g.margin;
    cairo_save(cr);
    // A soft shadow so the sheet sits on the map like paper.
    cairo_set_source_rgba(cr, 0, 0, 0, 0.32);
    cairo_rectangle(cr, sx + std::round(3 * unit_), sy + std::round(3 * unit_), g.sheet_w(), g.sheet_h());
    cairo_fill(cr);
    cairo_set_source_surface(cr, sheet, sx, sy);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    paint_viewport(cr, width, height);
}

void MinimapView::sync()
{
    if (engraved_) {
        // The engraved sheet syncs while it draws, with its own throttle.
        return;
    }
    if (pixels_ == nullptr || cairo_image_surface_get_width(pixels_) != CitySession::kWorldW ||
        cairo_image_surface_get_height(pixels_) != CitySession::kWorldH) {
        if (pixels_ != nullptr) {
            cairo_surface_destroy(pixels_);
        }
        pixels_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, CitySession::kWorldW, CitySession::kWorldH);
        cached_raw_.assign(static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH), -1);
    }
    const std::size_t cells = static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH);
    if (cached_raw_.size() != cells) {
        cached_raw_.assign(cells, -1);
    }
    const int look = tile_atlas().look_generation();
    if (look != look_generation_) {
        look_generation_ = look;
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
    }

    unsigned char *data = cairo_image_surface_get_data(pixels_);
    const int stride = cairo_image_surface_get_stride(pixels_);
    int dirty = 0;
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            const int raw = session_ != nullptr ? session_->map_value(x, y) : 0;
            const std::size_t index = static_cast<std::size_t>(y * CitySession::kWorldW + x);
            if (cached_raw_[index] == raw) {
                continue;
            }
            double r = 0;
            double g = 0;
            double b = 0;
            minimap_tile_rgb(raw, r, g, b);
            auto *pixel = reinterpret_cast<std::uint32_t *>(data + y * stride + x * 4);
            *pixel = pack_rgb(r, g, b);
            cached_raw_[index] = raw;
            ++dirty;
        }
    }
    if (dirty > 0) {
        cairo_surface_mark_dirty(pixels_);
    }
}

bool MinimapView::press(double x, double y, int width, int height)
{
    const AspectBox box = land_box(width, height);
    if (box.width <= 1.0 || box.height <= 1.0) {
        return false;
    }
    if (x < box.x || y < box.y || x >= box.x + box.width || y >= box.y + box.height) {
        return false;
    }
    double fx = 0;
    double fy = 0;
    aspect_box_fraction(box, x, y, fx, fy);
    if (on_jump) {
        on_jump(std::max(0.0, std::min(1.0, fx)), std::max(0.0, std::min(1.0, fy)));
    }
    return true;
}

bool MinimapView::drag(double x, double y, int width, int height)
{
    const AspectBox box = land_box(width, height);
    if (box.width <= 1.0 || box.height <= 1.0) {
        return false;
    }
    double fx = 0;
    double fy = 0;
    aspect_box_fraction_clamped(box, x, y, fx, fy);
    if (on_jump) {
        on_jump(fx, fy);
    }
    return true;
}

MinimapView::~MinimapView()
{
    if (pixels_ != nullptr) {
        cairo_surface_destroy(pixels_);
        pixels_ = nullptr;
    }
}

void MinimapView::paint_viewport(cairo_t *cr, int width, int height) const
{
    if (!viewport_) {
        return;
    }
    double vx = 0;
    double vy = 0;
    double vw = 1;
    double vh = 1;
    viewport_(vx, vy, vw, vh);
    clamp_viewport_span(vx, vw);
    clamp_viewport_span(vy, vh);
    const AspectBox box = land_box(width, height);
    const double rx = box.x + vx * box.width;
    const double ry = box.y + vy * box.height;
    const double rw = std::max(4.0, vw * box.width);
    const double rh = std::max(4.0, vh * box.height);
    if (engraved_) {
        // Red ink, dashed, on a pale halo so it lifts off the paper. Over a
        // red overlay plate the frame is dark ink instead.
        const bool red_plate = engraved::plate_is_red(engraved_map_.plate());
        const double u = unit_;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        cairo_set_line_width(cr, 3.5 * u);
        cairo_set_source_rgba(cr, 0.98, 0.95, 0.86, 0.85);
        cairo_rectangle(cr, rx + 0.5 * u, ry + 0.5 * u, rw - u, rh - u);
        cairo_stroke(cr);
        const double dash[2] = {4.0 * u, 2.0 * u};
        cairo_set_dash(cr, dash, 2, 0);
        cairo_set_line_width(cr, 1.6 * u);
        if (red_plate) {
            cairo_set_source_rgb(cr, 0.16, 0.11, 0.07);
        } else {
            cairo_set_source_rgb(cr, 0.72, 0.08, 0.06);
        }
        cairo_rectangle(cr, rx + 0.5 * u, ry + 0.5 * u, rw - u, rh - u);
        cairo_stroke(cr);
        cairo_restore(cr);
        return;
    }
    cairo_set_line_width(cr, 2);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, rx, ry, rw, rh);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, rx + 1.5, ry + 1.5, std::max(1.0, rw - 3), std::max(1.0, rh - 3));
    cairo_stroke(cr);
}

void MinimapView::draw(cairo_t *cr, int width, int height)
{
    if (engraved_) {
        draw_engraved(cr, width, height);
        return;
    }
    const AspectBox box = land_box(width, height);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    fill(cr, 0, 0, width, height, 0xED / 255.0, 0xEC / 255.0, 0xEB / 255.0);
    if (box.width < 1.0 || box.height < 1.0) {
        return;
    }
    if (pixels_ == nullptr) {
        sync();
    }
    if (pixels_ != nullptr) {
        cairo_save(cr);
        cairo_translate(cr, box.x, box.y);
        cairo_scale(cr, box.width / CitySession::kWorldW, box.height / CitySession::kWorldH);
        cairo_set_source_surface(cr, pixels_, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        fill(cr, box.x, box.y, box.width, box.height, 0.86, 0.58, 0.26);
    }
    if (night_) {
        // Halfway between day and the map's dusk multiply (0.30, 0.36, 0.58).
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
        cairo_set_source_rgb(cr, 0.55, 0.60, 0.80);
        cairo_rectangle(cr, box.x, box.y, box.width, box.height);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    paint_viewport(cr, width, height);
}

void DemandView::set_session(CitySession *session)
{
    session_ = session;
}

void DemandView::draw(cairo_t *cr, int width, int height, float ui_scale) const
{
    const double w = width;
    const double h = height;
    const float scale = ui_scale > 0.05f ? ui_scale : 1.0f;
    auto px = [&](double n) { return std::max(1.0, std::round(n * static_cast<double>(scale))); };
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    fill(cr, 0, 0, w, h, 0xED / 255.0, 0xEC / 255.0, 0xEB / 255.0);

    const double demands[3] = {
        session_ != nullptr ? session_->res_demand() : 0,
        session_ != nullptr ? session_->com_demand() : 0,
        session_ != nullptr ? session_->ind_demand() : 0,
    };
    const double colors[3][3] = {
        {0.05, 0.62, 0.12},
        {0.12, 0.28, 0.90},
        {0.95, 0.78, 0.02},
    };
    const char *letters[3] = {"R", "C", "I"};

    const double gap = px(5);
    const double col_w = (w - gap * 4) / 3.0;
    const double top = px(6);
    const double label_px = ui_scale::demand_label_px(scale);
    const double label_band = label_px + px(8);
    const double meter_h = std::max(px(8), h - top - label_band);
    const double mid = top + meter_h / 2.0;

    for (int i = 0; i < 3; ++i) {
        const double x = gap + i * (col_w + gap);
        fill(cr, x, top, col_w, meter_h, 0.96, 0.96, 0.96);
        cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x + 0.5, top + 0.5, col_w - 1, meter_h - 1);
        cairo_stroke(cr);
        const double frac = demands[i];
        if (frac >= 0) {
            const double bh = (meter_h / 2.0 - 2) * std::min(1.0, frac);
            fill(cr, x + 2, mid - bh, col_w - 4, bh, colors[i][0], colors[i][1], colors[i][2]);
        } else {
            const double bh = (meter_h / 2.0 - 2) * std::min(1.0, -frac);
            fill(cr, x + 2, mid, col_w - 4, bh, colors[i][0], colors[i][1], colors[i][2]);
        }
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, x + 1, mid);
        cairo_line_to(cr, x + col_w - 1, mid);
        cairo_stroke(cr);

        const double text_r = i == 2 ? 0.45 : colors[i][0] * 0.65;
        const double text_g = i == 2 ? 0.32 : colors[i][1] * 0.75;
        const double text_b = i == 2 ? 0.0 : colors[i][2] * 0.75;
        cairo_set_source_rgb(cr, text_r, text_g, text_b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, label_px);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, letters[i], &ext);
        cairo_move_to(cr, x + (col_w - ext.width) / 2.0, top + meter_h + label_px);
        cairo_show_text(cr, letters[i]);
    }
}

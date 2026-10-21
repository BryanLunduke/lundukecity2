// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "side_widgets.hpp"

#include "city_session.hpp"
#include "map_fit.hpp"
#include "tile_atlas.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cstdint>

namespace {

void fill(cairo_t *cr, double x, double y, double w, double h, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void tile_rgb(int raw, double &r, double &g, double &b)
{
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

std::uint32_t pack_rgb(double r, double g, double b)
{
    auto channel = [](double value) {
        const int byte = static_cast<int>(value * 255.0 + 0.5);
        return static_cast<std::uint32_t>(std::max(0, std::min(255, byte)));
    };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

} // namespace

void MinimapView::set_session(CitySession *session)
{
    session_ = session;
    cached_raw_.clear();
}

void MinimapView::set_viewport_provider(std::function<void(double &, double &, double &, double &)> provider)
{
    viewport_ = std::move(provider);
}

AspectBox MinimapView::land_box(int width, int height) const
{
    return largest_aspect_box(width, height, 6.0, 5.0);
}

void MinimapView::sync()
{
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
            tile_rgb(raw, r, g, b);
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
    double fx = 0;
    double fy = 0;
    aspect_box_fraction(box, x, y, fx, fy);
    if (on_jump) {
        on_jump(std::max(0.0, std::min(1.0, fx)), std::max(0.0, std::min(1.0, fy)));
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
    paint_viewport(cr, width, height);
}

void DemandView::set_session(CitySession *session)
{
    session_ = session;
}

void DemandView::draw(cairo_t *cr, int width, int height) const
{
    const double w = width;
    const double h = height;
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

    const double gap = 5;
    const double col_w = (w - gap * 4) / 3.0;
    const double top = 6;
    const double meter_h = h - 28;
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
        cairo_set_font_size(cr, 14);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, letters[i], &ext);
        cairo_move_to(cr, x + (col_w - ext.width) / 2.0, h - 6);
        cairo_show_text(cr, letters[i]);
    }
}

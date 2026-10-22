// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "view_math.hpp"

#include <cairo.h>

#include <functional>
#include <vector>

class CitySession;

class MinimapView {
public:
    void set_session(CitySession *session);
    void set_viewport_provider(std::function<void(double &, double &, double &, double &)> provider);

    MinimapView() = default;
    ~MinimapView();
    MinimapView(const MinimapView &) = delete;
    MinimapView &operator=(const MinimapView &) = delete;

    void sync();
    bool press(double x, double y, int width, int height);
    void draw(cairo_t *cr, int width, int height);

    std::function<void(double, double)> on_jump;

private:
    AspectBox land_box(int width, int height) const;
    void paint_viewport(cairo_t *cr, int width, int height) const;

    CitySession *session_ = nullptr;
    std::function<void(double &, double &, double &, double &)> viewport_;
    cairo_surface_t *pixels_ = nullptr;
    std::vector<int> cached_raw_;
    int look_generation_ = -1;
};

class DemandView {
public:
    void set_session(CitySession *session);
    void draw(cairo_t *cr, int width, int height, float ui_scale) const;

private:
    CitySession *session_ = nullptr;
};

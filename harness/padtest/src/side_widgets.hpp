// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "view_math.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>

#include <functional>
#include <vector>

class CitySession;

class MinimapView {
public:
    void set_session(CitySession *session);
    void set_viewport_provider(std::function<void(double &, double &, double &, double &)> provider);

    void sync();
    bool press(double x, double y, int width, int height);
    void draw(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height);

    std::function<void(double, double)> on_jump;

private:
    AspectBox land_box(int width, int height) const;
    void paint_viewport(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height) const;

    CitySession *session_ = nullptr;
    std::function<void(double &, double &, double &, double &)> viewport_;
    Cairo::RefPtr<Cairo::ImageSurface> pixels_;
    std::vector<int> cached_raw_;
};

class DemandView {
public:
    void set_session(CitySession *session);
    void draw(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height) const;

private:
    CitySession *session_ = nullptr;
};

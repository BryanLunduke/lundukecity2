// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairo.h>

#include <string>
#include <vector>

class OverlayMap {
public:
    OverlayMap() = default;
    ~OverlayMap();
    OverlayMap(const OverlayMap &) = delete;
    OverlayMap &operator=(const OverlayMap &) = delete;

    void set_layer(CitySession::MapLayer layer);
    void set_session(CitySession *session);
    void sync();
    void draw(cairo_t *cr, int width, int height);
    bool click(double x, double y, int width, int height, double &fx, double &fy) const;

    const char *title() const;
    const char *legend() const;

private:
    bool density_layer() const;
    void rebuild();

    CitySession::MapLayer layer_ = CitySession::MapLayer::Power;
    CitySession *session_ = nullptr;
    cairo_surface_t *image_ = nullptr;
    std::vector<int> cells_;
    int peak_ = 1;
    unsigned map_serial_ = 0;
};

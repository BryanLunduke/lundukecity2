// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <string>
#include <vector>

class OverlayMap {
public:
    void set_layer(CitySession::MapLayer layer);
    void set_session(CitySession *session);
    void sync();
    void draw(const Cairo::RefPtr<Cairo::Context> &cr, int width, int height);
    bool click(double x, double y, int width, int height, double &fx, double &fy) const;

    const char *title() const;
    const char *legend() const;

private:
    bool density_layer() const;
    void rebuild();

    CitySession::MapLayer layer_ = CitySession::MapLayer::Power;
    CitySession *session_ = nullptr;
    Cairo::RefPtr<Cairo::ImageSurface> image_;
    std::vector<int> cells_;
    int peak_ = 1;
    unsigned map_serial_ = 0;
};

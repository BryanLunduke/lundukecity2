// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "engraved_map.hpp"
#include "view_math.hpp"

#include <cairo.h>

#include <functional>
#include <vector>

class CitySession;

// The colour the colour minimap paints one map tile. The New City land
// preview uses it too, so the preview looks like the map Generate builds.
void minimap_tile_rgb(int raw, double &r, double &g, double &b);

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
    // Pin the city to the top of a tall slot so the space under it can
    // carry the tool name. Clicks use the same box.
    void set_pin_top(bool on) { pin_top_ = on; }
    // Night: the land takes a softer version of the map's dusk tint, so the
    // minimap matches the city but stays readable. The view frame is not dimmed.
    void set_night(bool on) { night_ = on; }
    AspectBox content_box(int width, int height) const { return land_box(width, height); }
    // Engraved paper map instead of tile colours. The sheet reaches past the
    // widget: sheet_margin() on the left, top and right, sheet_below() under
    // it for the caption. Clicks still map only inside the land box.
    void set_engraved(bool on)
    {
        if (on && !engraved_) {
            engraved_primed_ = false;
        }
        engraved_ = on;
    }
    bool engraved() const { return engraved_; }
    // UI scale (1 at 100%), so rules and hatching keep their size.
    void set_unit(double unit) { unit_ = unit > 0.05 ? unit : 1.0; }
    // The overlay shown on the big map, printed as a plate on the paper.
    // layer is a CitySession::MapLayer.
    void set_overlay(bool on, int layer);
    int sheet_margin() const { return engraved::sheet_margin(unit_); }
    int sheet_below() const { return engraved::sheet_margin(unit_) + engraved::sheet_caption(unit_); }
    const engraved::Renderer &engraved_renderer() const { return engraved_map_; }

    std::function<void(double, double)> on_jump;

private:
    AspectBox land_box(int width, int height) const;
    void paint_viewport(cairo_t *cr, int width, int height) const;
    void sync_engraved();
    void draw_engraved(cairo_t *cr, int width, int height);

    CitySession *session_ = nullptr;
    std::function<void(double &, double &, double &, double &)> viewport_;
    cairo_surface_t *pixels_ = nullptr;
    std::vector<int> cached_raw_;
    int look_generation_ = -1;
    bool pin_top_ = false;
    bool night_ = false;
    bool engraved_ = false;
    double unit_ = 1.0;
    bool overlay_on_ = false;
    int overlay_layer_ = 0;
    int plate_layer_seen_ = -1;
    double plate_read_at_ = -1e9;
    unsigned engraved_serial_ = 0;
    bool engraved_primed_ = false;
    engraved::RebuildGate engraved_gate_;
    engraved::Renderer engraved_map_;
};

class DemandView {
public:
    void set_session(CitySession *session);
    void draw(cairo_t *cr, int width, int height, float ui_scale) const;

private:
    CitySession *session_ = nullptr;
};

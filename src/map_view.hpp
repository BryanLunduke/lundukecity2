// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>

#include <chrono>
#include <functional>
#include <vector>

class TileAtlas;

// Top-down city viewport. Tiles are drawn from the Micropolis 16-pixel atlas.
// The ARGB buffer is kept. A sync copies only tiles whose map word changed,
// plus unpowered zones the lightning blink toggles. The shell scrolls and
// composites this surface; there is no toolkit widget.
class MapView {
public:
    MapView();
    ~MapView();

    void set_session(CitySession *session);
    void set_tool(int engine_tool);

    void sync();
    void invalidate_all();
    int last_sync_blits() const { return last_sync_blits_; }

    void set_hover_tile(int tx, int ty);
    void clear_hover();

    void set_tile_size(int pixels);
    void set_shake(int dx, int dy);
    int tile_size() const { return tile_size_; }
    int pixel_width() const;
    int pixel_height() const;

    void set_active(bool active) { active_ = active; }
    bool active() const { return active_; }

    // Advance the unpowered-zone blink. Call from the frame loop.
    void tick(int elapsed_ms);

    void draw(const Cairo::RefPtr<Cairo::Context> &cr);

    // Coordinates are map-local pixels (after subtracting the scroll origin).
    bool press(double x, double y);
    void release();
    void motion(double x, double y, bool button_down);
    void leave();
    bool dragging() const { return dragging_; }

    // While a stroke is down, pan when the pointer sits in the viewport margin.
    // view_* is the pointer inside the visible map port, not map-local.
    bool edge_scroll(double view_x, double view_y, int view_w, int view_h, double &dx, double &dy) const;

    std::function<void(int, int)> on_tool_down;
    std::function<void(int, int, int, int)> on_tool_drag;

private:
    bool tile_at(double x, double y, int &tx, int &ty) const;
    void draw_footprint(const Cairo::RefPtr<Cairo::Context> &cr) const;
    void ensure_surface();
    int display_tile(int raw, const TileAtlas &atlas) const;
    void blit_tile(int x, int y, int shown, const TileAtlas &atlas);
    void follow_local_pointer(double x, double y, bool drag);

    CitySession *session_ = nullptr;
    int engine_tool_ = 6;
    int tile_size_ = 12;
    int shake_x_ = 0;
    int shake_y_ = 0;
    bool dragging_ = false;
    int last_x_ = -1;
    int last_y_ = -1;
    bool hover_valid_ = false;
    int hover_x_ = 0;
    int hover_y_ = 0;
    bool blink_on_ = false;
    int blink_ms_ = 0;
    bool atlas_was_loaded_ = false;
    int last_sync_blits_ = 0;
    bool active_ = true;
    std::vector<int> cached_raw_;
    std::vector<int> cached_shown_;
    Cairo::RefPtr<Cairo::ImageSurface> map_pixels_;
};

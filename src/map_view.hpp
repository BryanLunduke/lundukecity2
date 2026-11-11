// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairo.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

// Overlay for the tool under the pointer. The geometry is the tool footprint.
// mark: 0 hidden, 1 ready (solid corners), 2 caution (crossed corners),
// 3 blocked (hatch), 4 short of funds (broken corners).
struct MapCue {
    bool active = false;
    int mark = 0;
    int left = 0;
    int top = 0;
    int width = 1;
    int height = 1;
    std::string tag;
    // Special-building sheet to ghost in the footprint. -1 draws corners only.
    int sprite = -1;
    // Full rectangle, used when the bulldozer is over a multi-tile special.
    bool outline = false;
    bool zone = false;
    int zleft = 0;
    int ztop = 0;
    int zwidth = 0;
    int zheight = 0;
    bool receipt = false;
    bool receipt_ok = false;
    int rleft = 0;
    int rtop = 0;
    int rwidth = 0;
    int rheight = 0;
    std::string receipt_text;
};

class TileAtlas;

// Top-down city viewport. Tiles are drawn from the Micropolis 16-pixel atlas.
// The ARGB buffer is kept. A sync copies only tiles whose map word changed,
// plus unpowered zones the lightning blink toggles. The shell scrolls and
// composites this surface; there is no toolkit widget.
class MapView {
public:
    MapView();
    ~MapView();
    MapView(const MapView &) = delete;
    MapView &operator=(const MapView &) = delete;

    void set_session(CitySession *session);
    void set_tool(int engine_tool);

    void sync();
    void invalidate_all();
    int last_sync_blits() const { return last_sync_blits_; }

    void set_hover_tile(int tx, int ty);
    void clear_hover();
    bool hover_tile(int &tx, int &ty) const;
    // Tile where the current stroke began. Valid while dragging().
    bool stroke_origin(int &tx, int &ty) const;
    void set_cue(const MapCue &cue);
    const MapCue &cue() const { return cue_; }
    // Bulldoze dust, drawn under the tool tag so it cannot cover the label.
    void set_dust(bool on, int elapsed_ms, int tile_x, int tile_y);

    void set_tile_size(int pixels);
    void set_shake(int dx, int dy);
    int tile_size() const { return tile_size_; }
    int shake_x() const { return shake_x_; }
    int shake_y() const { return shake_y_; }
    int pixel_width() const;
    int pixel_height() const;

    void set_active(bool active) { active_ = active; }
    bool active() const { return active_; }

    // Advance the unpowered-zone blink. Returns true when the blink actually
    // changed a tile, so a paused city with nothing to flash can stay idle.
    bool tick(int elapsed_ms);
    // Milliseconds until the next blink flip. The shell sleeps on this.
    int blink_remaining_ms() const;

    void draw(cairo_t *cr);

    // Coordinates are map-local pixels (after subtracting the scroll origin).
    bool press(double x, double y);
    void release();
    void motion(double x, double y, bool button_down);
    void leave();
    bool dragging() const { return dragging_; }

    // While a stroke is down, pan when the pointer sits in the viewport margin.
    // view_* is the pointer inside the visible map port, not map-local.
    // Scrolling waits until the pointer has moved, then about 300 ms, and
    // then advances by time (not by frame). elapsed_ms is the frame slice.
    bool edge_scroll(double view_x, double view_y, int view_w, int view_h, int elapsed_ms, double &dx, double &dy);

    std::function<void(int, int)> on_tool_down;
    std::function<void(int, int, int, int)> on_tool_drag;

private:
    bool tile_at(double x, double y, int &tx, int &ty) const;
    void draw_footprint(cairo_t *cr) const;
    void draw_dust(cairo_t *cr) const;
    // 8 for the overview atlas, 16 for every larger zoom.
    int source_px() const;
    void ensure_surface();
    int display_tile(int raw, const TileAtlas &atlas) const;
    void blit_tile(int x, int y, int shown, const TileAtlas &atlas);
    void blit_special(int x, int y, int index, int cell);
    void follow_local_pointer(double x, double y, bool drag);

    CitySession *session_ = nullptr;
    int engine_tool_ = 6;
    // Default is 1:1 with the 16px atlas. Steps are 8, 16, 32, 48, and 64.
    int tile_size_ = 16;
    int shake_x_ = 0;
    int shake_y_ = 0;
    bool dragging_ = false;
    bool drag_moved_ = false;
    double press_local_x_ = 0;
    double press_local_y_ = 0;
    bool edge_holding_ = false;
    std::chrono::steady_clock::time_point edge_hold_start_{};
    int last_x_ = -1;
    int last_y_ = -1;
    bool hover_valid_ = false;
    int hover_x_ = 0;
    int hover_y_ = 0;
    int origin_x_ = -1;
    int origin_y_ = -1;
    MapCue cue_;
    bool dust_on_ = false;
    int dust_ms_ = 0;
    int dust_tx_ = 0;
    int dust_ty_ = 0;
    bool blink_on_ = false;
    int blink_ms_ = 0;
    bool atlas_was_loaded_ = false;
    int look_generation_ = -1;
    int last_sync_blits_ = 0;
    bool active_ = true;
    std::vector<int> cached_raw_;
    std::vector<int> cached_shown_;
    cairo_surface_t *map_pixels_ = nullptr;
};

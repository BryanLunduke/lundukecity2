// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "city_session.hpp"

#include <cairo.h>

#include <chrono>
#include <functional>
#include <string>
#include <cstdint>
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
    // Look-and-feel effects, a look_fx::Effects value. Off draws them still.
    void set_effects(int effects);
    int effects() const { return effects_; }
    // The ambient clock (fire smoke, and later water and clouds) runs only
    // while the city runs, so a paused city keeps a still frame.
    void advance_fx(int elapsed_ms, bool running);
    // A new ambient step is due and the last frame had something moving.
    bool fx_frame_due() const;
    // Night mode: darken the map and add the light layer (lit windows,
    // street lamps, fires). The tile art is not touched. hold hides it for
    // a frame, for example under a data map.
    void set_night(bool on);
    void set_night_hold(bool hold) { night_hold_ = hold; }
    bool night() const { return night_; }
    // True when the dusk tint is on screen (night, and no overlay holding it off).
    bool night_shown() const { return night_ && !night_hold_; }
    // Slate look for the price and reason tag at the cursor.
    void set_slate_tags(bool on) { slate_tags_ = on; }
    // Smallest font size, in pixels, for the placement tags over the map
    // ("Can't build on water", the price). The handheld raises it so a tag
    // reads at arm's length; the desktop keeps 11.
    void set_tag_min_px(double px) { tag_min_px_ = px > 1.0 ? px : 1.0; }

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
    // Tiles inside the current clip, padded by `pad` and kept on the map.
    struct TileSpan {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
    };
    TileSpan visible_tiles(cairo_t *cr, int source, int pad) const;
    void draw_fire(cairo_t *cr, int source, const TileSpan &span, bool dusk);
    bool draw_water(cairo_t *cr, const TileSpan &span);
    // Per art pixel at 16px: bits 0-4 a water colour index + 1 (0 is not
    // water), bit 6 shore (water beside land), bit 7 solid (a pixel of a
    // building cell, which casts a shadow). Rebuilt for changed tiles.
    void refresh_pixel_classes();
    void learn_fx_palette();
    std::vector<std::uint8_t> pix_class_;
    std::vector<int> class_dirty_;
    bool class_all_ = true;
    int fx_palette_generation_ = -1;
    // Water colours of the drawn sheet, darkest first, with the lighter
    // colour each one shimmers to. Shimmer never leaves the sheet palette.
    std::vector<std::uint32_t> water_rgb_;
    std::vector<std::uint32_t> water_lift_;
    std::uint32_t foam_rgb_ = 0;
    // 1 for a map cell drawn from a landmark sprite (always a caster).
    std::vector<std::uint8_t> special_cell_;
    // Soft lower-right drop shadows at art resolution, premultiplied.
    cairo_surface_t *shade_pixels_ = nullptr;
    void refresh_shade(const std::vector<int> &tiles);
    void draw_shadows(cairo_t *cr);
    cairo_surface_t *fx_pixels_ = nullptr;
    int fx_cache_step_ = -1;
    TileSpan fx_cache_span_{};
    bool fx_water_seen_ = false;
    bool classic_fx() const;
    int effects_ = 0;
    int fx_ms_ = 0;
    mutable int fx_drawn_step_ = -1;
    bool fx_animated_ = false;
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
    void draw_night(cairo_t *cr, int source);
    void tint_sprites(cairo_t *cr, double sprite_to_source);
    bool night_ = false;
    bool night_hold_ = false;
    bool slate_tags_ = false;
    double tag_min_px_ = 11.0;
    bool light_dirty_ = true;
    std::chrono::steady_clock::time_point light_built_{};
    cairo_surface_t *light_pixels_ = nullptr;
    // Lit windows at full art resolution, from window masks found in the
    // active tile sheet (the sheet itself is never changed).
    cairo_surface_t *window_pixels_ = nullptr;
    // The day map times the dusk tint, kept tile by tile beside map_pixels_
    // so night costs a plain copy instead of a full-view MULTIPLY.
    cairo_surface_t *night_pixels_ = nullptr;
    // Glow and windows flattened at art resolution after each rebuild, so
    // a frame adds one surface.
    cairo_surface_t *lamp_pixels_ = nullptr;
    // Next art row to flatten after a throttled rebuild; the work is spread
    // over a few frames. -1 when the flattened layer is current.
    int flatten_row_ = -1;
    bool night_stale_ = true;
    std::vector<int> night_dirty_;
    void refresh_night_base();
    void tint_tile(int x, int y);
    std::vector<std::uint16_t> window_masks_;
    std::vector<int> window_keys_;
    // Data Center facade glass, kTile rows per map cell, restamped with
    // every night rebuild from datacenter-glass.png.
    std::vector<std::uint16_t> glass_cells_;
    int window_masks_generation_ = -1;
    bool window_masks_loaded_ = false;
};

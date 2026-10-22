#pragma once

#include <algorithm>
#include <cmath>

// How the city map sits in the view. Tiles grow to cover a large window and
// never shrink a closer zoom. A leftover band is centered. The R/C/I meters
// stay a fixed height instead of stretching with the window.

// Atlas tiles are 16px. Drawn sizes of 16, 32, 48, and 64 are whole multiples
// of that source, so nearest-neighbour sampling copies each pixel as an N×N
// block. 8px is the overview: a half-size atlas filtered once at load, drawn
// 1:1. Anything else (12, 14, 18, …) drops rows and columns.
inline constexpr int kMapTileSource = 16;
inline constexpr int kMapTileOverview = 8;
inline constexpr int kMinMapTile = kMapTileOverview;
inline constexpr int kMaxMapTile = 64;
// The toolbar is nine rows. A 176px graph crowds it on a 1280x800 window.
inline constexpr int kMaxDemandPx = 128;

inline int tile_size_to_cover(int view_w, int view_h, int world_w, int world_h, int current, int min_tile, int max_tile)
{
    if (world_w < 1) {
        world_w = 1;
    }
    if (world_h < 1) {
        world_h = 1;
    }
    if (min_tile < 1) {
        min_tile = 1;
    }
    if (max_tile < min_tile) {
        max_tile = min_tile;
    }
    const int need_w = (std::max(0, view_w) + world_w - 1) / world_w;
    const int need_h = (std::max(0, view_h) + world_h - 1) / world_h;
    int need = need_w > need_h ? need_w : need_h;
    if (current > need) {
        need = current;
    }
    if (need < min_tile) {
        need = min_tile;
    }
    if (need > max_tile) {
        need = max_tile;
    }
    return need;
}

// Smallest legal drawn size that is at least `pixels`. Covering a large
// window rounds up, so the city still fills the view. 8 is the only step
// below the 16px source.
inline int snap_map_tile(int pixels)
{
    if (pixels > kMaxMapTile) {
        return kMaxMapTile;
    }
    if (pixels <= kMapTileOverview) {
        return kMapTileOverview;
    }
    const int snapped = ((pixels + kMapTileSource - 1) / kMapTileSource) * kMapTileSource;
    if (snapped > kMaxMapTile) {
        return kMaxMapTile;
    }
    return snapped;
}

// One zoom step. The sign of `delta` is the direction. Steps are 8, 16, 32,
// 48, and 64.
inline int step_map_tile(int current, int delta)
{
    const int size = snap_map_tile(current);
    if (delta > 0) {
        if (size < kMapTileSource) {
            return kMapTileSource;
        }
        return snap_map_tile(size + 1);
    }
    if (delta < 0) {
        if (size <= kMapTileSource) {
            return kMapTileOverview;
        }
        return size - kMapTileSource;
    }
    return size;
}

// Right trigger zooms in, left trigger zooms out. One step per press: holding
// the trigger does not repeat until it is released.
inline constexpr float kTriggerZoomPress = 0.6f;
inline constexpr float kTriggerZoomRelease = 0.4f;

inline bool trigger_zoom_edge(float amount, bool &latched)
{
    if (amount >= kTriggerZoomPress) {
        if (latched) {
            return false;
        }
        latched = true;
        return true;
    }
    if (amount <= kTriggerZoomRelease) {
        latched = false;
    }
    return false;
}

// A pinch step waits until the finger distance has grown to 1.4× or shrunk
// to 0.7× the distance at the start of the gesture, or at the last step.
inline constexpr float kPinchZoomIn = 1.4f;
inline constexpr float kPinchZoomOut = 0.7f;

inline int pinch_zoom_step(float distance, float &baseline)
{
    if (!(distance > 1.0f)) {
        return 0;
    }
    if (!(baseline > 1.0f)) {
        baseline = distance;
        return 0;
    }
    if (distance > baseline * kPinchZoomIn) {
        baseline = distance;
        return 1;
    }
    if (distance < baseline * kPinchZoomOut) {
        baseline = distance;
        return -1;
    }
    return 0;
}

// Device-pixel origin of the scrolled map. Fractional scroll would make a
// 2× or 3× nearest-neighbour scale paint some source pixels 1px wide and
// others 3px wide.
inline double snapped_map_origin(double view, int letterbox, double scroll)
{
    return std::floor(view + static_cast<double>(letterbox) - scroll);
}

// Minimap rectangle as fractions of the city. Scroll and the map size are
// both in the current zoom's pixels, including the 8px overview.
inline void map_viewport_fraction(double scroll_x, double scroll_y, int map_w, int map_h, int view_w, int view_h,
                                  double &fx, double &fy, double &fw, double &fh)
{
    const double mw = std::max(1, map_w);
    const double mh = std::max(1, map_h);
    fx = scroll_x / mw;
    fy = scroll_y / mh;
    fw = static_cast<double>(view_w) / mw;
    fh = static_cast<double>(view_h) / mh;
}

inline int map_letterbox(int view, int map_pixels)
{
    if (map_pixels >= view) {
        return 0;
    }
    return (view - map_pixels) / 2;
}

// Fraction of the map under a window point. `point` and `view_origin` are
// window pixels. `letterbox` is the grey band on that side, and `scroll` is
// in map pixels. Leaving the band in the sum treats the margin as city, so a
// centred 8px map reports about 0.6 instead of 0.5.
inline double map_point_fraction(double point, double view_origin, int letterbox, double scroll, int map_pixels)
{
    const double span = std::max(1, map_pixels);
    return (scroll + (point - view_origin) - static_cast<double>(letterbox)) / span;
}

// Scroll that keeps `fraction` under the same window point. `letterbox` is
// the band at the size being scrolled.
inline double scroll_for_map_fraction(double fraction, double point, double view_origin, int letterbox, int map_pixels)
{
    return static_cast<double>(letterbox) + fraction * std::max(1, map_pixels) - (point - view_origin);
}

// A gamepad cursor inside the map port but on the grey band. `push` is the
// stick on this axis: a push toward the outside steps off the port, and a
// resting stick sits on the map instead of the band.
inline double clamp_cursor_off_letterbox(double coord, double push, double view0, double view1, double map0, double map1)
{
    if (coord < view0 || coord >= view1 || !(map1 > map0)) {
        return coord;
    }
    if (coord >= map0 && coord < map1) {
        return coord;
    }
    if (coord < map0) {
        if (push < -0.15) {
            return view0 - 1.0;
        }
        return map0;
    }
    if (push > 0.15) {
        return view1;
    }
    return map1 - 1.0;
}

inline void clamp_viewport_span(double &origin, double &span)
{
    if (origin < 0) {
        origin = 0;
    }
    if (origin > 1) {
        origin = 1;
    }
    if (span < 0) {
        span = 0;
    }
    if (span > 1) {
        span = 1;
    }
    if (origin + span > 1) {
        span = 1 - origin;
    }
}

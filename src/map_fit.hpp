#pragma once

#include <algorithm>
#include <cmath>

// How the city map sits in the view. Tiles grow to cover a large window and
// never shrink a closer zoom. A leftover band is centered. The R/C/I meters
// stay a fixed height instead of stretching with the window.

// Atlas tiles are 16px. Drawn sizes are whole multiples of that source, so
// nearest-neighbour sampling copies each pixel as an N×N block. A 12px tile
// (and 14, 18, 22, …) drops rows and columns and breaks road dashes and the
// zone letters.
inline constexpr int kMapTileSource = 16;
inline constexpr int kMinMapTile = kMapTileSource;
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
// window rounds up, so the city still fills the view.
inline int snap_map_tile(int pixels)
{
    if (pixels < kMinMapTile) {
        return kMinMapTile;
    }
    if (pixels > kMaxMapTile) {
        return kMaxMapTile;
    }
    const int snapped = ((pixels + kMapTileSource - 1) / kMapTileSource) * kMapTileSource;
    if (snapped > kMaxMapTile) {
        return kMaxMapTile;
    }
    return snapped;
}

// One zoom step toward the next or previous whole multiple. The sign of
// `delta` is the direction; the magnitude is the caller's "one notch".
inline int step_map_tile(int current, int delta)
{
    const int size = snap_map_tile(current);
    if (delta > 0) {
        return snap_map_tile(size + 1);
    }
    if (delta < 0) {
        return size <= kMinMapTile ? kMinMapTile : size - kMapTileSource;
    }
    return size;
}

// Device-pixel origin of the scrolled map. Fractional scroll would make a
// 2× or 3× nearest-neighbour scale paint some source pixels 1px wide and
// others 3px wide.
inline double snapped_map_origin(double view, int letterbox, double scroll)
{
    return std::floor(view + static_cast<double>(letterbox) - scroll);
}

inline int map_letterbox(int view, int map_pixels)
{
    if (map_pixels >= view) {
        return 0;
    }
    return (view - map_pixels) / 2;
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

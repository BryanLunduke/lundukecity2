#pragma once

#include <algorithm>

// How the city map sits in the view. Tiles grow to cover a large window and
// never shrink a closer zoom. A leftover band is centered. The R/C/I meters
// stay a fixed height instead of stretching with the window.

inline constexpr int kMinMapTile = 6;
inline constexpr int kMaxMapTile = 64;
inline constexpr int kMaxDemandPx = 176;

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

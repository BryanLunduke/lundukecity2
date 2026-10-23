#pragma once
// Keep a window's frame inside a work area. Toolkit-free so tests and the
// SDL shell share the same clamp. SDL windows have no separate title-bar
// inset inside the client size we draw, but the constants stay so a frame
// that a window manager adds around the client is still accounted for.

#include <algorithm>

namespace window_fit {

constexpr int kFrameTop = 40;
constexpr int kFrameBottom = 8;
constexpr int kFrameSide = 8;

inline void clamp_origin(int &x, int &y, int w, int h, int ax, int ay, int aw, int ah)
{
    const int max_x = ax + aw - w - 2 * kFrameSide;
    const int max_y = ay + ah - h - kFrameTop - kFrameBottom;
    x = std::max(ax, std::min(x, max_x));
    y = std::max(ay, std::min(y, max_y));
}

} // namespace window_fit

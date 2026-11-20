// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Smooth heat map for the data views (pollution, crime, land value and the
// rest). The simulator keeps these on 2x2-tile blocks; drawn as squares they
// read as noise. The field is blurred, sampled between tile centers and
// drawn with soft colour plus contour lines. Pure arithmetic, no cairo.
namespace heat_field {

// Separable Gaussian blur in place. Edges clamp, so a hot spot at the map
// border does not fade into an invisible frame.
inline void blur(std::vector<float> &field, int w, int h, double sigma)
{
    if (w <= 0 || h <= 0 || sigma <= 0.0 || static_cast<int>(field.size()) < w * h) {
        return;
    }
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 2.5)));
    std::vector<float> kernel(static_cast<std::size_t>(radius * 2 + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = static_cast<float>(std::exp(-(i * i) / (2.0 * sigma * sigma)));
        kernel[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (float &v : kernel) {
        v /= sum;
    }
    std::vector<float> tmp(field.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int sx = std::min(w - 1, std::max(0, x + i));
                acc += field[static_cast<std::size_t>(y * w + sx)] * kernel[static_cast<std::size_t>(i + radius)];
            }
            tmp[static_cast<std::size_t>(y * w + x)] = acc;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i) {
                const int sy = std::min(h - 1, std::max(0, y + i));
                acc += tmp[static_cast<std::size_t>(sy * w + x)] * kernel[static_cast<std::size_t>(i + radius)];
            }
            field[static_cast<std::size_t>(y * w + x)] = acc;
        }
    }
}

// Value at a point in tile units, where (x + 0.5, y + 0.5) is the center of
// tile (x, y). Bilinear between the four nearest centers.
inline float sample(const std::vector<float> &field, int w, int h, double fx, double fy)
{
    if (w <= 0 || h <= 0) {
        return 0.0f;
    }
    const double gx = std::min(static_cast<double>(w - 1), std::max(0.0, fx - 0.5));
    const double gy = std::min(static_cast<double>(h - 1), std::max(0.0, fy - 0.5));
    const int x0 = static_cast<int>(gx);
    const int y0 = static_cast<int>(gy);
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const float tx = static_cast<float>(gx - x0);
    const float ty = static_cast<float>(gy - y0);
    auto at = [&](int x, int y) { return field[static_cast<std::size_t>(y * w + x)]; };
    const float top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
    const float bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

// Contour band for a value in 0..1 with `levels` steps: 0 below the first
// line, levels-1 at the top. A pixel whose band differs from its right or
// lower neighbour sits on a contour line.
inline int band(float value, int levels)
{
    if (levels < 2) {
        return 0;
    }
    const float clamped = std::min(0.9999f, std::max(0.0f, value));
    return static_cast<int>(clamped * static_cast<float>(levels));
}

// Heat colour, straight (not premultiplied) 0..1, for t in 0..1: clear,
// then pale yellow through orange to deep red. Alpha stays well under 1
// so the city shows through.
inline void ramp(float t, float &r, float &g, float &b, float &a)
{
    t = std::min(1.0f, std::max(0.0f, t));
    if (t < 0.04f) {
        r = g = b = a = 0.0f;
        return;
    }
    r = 1.0f;
    g = 0.92f - 0.80f * t;
    b = 0.35f - 0.30f * t;
    a = 0.18f + 0.50f * std::sqrt(t);
}

// Shrinking blocks on the Growth map.
inline void cool_ramp(float t, float &r, float &g, float &b, float &a)
{
    t = std::min(1.0f, std::max(0.0f, t));
    if (t < 0.04f) {
        r = g = b = a = 0.0f;
        return;
    }
    r = 0.20f - 0.15f * t;
    g = 0.55f + 0.10f * t;
    b = 1.0f;
    a = 0.18f + 0.50f * std::sqrt(t);
}

} // namespace heat_field

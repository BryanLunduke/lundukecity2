// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The Classic look has no alpha. A semi-transparent effect there is drawn
// as opaque palette pixels through a 4x4 ordered dither: coverage instead
// of blending, the way SceneFade and the modal dim already work.

#include "look_fx.hpp"

#include <cairo.h>

namespace look_fx {

// Repeating 4x4 A8 mask with `level` of 16 pixels set. Cached; do not free.
inline cairo_surface_t *dither_surface(int level)
{
    static cairo_surface_t *cache[17] = {};
    level = level < 0 ? 0 : (level > 16 ? 16 : level);
    if (cache[level] == nullptr) {
        cairo_surface_t *mask = cairo_image_surface_create(CAIRO_FORMAT_A8, 4, 4);
        unsigned char *pixels = cairo_image_surface_get_data(mask);
        const int stride = cairo_image_surface_get_stride(mask);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                pixels[row * stride + col] = bayer_rank(col, row) < level ? 255 : 0;
            }
        }
        cairo_surface_mark_dirty(mask);
        cache[level] = mask;
    }
    return cache[level];
}

// Paint the current source through the dither for this coverage (0..1).
// The clip decides where. The mask is in user space, so a zoomed map gets
// fat dither pixels like its tiles.
inline void dither_paint(cairo_t *cr, double coverage)
{
    const int level = static_cast<int>(coverage * 16.0 + 0.5);
    if (level <= 0) {
        return;
    }
    if (level >= 16) {
        cairo_paint(cr);
        return;
    }
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(dither_surface(level));
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    cairo_mask(cr, pattern);
    cairo_pattern_destroy(pattern);
}

// Fill the current path with a colour at this opacity: blended normally,
// or through the dither when `dither` (Classic) is set.
inline void fill_fx(cairo_t *cr, double r, double g, double b, double alpha, bool dither)
{
    if (!dither) {
        cairo_set_source_rgba(cr, r, g, b, alpha);
        cairo_fill(cr);
        return;
    }
    cairo_save(cr);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, r, g, b);
    dither_paint(cr, alpha);
    cairo_restore(cr);
}

} // namespace look_fx

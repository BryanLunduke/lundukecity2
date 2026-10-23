// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "menu_model.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <string>

struct TextMetrics {
    int width = 0;
    int height = 0;
};

inline TextMetrics measure_text(cairo_t *cr, const char *font, const std::string &text, int wrap_width)
{
    TextMetrics metrics;
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    if (wrap_width > 0) {
        pango_layout_set_width(layout, wrap_width * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_layout_get_pixel_size(layout, &metrics.width, &metrics.height);
    pango_font_description_free(desc);
    g_object_unref(layout);
    return metrics;
}

inline void paint_text(cairo_t *cr, const char *font, const std::string &text, double x, double y, double r,
                       double g, double b, int wrap_width)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    if (wrap_width > 0) {
        pango_layout_set_width(layout, wrap_width * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_text(layout, text.c_str(), -1);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
}

// Draws a menu label. When show_underline is set, the mnemonic letter is
// underlined the way a GTK menu does after Alt.
inline void paint_mnemonic(cairo_t *cr, const char *font, const char *label, double x, double y, bool show_underline,
                           double r, double g, double b)
{
    const std::string plain = strip_mnemonic(label);
    paint_text(cr, font, plain, x, y, r, g, b, -1);
    if (!show_underline || label == nullptr) {
        return;
    }
    const char mark = mnemonic_of(label);
    if (mark == 0) {
        return;
    }
    int index = -1;
    int seen = 0;
    for (const char *p = label; *p != '\0'; ++p) {
        if (*p == '_' && p[1] != '\0' && p[1] != '_') {
            index = seen;
            break;
        }
        if (*p == '_' && p[1] == '_') {
            ++p;
        }
        ++seen;
    }
    if (index < 0 || index >= static_cast<int>(plain.size())) {
        return;
    }
    const TextMetrics prefix = measure_text(cr, font, plain.substr(0, static_cast<std::size_t>(index)), -1);
    const TextMetrics letter = measure_text(cr, font, plain.substr(static_cast<std::size_t>(index), 1), -1);
    const TextMetrics all = measure_text(cr, font, plain, -1);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x + prefix.width, y + all.height - 1.0);
    cairo_line_to(cr, x + prefix.width + std::max(letter.width, 1), y + all.height - 1.0);
    cairo_stroke(cr);
}

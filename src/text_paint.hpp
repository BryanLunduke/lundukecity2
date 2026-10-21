// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "menu_model.hpp"

#include <pango/pangocairo.h>

#ifdef LUNDUKE_HAS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

struct TextMetrics {
    int width = 0;
    int height = 0;
};

struct TextCacheEntry {
    std::string font;
    std::string text;
    int wrap = -1;
    PangoLayout *layout = nullptr;
};

inline std::vector<TextCacheEntry> &text_cache()
{
    static std::vector<TextCacheEntry> cache;
    return cache;
}

inline int text_cache_entries()
{
    return static_cast<int>(text_cache().size());
}

inline void release_text_cache()
{
    for (TextCacheEntry &entry : text_cache()) {
        if (entry.layout != nullptr) {
            g_object_unref(entry.layout);
            entry.layout = nullptr;
        }
    }
    text_cache().clear();
}

// Drop the thread's Cairo font map, then fontconfig's cache. Call only from
// Shell::shutdown, after release_text_cache(), and never from a unit test:
// FcFini() ends fontconfig for the whole process.
inline void release_fontconfig()
{
#ifdef LUNDUKE_HAS_FONTCONFIG
    pango_cairo_font_map_set_default(nullptr);
    FcFini();
#endif
}

// Layouts are reused across frames. A paused city otherwise built thousands
// of PangoLayouts a second.
inline PangoLayout *cached_layout(cairo_t *cr, const char *font, const std::string &text, int wrap_width)
{
    const char *face = font != nullptr ? font : "";
    for (TextCacheEntry &entry : text_cache()) {
        if (entry.wrap == wrap_width && entry.font == face && entry.text == text && entry.layout != nullptr) {
            pango_cairo_update_layout(cr, entry.layout);
            return entry.layout;
        }
    }
    auto &cache = text_cache();
    if (cache.size() >= 512) {
        const std::size_t drop = cache.size() / 2;
        for (std::size_t i = 0; i < drop; ++i) {
            if (cache[i].layout != nullptr) {
                g_object_unref(cache[i].layout);
            }
        }
        cache.erase(cache.begin(), cache.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    PangoFontMap *map = pango_cairo_font_map_get_default();
    PangoContext *context = pango_font_map_create_context(map);
    PangoLayout *layout = pango_layout_new(context);
    g_object_unref(context);
    PangoFontDescription *desc = pango_font_description_from_string(face);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    if (wrap_width > 0) {
        pango_layout_set_width(layout, wrap_width * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_text(layout, text.c_str(), -1);
    pango_cairo_update_layout(cr, layout);
    cache.push_back(TextCacheEntry{face, text, wrap_width, layout});
    return layout;
}

inline TextMetrics measure_text(cairo_t *cr, const char *font, const std::string &text, int wrap_width)
{
    TextMetrics metrics;
    PangoLayout *layout = cached_layout(cr, font, text, wrap_width);
    pango_layout_get_pixel_size(layout, &metrics.width, &metrics.height);
    return metrics;
}

inline void paint_text(cairo_t *cr, const char *font, const std::string &text, double x, double y, double r,
                       double g, double b, int wrap_width)
{
    PangoLayout *layout = cached_layout(cr, font, text, wrap_width);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
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

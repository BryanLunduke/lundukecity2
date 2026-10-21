// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "tool_palette.hpp"

#include "assets.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

void box(cairo_t *cr, double x, double y, double w, double h, double r, double g,
         double b)
{
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

// Original 40px sprites in data/tool-icons. Nearest-neighbor only. The
// sprite fills a 40px slot. A larger slot uses a whole multiple of that
// size, and a smaller slot (UI scale under 1) is the only case that shrinks.
struct ToolIcons {
    bool tried = false;
    cairo_surface_t *image[17] = {};

    ~ToolIcons()
    {
        for (cairo_surface_t *surface : image) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
        }
    }
};

ToolIcons &tool_icons()
{
    static ToolIcons icons;
    if (icons.tried) {
        return icons;
    }
    icons.tried = true;
    static const char *names[17] = {
        "bulldozer", "road",     "rail",     "wire",    "park",       "residential", "commercial", "industrial",
        "police",    "fire",     "stadium",  "seaport", "coal",       "nuclear",     "airport",    "special",
        "query",
    };
    for (int i = 0; i < 17; ++i) {
        const std::string path = ui_data_file(std::string("tool-icons/") + names[i] + ".png");
        if (path.empty()) {
            continue;
        }
        cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
        if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            continue;
        }
        icons.image[i] = surface;
    }
    return icons;
}

void paint_icon(cairo_t *cr, cairo_surface_t *icon, int index, double x, double y, double s)
{
    if (s < 4.0) {
        return;
    }
    if (icon == nullptr) {
        paint_tool_fallback(cr, index, x, y, s);
        return;
    }
    const int iw = cairo_image_surface_get_width(icon);
    const int ih = cairo_image_surface_get_height(icon);
    if (iw < 1 || ih < 1) {
        paint_tool_fallback(cr, index, x, y, s);
        return;
    }
    const double scale = icon_pixel_scale(std::max(iw, ih), static_cast<int>(s));
    const double dw = static_cast<double>(iw) * scale;
    const double dh = static_cast<double>(ih) * scale;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, s, s);
    cairo_clip(cr);
    cairo_translate(cr, std::floor(x + (s - dw) / 2.0), std::floor(y + (s - dh) / 2.0));
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, icon, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

} // namespace

ToolPalette::ToolPalette()
{
    selected_ = kDefaultToolIndex;
}

int ToolPalette::width_for(int cell)
{
    return kPad * 2 + kCols * cell;
}

int ToolPalette::height_for(int cell)
{
    const int rows = (kToolCount + kCols - 1) / kCols;
    return kPad * 2 + rows * cell;
}

void ToolPalette::set_selected(int index)
{
    if (index < 0 || index >= kToolCount || index == selected_) {
        return;
    }
    selected_ = index;
    if (on_selected) {
        on_selected(selected_);
    }
}

int ToolPalette::index_at(double x, double y) const
{
    if (x < kPad || y < kPad) {
        return -1;
    }
    const int col = static_cast<int>(x - kPad) / cell_;
    const int row = static_cast<int>(y - kPad) / cell_;
    if (col < 0 || row < 0 || col >= kCols) {
        return -1;
    }
    const int index = row * kCols + col;
    if (index < 0 || index >= kToolCount) {
        return -1;
    }
    return index;
}

const char *ToolPalette::tooltip() const
{
    if (hover_ < 0) {
        return nullptr;
    }
    return tool_by_index(hover_)->hint;
}

bool ToolPalette::press(double x, double y)
{
    const int index = index_at(x, y);
    if (index < 0) {
        return false;
    }
    if (index != selected_) {
        selected_ = index;
        if (on_selected) {
            on_selected(selected_);
        }
    }
    return true;
}

void ToolPalette::motion(double x, double y)
{
    hover_ = index_at(x, y);
}

void ToolPalette::leave()
{
    hover_ = -1;
}

cairo_surface_t *tool_icon_named(const char *stem)
{
    struct Slot {
        const char *stem = nullptr;
        cairo_surface_t *image = nullptr;
        bool tried = false;
    };
    struct Cache {
        Slot slots[8];
        ~Cache()
        {
            for (Slot &slot : slots) {
                if (slot.image != nullptr) {
                    cairo_surface_destroy(slot.image);
                }
            }
        }
    };
    static Cache cache;
    if (stem == nullptr || stem[0] == '\0') {
        return nullptr;
    }
    Slot *free_slot = nullptr;
    for (Slot &slot : cache.slots) {
        if (slot.tried && slot.stem != nullptr && std::string(slot.stem) == stem) {
            return slot.image;
        }
        if (!slot.tried && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (free_slot == nullptr) {
        return nullptr;
    }
    free_slot->tried = true;
    free_slot->stem = stem;
    const std::string path = ui_data_file(std::string("tool-icons/") + stem + ".png");
    if (path.empty()) {
        return nullptr;
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        return nullptr;
    }
    free_slot->image = surface;
    return surface;
}

void ToolPalette::draw(cairo_t *cr) const
{
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const int rows = (kToolCount + kCols - 1) / kCols;
    box(cr, 0, 0, kPad * 2 + kCols * cell_, kPad * 2 + rows * cell_, 0xED / 255.0, 0xEC / 255.0, 0xEB / 255.0);

    for (int i = 0; i < kToolCount; ++i) {
        const int col = i % kCols;
        const int row = i / kCols;
        const double x = kPad + col * cell_ + 1;
        const double y = kPad + row * cell_ + 1;
        const double bs = cell_ - 2;
        const bool pressed = i == selected_;
        const bool hover = i == hover_ && !pressed;

        const double light = hover ? 1.0 : 0.96;
        const double dark = 0.28;
        const double face_r = pressed ? 1.0 : (hover ? 0.93 : 0.82);
        const double face_g = pressed ? 0.86 : (hover ? 0.93 : 0.82);
        const double face_b = pressed ? 0.15 : (hover ? 0.93 : 0.82);

        if (pressed) {
            box(cr, x, y, bs, bs, dark, dark, dark);
            box(cr, x + 2, y + bs - 2, bs - 2, 2, light, light, light);
            box(cr, x + bs - 2, y + 2, 2, bs - 4, light, light, light);
            box(cr, x + 2, y + 2, bs - 4, bs - 4, face_r, face_g, face_b);
            cairo_set_source_rgb(cr, 0.05, 0.05, 0.05);
            cairo_set_line_width(cr, 1);
            cairo_rectangle(cr, x + 2.5, y + 2.5, bs - 6, bs - 6);
            cairo_stroke(cr);
        } else {
            box(cr, x, y, bs, bs, light, light, light);
            box(cr, x, y + bs - 2, bs, 2, dark, dark, dark);
            box(cr, x + bs - 2, y, 2, bs, dark, dark, dark);
            box(cr, x + 1, y + bs - 3, bs - 3, 1, 0.50, 0.50, 0.50);
            box(cr, x + bs - 3, y + 1, 1, bs - 3, 0.50, 0.50, 0.50);
            box(cr, x + 2, y + 2, bs - 5, bs - 5, face_r, face_g, face_b);
        }

        const double shift = pressed ? 1.0 : 0.0;
        cairo_surface_t *icon = (i >= 0 && i < 17) ? tool_icons().image[i] : nullptr;
        // 3px inside the bevel. At the default 48px cell the slot is 40px,
        // which is the authored sprite size, so the icon fills the face.
        paint_icon(cr, icon, i, x + 3 + shift, y + 3 + shift, bs - 6);
    }
}

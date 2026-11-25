// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "tool_palette.hpp"

#include "assets.hpp"
#include "tool_groups.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr double kPiSlate = 3.14159265358979323846;

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
    cairo_surface_t *image[kToolIconCount] = {};

    ~ToolIcons()
    {
        for (cairo_surface_t *surface : image) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
        }
    }
};

} // namespace

const char *tool_icon_stem(int index)
{
    static const char *names[kToolIconCount] = {
        "bulldozer", "road",    "rail",         "wire",    "residential", "commercial", "industrial",
        "datacenter", "park",   "bigpark",      "fountain", "fire",       "police",     "prison",
        "coal",      "nuclear", "micronuclear", "zoo",     "amusement",   "casino",     "stadium",
        "marina",    "library", "mayor",        "airport", "seaport",     "inspect",
    };
    if (index < 0 || index >= kToolIconCount) {
        return "";
    }
    return names[index];
}

namespace {

ToolIcons &tool_icons()
{
    static ToolIcons icons;
    if (icons.tried) {
        return icons;
    }
    icons.tried = true;
    for (int i = 0; i < kToolIconCount; ++i) {
        const std::string path = ui_data_file(std::string("tool-icons/") + tool_icon_stem(i) + ".png");
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
    bool smooth = false;
    const double scale = icon_fit_scale(std::max(iw, ih), static_cast<int>(s), smooth);
    const double dw = static_cast<double>(iw) * scale;
    const double dh = static_cast<double>(ih) * scale;
    cairo_save(cr);
    cairo_rectangle(cr, x, y, s, s);
    cairo_clip(cr);
    cairo_translate(cr, std::floor(x + (s - dw) / 2.0), std::floor(y + (s - dh) / 2.0));
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, icon, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), smooth ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

} // namespace

ToolPalette::ToolPalette()
{
    selected_ = kDefaultToolIndex;
}

void ToolPalette::set_columns(int columns)
{
    if (columns < 2) {
        columns = 2;
    }
    if (columns > 4) {
        columns = 4;
    }
    columns_ = columns;
}

int ToolPalette::width_for(int cell, int columns)
{
    if (columns < 2) {
        columns = 2;
    }
    return kPad * 2 + columns * cell;
}

int ToolPalette::height_for(int cell, int columns)
{
    if (columns < 2) {
        columns = 2;
    }
    const int rows = (kPaletteToolCount + columns - 1) / columns;
    return kPad * 2 + rows * cell;
}

int ToolPalette::grouped_height_for(int cell, int columns, int header)
{
    if (columns < 2) {
        columns = 2;
    }
    return kPad * 2 + tool_groups::grid_height(columns, cell, header);
}

void ToolPalette::set_style(bool slate, int header)
{
    slate_ = slate;
    header_ = std::max(0, header);
}

int ToolPalette::height() const
{
    return header_ > 0 ? grouped_height_for(cell_, columns_, header_) : height_for(cell_, columns_);
}

bool ToolPalette::cell_origin(int index, double &x, double &y) const
{
    if (index < 0 || index >= kPaletteToolCount) {
        return false;
    }
    if (header_ > 0) {
        int gx = 0;
        int gy = 0;
        if (!tool_groups::slot(index, columns_, cell_, header_, gx, gy)) {
            return false;
        }
        x = kPad + gx;
        y = kPad + gy;
        return true;
    }
    x = kPad + (index % columns_) * cell_;
    y = kPad + (index / columns_) * cell_;
    return true;
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
    if (header_ > 0) {
        const int index = tool_groups::index_at(static_cast<int>(x - kPad), static_cast<int>(y - kPad), columns_, cell_,
                                                header_);
        return index >= 0 && index < kPaletteToolCount ? index : -1;
    }
    const int col = static_cast<int>(x - kPad) / cell_;
    const int row = static_cast<int>(y - kPad) / cell_;
    if (col < 0 || row < 0 || col >= columns_) {
        return -1;
    }
    const int index = row * columns_ + col;
    if (index < 0 || index >= kPaletteToolCount) {
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
    pressed_ = index;
    if (index != selected_) {
        selected_ = index;
        if (on_selected) {
            on_selected(selected_);
        }
    }
    return true;
}

void ToolPalette::clear_pressed()
{
    pressed_ = -1;
}

void ToolPalette::motion(double x, double y)
{
    const int next = index_at(x, y);
    if (next != hover_) {
        hover_ = next;
        hover_ms_ = 0;
        edge_ms_ = 0;
    }
}

void ToolPalette::leave()
{
    hover_ = -1;
    hover_ms_ = 0;
    edge_ms_ = 0;
}

void ToolPalette::advance(int elapsed_ms)
{
    if (elapsed_ms < 0) {
        elapsed_ms = 0;
    }
    if (hover_ < 0) {
        hover_ms_ = 0;
        edge_ms_ = 0;
        return;
    }
    hover_ms_ += elapsed_ms;
    edge_ms_ += elapsed_ms;
    if (hover_ms_ > 4000) {
        hover_ms_ = 4000;
    }
    if (edge_ms_ > 200) {
        edge_ms_ = 200;
    }
}

cairo_surface_t *tool_icon_named(const char *stem)
{
    struct Slot {
        std::string key;
        cairo_surface_t *image = nullptr;
        bool tried = false;
    };
    struct Cache {
        // One original icon set, shared by all tile looks.
        Slot slots[48];
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
    const std::string key = stem;
    Slot *free_slot = nullptr;
    for (Slot &slot : cache.slots) {
        if (slot.tried && slot.key == key) {
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
    free_slot->key = key;
    const std::string path = ui_data_file(std::string("tool-icons/") + stem + ".png");
    if (!path.empty()) {
        cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
        if (surface != nullptr && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
            free_slot->image = surface;
            return surface;
        }
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
    }
    return nullptr;
}

void ToolPalette::draw(cairo_t *cr) const
{
    if (slate_) {
        draw_slate(cr);
        return;
    }
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    box(cr, 0, 0, kPad * 2 + columns_ * cell_, height(), 0xED / 255.0, 0xEC / 255.0, 0xEB / 255.0);

    for (int i = 0; i < kPaletteToolCount; ++i) {
        double cx = 0;
        double cy = 0;
        cell_origin(i, cx, cy);
        const double x = cx + 1;
        const double y = cy + 1;
        const double bs = cell_ - 2;
        const bool pressed = i == selected_;
        const bool hover = i == hover_ && !pressed;
        const bool down = i == pressed_;
        const bool focused = i == focus_;

        // Engraved-stone buttons: one calm material in every tile look.
        // Dimensions are screen pixels, aligned to the toolbar's scale.
        const double unit = std::max(1.0, std::round(bs / 46.0));
        auto slab = [&](double dx, double dy, double w, double h, unsigned rgb) {
            box(cr, x + dx, y + dy, w, h, ((rgb >> 16) & 255) / 255.0,
                ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0);
        };
        const bool sunk = pressed || down;
        const bool hot = hover || focused;
        slab(0, 0, bs, bs, hot && !sunk ? 0xB88C2E : 0x5E5242);
        slab(unit, unit, bs - 2 * unit, bs - 2 * unit,
             sunk ? 0xC9B892 : (hot ? 0xEBE3D5 : 0xDED4C2));
        if (sunk) {
            slab(unit, unit, bs - 2 * unit, 2 * unit, 0x8C634A);
            slab(unit, unit, 2 * unit, bs - 2 * unit, 0x8C634A);
            slab(3 * unit, 3 * unit, bs - 6 * unit, unit, 0xB59473);
            slab(3 * unit, 3 * unit, unit, bs - 6 * unit, 0xB59473);
        } else {
            slab(unit, unit, bs - 2 * unit, 2 * unit, 0xF6F0E4);
            slab(unit, unit, 2 * unit, bs - 2 * unit, 0xF6F0E4);
            slab(unit, bs - 3 * unit, bs - 2 * unit, 2 * unit, 0x9E907A);
            slab(bs - 3 * unit, unit, 2 * unit, bs - 2 * unit, 0x9E907A);
        }

        const double shift = sunk ? unit : 0.0;
        cairo_surface_t *icon = (i >= 0 && i < kToolIconCount) ? tool_icons().image[i] : nullptr;
        // Authored at 20x20, pre-doubled to 40x40; integer scaling only.
        paint_icon(cr, icon, i, x + 3 * unit + shift, y + 2 * unit + shift, bs - 6 * unit);
        if (dimmed(i)) {
            // Locked (population or cap): the stone shows through the icon.
            cairo_set_source_rgba(cr, 0xDE / 255.0, 0xD4 / 255.0, 0xC2 / 255.0, 0.6);
            cairo_rectangle(cr, x + 3 * unit, y + 2 * unit, bs - 6 * unit, bs - 6 * unit);
            cairo_fill(cr);
        }
        if (sunk || hot) {
            slab(6 * unit, bs - (sunk ? 3 : 4) * unit, bs - 12 * unit, 2 * unit,
                 sunk ? 0xB88C2E : 0xB59473);
        }
        // Keyboard / controller focus: a dark-and-cream double keyline that
        // reads on the stone face and on the sunk tan face alike.
        if (focused) {
            cairo_set_line_width(cr, unit);
            cairo_set_source_rgb(cr, 0x2E / 255.0, 0x24 / 255.0, 0x1C / 255.0);
            cairo_rectangle(cr, x + 2.5 * unit, y + 2.5 * unit, bs - 5 * unit, bs - 5 * unit);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, 0xF6 / 255.0, 0xF0 / 255.0, 0xE4 / 255.0);
            cairo_rectangle(cr, x + 3.5 * unit, y + 3.5 * unit, bs - 7 * unit, bs - 7 * unit);
            cairo_stroke(cr);
        }
    }
}

void ToolPalette::draw_slate(cairo_t *cr) const
{
    const double w = kPad * 2 + columns_ * cell_;
    const double h = height();
    auto rgb = [&](unsigned c, double a = 1.0) {
        cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, a);
    };
    auto rounded = [&](double x, double y, double rw, double rh, double r) {
        r = std::min(r, std::min(rw, rh) / 2.0);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + rw - r, y + r, r, -kPiSlate / 2.0, 0);
        cairo_arc(cr, x + rw - r, y + rh - r, r, 0, kPiSlate / 2.0);
        cairo_arc(cr, x + r, y + rh - r, r, kPiSlate / 2.0, kPiSlate);
        cairo_arc(cr, x + r, y + r, r, kPiSlate, 1.5 * kPiSlate);
        cairo_close_path(cr);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    rgb(0x1B222C);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    if (header_ > 0) {
        const double font = std::max(8.0, std::min(header_ * 0.58, cell_ * 0.26));
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font);
        for (int g = 0; g < tool_groups::kGridGroupCount; ++g) {
            const double top = kPad + tool_groups::group_top(g, columns_, cell_, header_);
            const char *title = tool_groups::groups()[g].title;
            cairo_text_extents_t extents;
            cairo_text_extents(cr, title, &extents);
            const double room = w - kPad * 2 - 4;
            // A narrow toolbar shows the first word only ("SPECIAL").
            std::string shown = title;
            const std::size_t space = shown.find(' ');
            if (extents.x_advance > room && space != std::string::npos) {
                shown.resize(space);
                cairo_text_extents(cr, shown.c_str(), &extents);
            }
            double tx = kPad + 2;
            const double baseline = top + header_ * 0.5 + font * 0.36;
            cairo_save(cr);
            cairo_rectangle(cr, kPad, top, w - kPad * 2, header_);
            cairo_clip(cr);
            rgb(0xC9A646);
            cairo_move_to(cr, tx - extents.x_bearing, baseline);
            cairo_show_text(cr, shown.c_str());
            cairo_restore(cr);
            const double rule_x = tx + extents.x_advance + 5;
            if (rule_x < kPad + room) {
                rgb(0x3A4658);
                cairo_rectangle(cr, std::round(rule_x), std::round(baseline - font * 0.36), kPad + room - rule_x + 2, 1);
                cairo_fill(cr);
            }
        }
    }
    for (int i = 0; i < kPaletteToolCount; ++i) {
        double cx = 0;
        double cy = 0;
        if (!cell_origin(i, cx, cy)) {
            continue;
        }
        const double inset = std::max(2.0, std::round(cell_ / 22.0));
        const double x = cx + inset;
        const double y = cy + inset;
        const double bs = cell_ - inset * 2;
        const bool chosen = i == selected_;
        const bool hover = i == hover_ && !chosen;
        const bool down = i == pressed_;
        const bool focused = i == focus_;
        const double radius = std::max(3.0, bs * 0.14);
        if (chosen) {
            // Active-tool glow: soft gold rings outside the face.
            for (int ring = 3; ring >= 1; --ring) {
                rounded(x - ring * 1.5, y - ring * 1.5, bs + ring * 3.0, bs + ring * 3.0, radius + ring * 1.5);
                rgb(0xF2C14E, 0.10 + 0.08 * (3 - ring));
                cairo_fill(cr);
            }
        }
        rounded(x, y, bs, bs, radius);
        rgb(chosen ? 0x3E4A5E : (hover || focused ? 0x354154 : 0x29323F));
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, chosen ? 2.0 : 1.0);
        rgb(chosen ? 0xF2C14E : (hover || focused ? 0x8C98AA : 0x3C4859));
        cairo_stroke(cr);
        const double shift = down ? 1.0 : 0.0;
        cairo_surface_t *icon = (i >= 0 && i < kToolIconCount) ? tool_icons().image[i] : nullptr;
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        // The 40px icon art has its own transparent margin. Give it the whole
        // face when it fits 1:1, so it is not halved to 20px.
        // A face a little under 40px keeps a 1px pad and fits the icon to it
        // (icon_fit_scale) instead of halving it.
        const double pad = bs >= 42.0 ? 1.0 : (bs >= 30.0 ? 1.0 : std::max(2.0, std::round(bs * 0.1)));
        paint_icon(cr, icon, i, std::round(x + pad + shift), std::round(y + pad + shift), bs - pad * 2);
        cairo_restore(cr);
        if (dimmed(i)) {
            // Locked (population or cap): the face shows through the icon.
            rounded(x + 1, y + 1, bs - 2, bs - 2, std::max(2.0, radius - 1));
            rgb(chosen ? 0x3E4A5E : 0x29323F, 0.62);
            cairo_fill(cr);
        }
        if (focused) {
            cairo_set_line_width(cr, 2.0);
            rgb(0xF6F0E4);
            rounded(x + 2.5, y + 2.5, bs - 5, bs - 5, std::max(2.0, radius - 2));
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);
}

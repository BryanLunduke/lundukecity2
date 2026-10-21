// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

#include <algorithm>
#include <functional>

// Two-by-nine tool grid. Icons are original drawings, not imported artwork.

// Nearest-neighbor scale for a square sprite. A slot smaller than the sprite
// (UI scale below 1) shrinks to fit. A larger slot uses the biggest whole
// multiple that fits, so the pixels are not stretched by a fraction.
inline double icon_pixel_scale(int source, int slot)
{
    if (source < 1 || slot < 1) {
        return 1.0;
    }
    if (slot < source) {
        // Half the sprite, nearest-neighbour. A smaller cell may clip it.
        // A fractional fit (16/40) would blur the pixels.
        return (source % 2 == 0) ? 0.5 : 1.0;
    }
    return static_cast<double>(std::max(1, slot / source));
}

// Short name drawn when the sprite file is missing, so the button is not blank.
inline const char *tool_icon_fallback_label(int index)
{
    static const char *labels[17] = {
        "BD", "RD", "RL", "PW", "PK", "R", "C", "I", "PO", "FD", "ST", "SP", "CO", "NU", "AP", "*", "?",
    };
    if (index < 0 || index >= 17) {
        return "?";
    }
    return labels[index];
}

inline void paint_tool_fallback(cairo_t *cr, int index, double x, double y, double size)
{
    if (cr == nullptr || size < 4.0) {
        return;
    }
    static const double colors[17][3] = {
        {0.85, 0.62, 0.12}, {0.25, 0.25, 0.28}, {0.45, 0.28, 0.14}, {0.55, 0.55, 0.15},
        {0.18, 0.55, 0.22}, {0.20, 0.62, 0.28}, {0.16, 0.32, 0.72}, {0.72, 0.40, 0.12},
        {0.15, 0.28, 0.70}, {0.75, 0.16, 0.14}, {0.20, 0.55, 0.28}, {0.12, 0.40, 0.62},
        {0.22, 0.22, 0.24}, {0.15, 0.55, 0.58}, {0.35, 0.45, 0.62}, {0.85, 0.65, 0.12},
        {0.25, 0.25, 0.28},
    };
    const int clamped = std::max(0, std::min(index, 16));
    cairo_set_source_rgb(cr, colors[clamped][0], colors[clamped][1], colors[clamped][2]);
    cairo_rectangle(cr, x, y, size, size);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.08, 0.07, 0.06);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, x + 0.5, y + 0.5, size - 1.0, size - 1.0);
    cairo_stroke(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, std::max(8.0, size * 0.36));
    cairo_text_extents_t extents;
    const char *label = tool_icon_fallback_label(clamped);
    cairo_text_extents(cr, label, &extents);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, x + (size - extents.width) / 2.0 - extents.x_bearing,
                  y + (size - extents.height) / 2.0 - extents.y_bearing);
    cairo_show_text(cr, label);
}
class ToolPalette {
public:
    ToolPalette();

    int selected() const { return selected_; }
    void set_selected(int index);
    int hover() const { return hover_; }

    // Width and height of the grid for a given cell size.
    static int width_for(int cell);
    static int height_for(int cell);
    void set_cell(int cell) { cell_ = cell; }
    int cell() const { return cell_; }

    int index_at(double x, double y) const;
    const char *tooltip() const;

    bool press(double x, double y);
    void motion(double x, double y);
    void leave();
    void draw(cairo_t *cr) const;

    std::function<void(int)> on_selected;

private:
    static constexpr int kCols = 2;
    static constexpr int kPad = 4;

    int cell_ = 44;
    int selected_ = 0;
    int hover_ = -1;
};

// Toolbar and menu sprites, loaded by file stem under tool-icons/.
cairo_surface_t *tool_icon_named(const char *stem);

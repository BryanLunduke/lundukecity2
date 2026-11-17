// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

#include <algorithm>
#include <functional>

// Toolbar grid of the build tools (Query has no button), in the
// tool_groups.hpp sections. Icons are original drawings, not imported
// artwork.

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

// Toolbar buttons with a sprite: every palette entry but Query.
inline constexpr int kToolIconCount = 26;

// Short name drawn when the sprite file is missing, so the button is not blank.
inline const char *tool_icon_fallback_label(int index)
{
    static const char *labels[kToolIconCount + 1] = {
        "BD", "RD", "RL", "PW", "R",  "C",  "I",  "DC", "PK", "BP", "FN", "FD", "PO", "PR",
        "CO", "NU", "MN", "ZO", "AM", "CA", "ST", "MA", "LB", "MH", "AP", "SP", "?",
    };
    if (index < 0 || index > kToolIconCount) {
        return "?";
    }
    return labels[index];
}

inline void paint_tool_fallback(cairo_t *cr, int index, double x, double y, double size)
{
    if (cr == nullptr || size < 4.0) {
        return;
    }
    static const double colors[kToolIconCount + 1][3] = {
        {0.85, 0.62, 0.12}, {0.25, 0.25, 0.28}, {0.45, 0.28, 0.14}, {0.55, 0.55, 0.15}, // build
        {0.20, 0.62, 0.28}, {0.16, 0.32, 0.72}, {0.72, 0.40, 0.12}, {0.78, 0.66, 0.16}, // zones
        {0.18, 0.55, 0.22}, {0.16, 0.48, 0.20}, {0.30, 0.55, 0.70},                     // parks
        {0.75, 0.16, 0.14}, {0.15, 0.28, 0.70}, {0.40, 0.40, 0.42},                     // services
        {0.22, 0.22, 0.24}, {0.15, 0.55, 0.58}, {0.20, 0.48, 0.52},                     // utilities
        {0.55, 0.45, 0.20}, {0.70, 0.30, 0.45}, {0.60, 0.15, 0.20}, {0.20, 0.55, 0.28},
        {0.25, 0.45, 0.70}, {0.50, 0.32, 0.20}, {0.62, 0.55, 0.40},                     // special
        {0.35, 0.45, 0.62}, {0.12, 0.40, 0.62},                                         // ports
        {0.25, 0.25, 0.28},
    };
    const int clamped = std::max(0, std::min(index, kToolIconCount));
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

    // Width and height of the grid, 2 to 4 columns. The desktop layout
    // picks the fewest columns (from 3) that fit the window height.
    static int width_for(int cell, int columns = 2);
    static int height_for(int cell, int columns = 2);
    // Slate toolbar: section headers of `header` pixels above each group.
    static int grouped_height_for(int cell, int columns, int header);
    // slate draws the dark skin. header > 0 adds the section headers.
    void set_style(bool slate, int header);
    bool slate() const { return slate_; }
    int header() const { return header_; }
    // Height of the grid as it is drawn now.
    int height() const;
    // Top-left of a tool's cell in palette coordinates. False without a button.
    bool cell_origin(int index, double &x, double &y) const;
    void set_cell(int cell) { cell_ = cell; }
    int cell() const { return cell_; }
    void set_columns(int columns);
    int columns() const { return columns_; }

    int index_at(double x, double y) const;
    const char *tooltip() const;

    bool press(double x, double y);
    void clear_pressed();
    void motion(double x, double y);
    void leave();
    void set_focus(int index) { focus_ = index; }
    // Bit i greys out button i: a landmark the city cannot buy yet (the
    // population gate or the cap). It stays clickable; placing it shows why.
    void set_dimmed(unsigned mask) { dimmed_ = mask; }
    bool dimmed(int index) const { return index >= 0 && index < 32 && (dimmed_ >> index & 1u) != 0; }
    int focus() const { return focus_; }
    void advance(int elapsed_ms);
    int hover_ms() const { return hover_ms_; }
    void draw(cairo_t *cr) const;

    std::function<void(int)> on_selected;

private:
    static constexpr int kPad = 4;
    void draw_slate(cairo_t *cr) const;

    int columns_ = 2;
    int cell_ = 44;
    int header_ = 0;
    bool slate_ = false;
    int selected_ = 0;
    int hover_ = -1;
    int pressed_ = -1;
    int focus_ = -1;
    unsigned dimmed_ = 0;
    int edge_ms_ = 0;
    int hover_ms_ = 0;
};

// File stem under tool-icons/ for a palette index (Query has none).
const char *tool_icon_stem(int index);
// Toolbar and menu sprites, loaded by file stem under tool-icons/.
cairo_surface_t *tool_icon_named(const char *stem);

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairomm/context.h>

#include <functional>

// Two-by-eight tool grid. Icons are original drawings, not imported artwork.
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
    void draw(const Cairo::RefPtr<Cairo::Context> &cr) const;

    std::function<void(int)> on_selected;

private:
    static constexpr int kCols = 2;
    static constexpr int kPad = 4;

    int cell_ = 44;
    int selected_ = 0;
    int hover_ = -1;
};

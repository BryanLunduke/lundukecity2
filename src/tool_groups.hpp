// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Section headers for the Slate toolbar. kTools is in toolbar order, so
// each group is a run of palette indices and a tool keeps one index for
// its button, its quick-bar slot and its pad position.
// Pure layout arithmetic: no cairo, no SDL, so a test can check it.

#include <algorithm>

namespace tool_groups {

struct Group {
    const char *title;
    int first;
    int count;
};

// Build, Zones (R, C, I and the Data Center), Parks, Services, Utilities,
// Special buildings (the landmarks and the stadium) and Ports.
inline const Group *groups()
{
    static const Group table[] = {
        {"BUILD", 0, 4},     {"ZONES", 4, 4},      {"PARKS", 8, 3},  {"SERVICES", 11, 3},
        {"UTILITIES", 14, 3}, {"SPECIAL BUILDINGS", 17, 7}, {"PORTS", 24, 2},
        // Desktop toolbar only (release nov24 #3): Inspect's own button. The
        // handheld quick bar and the L1/R1 section jumps keep Inspect as the
        // separate last stop, so only the grid layout below counts this one.
        {"INSPECT", 26, 1},
    };
    return table;
}

// Build sections (the handheld quick bar, section jumps, group_of).
constexpr int kGroupCount = 7;
// Sections drawn in the desktop toolbar grid: the build sections + INSPECT.
constexpr int kGridGroupCount = 8;

// Grid section of a toolbar button, Inspect included; -1 for none.
inline int grid_group_of(int index)
{
    const Group *table = groups();
    for (int g = 0; g < kGridGroupCount; ++g) {
        if (index >= table[g].first && index < table[g].first + table[g].count) {
            return g;
        }
    }
    return -1;
}

inline int group_of(int index)
{
    const Group *table = groups();
    for (int g = 0; g < kGroupCount; ++g) {
        if (index >= table[g].first && index < table[g].first + table[g].count) {
            return g;
        }
    }
    return -1;
}

inline int rows_in(int group, int columns)
{
    if (columns < 1) {
        columns = 1;
    }
    const Group &entry = groups()[group];
    return (entry.count + columns - 1) / columns;
}

// Top of a group's header strip, measured from the inner padding.
inline int group_top(int group, int columns, int cell, int header)
{
    int y = 0;
    for (int g = 0; g < group && g < kGridGroupCount; ++g) {
        y += header + rows_in(g, columns) * cell;
    }
    return y;
}

inline int grid_height(int columns, int cell, int header)
{
    return group_top(kGridGroupCount, columns, cell, header);
}

// Buttons in one row of a group. The rows are balanced so no row is a
// single orphan: 4 tools in 3 columns are 2+2, 7 are 3+2+2. The longer
// rows come first. The row count is the same as a plain wrap.
inline int row_length(int group, int columns, int row)
{
    const int rows = rows_in(group, columns);
    if (row < 0 || row >= rows) {
        return 0;
    }
    const int count = groups()[group].count;
    const int base = count / rows;
    const int extra = count % rows;
    return base + (row < extra ? 1 : 0);
}

// Row and column of a group's local button under the balanced rows.
inline void row_col(int group, int columns, int local, int &row, int &col)
{
    const int rows = rows_in(group, columns);
    row = 0;
    col = local;
    for (int r = 0; r < rows; ++r) {
        const int len = row_length(group, columns, r);
        if (col < len) {
            row = r;
            return;
        }
        col -= len;
    }
    row = rows - 1;
}

// Left offset of a balanced row: a short row is centred in the grid, in
// half-cell steps, so a 2+2 group sits in the middle of three columns.
inline int row_offset(int group, int columns, int row, int cell)
{
    const int len = row_length(group, columns, row);
    return (columns - len) * cell / 2;
}

// Cell origin (inside the padding) for a tool. False for an index with no button.
inline bool slot(int index, int columns, int cell, int header, int &x, int &y)
{
    const int g = grid_group_of(index);
    if (g < 0 || columns < 1) {
        return false;
    }
    const int local = index - groups()[g].first;
    int row = 0;
    int col = 0;
    row_col(g, columns, local, row, col);
    x = row_offset(g, columns, row, cell) + col * cell;
    y = group_top(g, columns, cell, header) + header + row * cell;
    return true;
}

// Tool under a point (inside the padding), or -1 for a header, a gap, or
// the empty end of a short row.
inline int index_at(int x, int y, int columns, int cell, int header)
{
    if (x < 0 || y < 0 || cell < 1 || columns < 1) {
        return -1;
    }
    if (x >= columns * cell) {
        return -1;
    }
    for (int g = 0; g < kGridGroupCount; ++g) {
        const int top = group_top(g, columns, cell, header) + header;
        const int rows = rows_in(g, columns);
        if (y >= top && y < top + rows * cell) {
            const int row = (y - top) / cell;
            const int dx = x - row_offset(g, columns, row, cell);
            if (dx < 0) {
                return -1;
            }
            const int col = dx / cell;
            if (col >= row_length(g, columns, row)) {
                return -1;
            }
            int local = col;
            for (int r = 0; r < row; ++r) {
                local += row_length(g, columns, r);
            }
            return groups()[g].first + local;
        }
    }
    return -1;
}

// Handheld quick bar widths. Every slot of the section holding `shown`
// (Inspect, the last slot, is a section of its own) gets `big` pixels when
// the rest can keep at least 36px (or their even share if that is less);
// the other slots split what is left. -1 shows every slot the same.
struct QuickBarLayout {
    double width[32] = {};
    double big = 0.0;
    double small = 0.0;
    double total = 0.0;
};

inline QuickBarLayout quick_bar_layout(int count, int shown, double big, double room)
{
    QuickBarLayout out;
    if (count < 1 || count > 32 || room <= 0.0) {
        return out;
    }
    const double even = std::min(big, room / count);
    int first = -1;
    int span = 0;
    const int g = shown >= 0 ? group_of(shown) : -1;
    if (g >= 0) {
        first = groups()[g].first;
        span = groups()[g].count;
    } else if (shown == count - 1) {
        first = shown;
        span = 1;
    }
    double wide = even;
    double rest = even;
    if (span > 0 && span < count && big > even) {
        const double floor_small = std::min(even, 36.0);
        wide = std::min(big, (room - (count - span) * floor_small) / span);
        wide = std::max(wide, even);
        rest = std::min(wide, (room - span * wide) / (count - span));
    }
    out.big = wide;
    out.small = rest;
    for (int i = 0; i < count; ++i) {
        out.width[i] = (span > 0 && i >= first && i < first + span) ? wide : rest;
        out.total += out.width[i];
    }
    return out;
}

// The first tool of the section after (dir > 0) or before (dir < 0) the
// one holding `index`, wrapping round. Inspect (count - 1, no section) is
// its own stop after Ports. Holding L1/R1 on a pad steps by these.
inline int section_jump(int index, int dir, int count)
{
    const int g = group_of(index);
    // Stops: each section's first tool, then Inspect.
    const int stops = kGroupCount + 1;
    int at = g >= 0 ? g : kGroupCount;
    at = ((at + (dir >= 0 ? 1 : -1)) % stops + stops) % stops;
    return at == kGroupCount ? count - 1 : groups()[at].first;
}

} // namespace tool_groups

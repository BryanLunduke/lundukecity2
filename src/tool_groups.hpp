// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Section headers for the Slate toolbar. kTools is in toolbar order, so
// each group is a run of palette indices and a tool keeps one index for
// its button, its quick-bar slot and its pad position.
// Pure layout arithmetic: no cairo, no SDL, so a test can check it.

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
    };
    return table;
}

constexpr int kGroupCount = 7;

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
    for (int g = 0; g < group && g < kGroupCount; ++g) {
        y += header + rows_in(g, columns) * cell;
    }
    return y;
}

inline int grid_height(int columns, int cell, int header)
{
    return group_top(kGroupCount, columns, cell, header);
}

// Cell origin (inside the padding) for a tool. False for an index with no button.
inline bool slot(int index, int columns, int cell, int header, int &x, int &y)
{
    const int g = group_of(index);
    if (g < 0 || columns < 1) {
        return false;
    }
    const int local = index - groups()[g].first;
    x = (local % columns) * cell;
    y = group_top(g, columns, cell, header) + header + (local / columns) * cell;
    return true;
}

// Tool under a point (inside the padding), or -1 for a header, a gap, or
// the empty end of a short row.
inline int index_at(int x, int y, int columns, int cell, int header)
{
    if (x < 0 || y < 0 || cell < 1 || columns < 1) {
        return -1;
    }
    const int col = x / cell;
    if (col >= columns) {
        return -1;
    }
    for (int g = 0; g < kGroupCount; ++g) {
        const int top = group_top(g, columns, cell, header) + header;
        const int rows = rows_in(g, columns);
        if (y >= top && y < top + rows * cell) {
            const int local = ((y - top) / cell) * columns + col;
            if (local < groups()[g].count) {
                return groups()[g].first + local;
            }
            return -1;
        }
    }
    return -1;
}

} // namespace tool_groups

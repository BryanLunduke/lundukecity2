// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Deck nov24: Steam Deck review fixes that are pure functions.

#include "micropolis.h"
#include "map_fit.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool has(const std::string &text, const char *part)
{
    return text.find(part) != std::string::npos;
}

// #1: the handheld quick bar reserves the bottom of the view the same way
// the minimap reserves the right, so the last rows scroll up above it.
void bottom_reserve()
{
    const int view = 800;
    const int map = 100 * 16;
    const int reserve = 96;
    const int box = map_letterbox_clear(view, map, reserve);
    check(box == 0, "a map taller than the view has no letterbox");
    check(map_scroll_limit(view, map, box, reserve) == map + reserve - view, "bottom scroll reaches past the bar");
    check(map_scroll_limit(view, map, box, 0) == map - view, "no reserve keeps the old limit");
}

} // namespace

int main()
{
    bottom_reserve();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("deck_nov24 ok");
    return 0;
}

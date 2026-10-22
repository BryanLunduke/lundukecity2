// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A quick look at a classic .cty without loading it into the simulator.
// Date and funds come from the misc block (funds from the wide trailer when
// the file has one). Population is counted from the zone tiles, so a stale
// census in the file is not what the list shows. The swatch is one colour
// per tile, classified the same way the minimap falls back when the tile
// atlas is not used.

struct CityGlance {
    bool ok = false;
    std::int64_t population = 0;
    std::int64_t funds = 0;
    int year = 1900;
    int month = 0;
    std::string summary;
    // 120 by 100 packed RGB, row-major. Empty when the map could not be read.
    std::vector<std::uint32_t> swatch;
};

CityGlance city_glance(const std::string &path);

inline const char *city_glance_month(int month)
{
    static const char *names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 0 || month > 11) {
        return names[0];
    }
    return names[month];
}

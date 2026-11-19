// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The New City land preview must be the map Generate builds for the same
// seed and knobs, tile for tile, trees included.

#include "city_session.hpp"

#include <cstdio>
#include <vector>

namespace {

struct Case {
    int seed;
    int island;
    int rivers;
    int lakes;
    int trees;
};

int compare(const Case &c, int code)
{
    CitySession preview_city;
    std::vector<unsigned short> tiles;
    if (!preview_city.preview_terrain(c.seed, c.island, c.rivers, c.lakes, c.trees, tiles) ||
        tiles.size() != static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH)) {
        std::fprintf(stderr, "seed %d: no preview\n", c.seed);
        return code;
    }
    CitySession city;
    // Build another city first so leftover engine state cannot hide a
    // difference between the two paths.
    city.new_city("Before", 99);
    CitySession::NewCitySpec spec;
    spec.name = "Preview";
    spec.seed = c.seed;
    spec.seed_was_set = true;
    spec.island = c.island;
    spec.rivers = c.rivers;
    spec.lakes = c.lakes;
    spec.trees = c.trees;
    city.new_city(spec);
    int differ = 0;
    int first_x = -1;
    int first_y = -1;
    for (int y = 0; y < CitySession::kWorldH; ++y) {
        for (int x = 0; x < CitySession::kWorldW; ++x) {
            const int want = city.map_value(x, y) & 1023;
            const int got = tiles[static_cast<std::size_t>(y * CitySession::kWorldW + x)] & 1023;
            if (want != got) {
                if (differ == 0) {
                    first_x = x;
                    first_y = y;
                }
                ++differ;
            }
        }
    }
    if (differ != 0) {
        std::fprintf(stderr, "seed %d knobs %d/%d/%d/%d: %d tiles differ, first at %d,%d\n", c.seed, c.island,
                     c.rivers, c.lakes, c.trees, differ, first_x, first_y);
        return code;
    }
    return 0;
}

} // namespace

int main()
{
    const Case cases[] = {
        {123456, -1, -1, -1, -1},
        {1000, -1, -1, -1, -1},
        {777777, 0, 1, 1, 50},
        {4242, 1, -1, 0, 90},
        {0, -1, -1, -1, 0},
    };
    int code = 10;
    for (const Case &c : cases) {
        if (const int result = compare(c, code)) {
            return result;
        }
        ++code;
    }
    std::printf("PASS: the New City preview matches the generated map\n");
    return 0;
}

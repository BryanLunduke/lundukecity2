// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// The Inspect outline covers the whole building under the clicked tile:
// a 3x3 zone, a 4x4 plant, a 6x6 airport and a landmark, from any tile of
// the footprint, and a single tile for a road.

#include "city_session.hpp"
#include "special_buildings.hpp"

#include "micropolis.h"

#include <cstdio>
#include <string>

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

// A spot whose box (with a one-tile margin) is bare dirt.
bool clear_box(const CitySession &city, int left, int top, int width, int height)
{
    for (int y = top - 1; y <= top + height; ++y) {
        for (int x = left - 1; x <= left + width; ++x) {
            if (x < 0 || y < 0 || x >= CitySession::kWorldW || y >= CitySession::kWorldH) {
                return false;
            }
            if ((city.map_value(x, y) & LOMASK) != DIRT) {
                return false;
            }
        }
    }
    return true;
}

bool find_clear(const CitySession &city, int size, int &left, int &top)
{
    for (int y = 2; y + size + 2 < CitySession::kWorldH; ++y) {
        for (int x = 2; x + size + 2 < CitySession::kWorldW; ++x) {
            if (clear_box(city, x, y, size, size)) {
                left = x;
                top = y;
                return true;
            }
        }
    }
    return false;
}

// Every tile of the footprint reports the same, whole footprint.
bool covers(const CitySession &city, int left, int top, int size)
{
    for (int y = top; y < top + size; ++y) {
        for (int x = left; x < left + size; ++x) {
            const CitySession::TileInspect card = city.inspect_tile(x, y);
            if (!card.zone || card.left != left || card.top != top || card.width != size || card.height != size) {
                std::fprintf(stderr, "tile %d,%d -> zone=%d %d,%d %dx%d (want %d,%d %dx%d)\n", x, y, card.zone ? 1 : 0,
                             card.left, card.top, card.width, card.height, left, top, size, size);
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    CitySession city;
    city.new_city("Footprint", 7);
    city.set_disasters(false);

    struct Case {
        int tool;
        int size;
    };
    const Case cases[] = {{TOOL_RESIDENTIAL, 3}, {TOOL_COALPOWER, 4}, {TOOL_AIRPORT, 6}};
    int code = 10;
    for (const Case &c : cases) {
        int left = 0;
        int top = 0;
        if (!find_clear(city, c.size, left, top)) {
            return fail(code, "no clear land for a building");
        }
        // The engine's click is the zone centre, one tile in from the top-left.
        city.use_tool(c.tool, left + 1, top + 1);
        if ((city.map_value(left + 1, top + 1) & ZONEBIT) == 0) {
            return fail(code + 1, "the building was not placed");
        }
        if (!covers(city, left, top, c.size)) {
            return fail(code + 2, "the Inspect footprint does not cover the whole building");
        }
        code += 10;
    }

    // Landmarks of every size the table has room for here (some need a
    // road, residents or a shore first); each covers its own footprint.
    int landmarks = 0;
    for (int i = 0; i < special_building_count(); ++i) {
        const SpecialBuilding &building = special_building(i);
        const int size = building.width > building.height ? building.width : building.height;
        int left = 0;
        int top = 0;
        if (!find_clear(city, size, left, top)) {
            continue;
        }
        if (!city.place_special(i, left + building.anchor_x, top + building.anchor_y)) {
            continue;
        }
        if (building.width != building.height || !covers(city, left, top, building.width)) {
            std::fprintf(stderr, "landmark %s\n", building.name);
            return fail(41, "the Inspect footprint does not cover the whole landmark");
        }
        ++landmarks;
    }
    if (landmarks < 2) {
        return fail(42, "fewer than two landmarks could be placed");
    }

    // A single road tile is its own 1x1 outline.
    {
        int left = 0;
        int top = 0;
        if (!find_clear(city, 1, left, top)) {
            return fail(50, "no clear land for a road");
        }
        city.use_tool(TOOL_ROAD, left, top);
        const CitySession::TileInspect card = city.inspect_tile(left, top);
        if (card.zone || card.name != "Road") {
            return fail(51, "a lone road reports a building footprint");
        }
        int center_left = 0;
        int center_top = 0;
        int width = 0;
        int height = 0;
        if (city.building_footprint(left, top, center_left, center_top, width, height)) {
            return fail(52, "a road reports as a zone centre");
        }
    }
    std::printf("footprint ok\n");
    return 0;
}

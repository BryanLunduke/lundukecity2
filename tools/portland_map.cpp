// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/portland.cty, the map of the Portland (2020) Riot
// scenario.
//
// The map is drawn here from a hand-made outline of Portland's real
// geography (no other game's map is used): the Columbia River along the
// top with Hayden and Government Islands and the Washington shore beyond
// it, the Willamette coming up from Sellwood past Ross Island and
// downtown and bending north-west to meet the Columbia, Sauvie Island and
// the Multnomah Channel in the north-west corner, Forest Park on the West
// Hills above Linnton and the Northwest industrial strip, Washington Park,
// Mount Tabor, Rocky Butte and Powell Butte. Map north is real north; one
// tile is about 150 m east-west and 170 m north-south (the map spans the
// West Hills to I-205 and Vancouver's shore to Sellwood).
//
// The city is laid out as it stood in 2020: downtown and the Pearl on the
// west bank inside the I-405 loop, the Willamette bridges (St. Johns,
// Fremont, Broadway, Steel, Burnside, Morrison, Hawthorne, Marquam,
// Tilikum, Ross Island and Sellwood), I-5 down the east bank over the
// Interstate Bridge, I-84 east from the Rose Quarter, I-405, I-205 over
// the Glenn Jackson Bridge, US 26 and US 30, the east side street grid,
// MAX light rail, the Union Pacific line, Rivergate and Swan Island, PDX,
// Providence Park and the Moda Center. Portland's power comes from dams
// and plants outside the map; here it is a few plants (coal-plant tiles)
// in the industrial districts. Washington state across the Columbia and
// Sauvie Island are left as open country.
//
//   portland_map <output.cty> [preview.ppm]
//
// The run is deterministic (fixed seed).

#include "map_builder.hpp"

using namespace mapgen;

namespace {

// ---- Geography outline (tile coordinates, x east, y south) --------------

// The Columbia, from the north-west corner clockwise.
const std::vector<Point> kColumbia = {
    {0, 0},    {35, 0},   {45, 1},   {55, 3},   {70, 4},   {90, 5},  {119, 6},
    {119, 15}, {100, 14}, {90, 13},  {70, 12},  {58, 11},  {45, 11}, {30, 10}, {17, 7.5}, {0, 8},
};
// The Willamette from its mouth up to Sellwood, and the Multnomah Channel.
const std::vector<LinePoint> kWillamette = {
    {17, 7, 5},     {20, 13, 5},    {25, 19, 4.5},  {30, 25, 4.5},  {35, 31, 4.5}, {40, 36, 4.5},
    {45, 40, 4},    {50, 44, 4},    {54, 48, 3.5},  {56, 52, 3.5},  {56.5, 58, 3.5}, {56.5, 64, 3.5},
    {57, 70, 3.5},  {58, 76, 3.5},  {59, 82, 3.5},  {59.5, 88, 3.5}, {59, 94, 3.5}, {58, 100, 3.5},
};
const std::vector<LinePoint> kMultnomahChannel = {{16, 8, 2}, {11, 14, 2}, {6, 21, 2}, {0, 29, 2}};
// Forest Park on the West Hills.
const std::vector<Point> kForestPark = {
    {0, 32},  {6, 22},  {12, 17}, {15, 17}, {19, 21}, {23, 26}, {27, 31}, {30, 36},
    {32, 41}, {32, 46}, {29, 50}, {22, 53}, {12, 54}, {0, 54},
};

double river_x(double y)
{
    // The Willamette's centre line at row y (for the stretch south of 44).
    for (std::size_t i = 0; i + 1 < kWillamette.size(); ++i) {
        const LinePoint &a = kWillamette[i];
        const LinePoint &b = kWillamette[i + 1];
        if (y >= a.y && y <= b.y) {
            return a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
        }
    }
    return kWillamette.back().x;
}

// 'W' water, 'T' woods, 'L' land.
char terrain_at(int tx, int ty)
{
    const double x = tx + 0.5, y = ty + 0.5;
    const bool hayden = in_ellipse(x, y, 54, 7.5, 7, 1.8);
    const bool government = in_ellipse(x, y, 108, 10, 7, 1.8);
    const bool ross = in_ellipse(x, y, 61.5, 85, 1.4, 2.6);
    if ((inside(kColumbia, x, y) && !hayden && !government) || in_line(kWillamette, x, y) ||
        in_line(kMultnomahChannel, x, y) || (in_ellipse(x, y, 60.5, 85, 3, 4.2) && !ross)) {
        return 'W';
    }
    // Woods: Washington state north of the Columbia, Sauvie Island west of
    // the channel, Government Island, Forest Park (not where US 30 and
    // US 26 run), Washington Park and the Hoyt Arboretum, Marquam Nature
    // Park, Tryon Creek, Oaks Bottom and Ross Island, Mount Tabor, Rocky
    // Butte, Powell Butte, Kelley Point and the Smith and Bybee lakes.
    const bool washington = y < 8 && !inside(kColumbia, x, y) && !hayden;
    const bool sauvie = x + 0.75 * y < 21 && !in_line(kMultnomahChannel, x, y);
    if (washington || sauvie || government || ross ||
        (inside(kForestPark, x, y) && !(ty >= 61 && ty <= 62)) || in_ellipse(x, y, 34, 66, 5.5, 3.5) ||
        in_ellipse(x, y, 47, 82, 3, 3) || in_ellipse(x, y, 42, 95, 4, 3) || in_ellipse(x, y, 64, 89, 2, 2.5) ||
        in_ellipse(x, y, 97, 68, 3, 2.5) || in_ellipse(x, y, 106, 40, 2.5, 2.5) ||
        in_ellipse(x, y, 113, 92, 5, 3.5) || in_ellipse(x, y, 22, 11, 3, 2) || in_ellipse(x, y, 31, 15, 3, 2)) {
        return 'T';
    }
    return 'L';
}

// Zone kind for a 3x3 lot centred on (x, y).
EditingTool zone_kind(int x, int y)
{
    // Industry: Rivergate and the marine terminals, the Northwest
    // industrial strip along US 30, Swan Island, the Central Eastside, the
    // Brooklyn yard, and the warehouses by PDX.
    if (in_rect(x, y, 17, 10, 40, 22) || (in_rect(x, y, 18, 18, 46, 49) && x > river_x(y) - 12 && x < river_x(y)) ||
        in_rect(x, y, 44, 34, 52, 44) || in_rect(x, y, 61, 57, 67, 73) || in_rect(x, y, 61, 76, 67, 84) ||
        in_rect(x, y, 76, 13, 118, 22)) {
        return TOOL_INDUSTRIAL;
    }
    // Downtown and the Pearl, with apartments mixed in; the Lloyd District.
    if (in_rect(x, y, 43, 47, 55, 73)) {
        return ((x / 3) + (y / 4)) % 5 >= 3 ? TOOL_RESIDENTIAL : TOOL_COMMERCIAL;
    }
    if (in_rect(x, y, 61, 47, 74, 56)) {
        return TOOL_COMMERCIAL;
    }
    // Neighbourhood main streets: Hawthorne, Division, Alberta, Lombard,
    // Mississippi, Sandy, 82nd Avenue, Sellwood, Northwest 23rd, Hillsdale.
    if (in_rect(x, y, 64, 65, 110, 67) || in_rect(x, y, 64, 69, 110, 71) || in_rect(x, y, 62, 37, 100, 39) ||
        in_rect(x, y, 30, 25, 80, 27) || in_rect(x, y, 66, 30, 68, 46) || in_rect(x, y, 80, 22, 84, 99) ||
        in_rect(x, y, 62, 92, 74, 94) || in_rect(x, y, 36, 52, 40, 60) || in_rect(x, y, 38, 76, 44, 79)) {
        return TOOL_COMMERCIAL;
    }
    const int d = std::abs(x - 50) + std::abs(y - 60);
    const int every = d < 36 ? 4 : 6;
    if (((x / 3) + 2 * (y / 4)) % every == 0) {
        return TOOL_COMMERCIAL;
    }
    return TOOL_RESIDENTIAL;
}

void layout(Builder &b)
{
    // Big buildings before the streets: PDX on the Columbia, the marine
    // terminals at Rivergate and Swan Island, Providence Park and the Moda
    // Center, and the plants.
    if (!b.clear_land(90, 15, 6, 6) || !b.tool(TOOL_AIRPORT, 91, 16)) {
        std::fprintf(stderr, "portland_map: could not place the airport\n");
    }
    const struct {
        EditingTool tool;
        int x, y;
    } big[] = {
        {TOOL_SEAPORT, 36, 11},   {TOOL_SEAPORT, 48, 32},   {TOOL_STADIUM, 38, 59},   {TOOL_STADIUM, 61, 51},
        {TOOL_COALPOWER, 40, 11}, {TOOL_COALPOWER, 36, 41}, {TOOL_COALPOWER, 33, 37},  {TOOL_COALPOWER, 100, 17},
        {TOOL_COALPOWER, 64, 78}, {TOOL_COALPOWER, 110, 17}, {TOOL_COALPOWER, 80, 15}, {TOOL_COALPOWER, 51, 88},
        {TOOL_COALPOWER, 39, 45}, {TOOL_COALPOWER, 70, 15},
    };
    for (const auto &item : big) {
        if (!b.building(item.tool, 4, item.x, item.y)) {
            std::fprintf(stderr, "portland_map: could not place tool %d at %d,%d\n", static_cast<int>(item.tool),
                         item.x, item.y);
        }
    }

    // I-5 over the Interstate Bridge and Hayden Island, down the east bank
    // to the Marquam Bridge, and on south-west. I-405 round the west side
    // of downtown to the Fremont Bridge. I-84 east from the Rose Quarter,
    // I-205 over the Glenn Jackson Bridge, US 26 west through the hills,
    // US 30 along the Northwest industrial strip.
    b.road_line(60, 0, 60, 74, true);
    b.road_line(60, 74, 43, 74, true);
    b.road_line(49, 74, 49, 99, false);
    b.road_line(43, 46, 43, 74, false);
    b.road_line(43, 46, 60, 46, true);
    b.road_line(60, 54, 119, 54, false);
    b.road_line(112, 0, 112, 99, true);
    b.road_line(0, 62, 43, 62, false);
    b.road_stairs(42, 47, 19, 19);
    // The Willamette bridges downtown: Broadway, Steel, Burnside (across
    // the whole city), Morrison, Hawthorne; Ross Island, Sellwood and St.
    // Johns.
    b.road_line(43, 50, 60, 50, true);
    b.road_line(43, 54, 60, 54, true);
    b.road_line(30, 58, 119, 58, true);
    b.road_line(43, 62, 60, 62, true);
    b.road_line(43, 66, 60, 66, true);
    b.road_line(49, 82, 66, 82, true);
    b.road_line(49, 94, 66, 94, true);
    b.road_line(24, 26, 40, 26, true);
    // MAX: across the Tilikum Crossing and out along I-84; the Union
    // Pacific down the east bank.
    b.rail_line(49, 78, 64, 78);
    b.rail_line(60, 55, 119, 55);
    b.rail_line(64, 56, 64, 99);

    // Streets: the close grid everywhere (a street every fourth row), an
    // avenue every fourth column downtown and every seventh elsewhere,
    // kept clear of the freeways and the rail line.
    auto central = [](int x, int y) { return x >= 43 && x <= 54 && y >= 46 && y <= 74; };
    auto clear_of = [](int x, int y) {
        const int spines[][3] = {{60, 0, 74}, {43, 46, 74}, {49, 74, 99}, {112, 0, 99}, {64, 56, 99}};
        for (const auto &sp : spines) {
            if (y >= sp[1] && y <= sp[2] && std::abs(sp[0] - x) <= 2 && sp[0] != x) {
                return false;
            }
        }
        return true;
    };
    b.grid([&](int x, int y) {
        if (y == 55 || (y >= 53 && y <= 56 && x > 60)) {
            return false; // I-84 and MAX
        }
        return y % 4 == 2 && x > 0;
    }, [&](int x, int y) {
        if (x < 1 || x > W - 2 || y < 9) {
            return false;
        }
        if (central(x, y)) {
            return x % 4 == 3 && clear_of(x, y);
        }
        return x % 7 == 3 && clear_of(x, y);
    });

    // Power under the rivers: Hayden Island, and across the Willamette at
    // Swan Island, downtown and Sellwood.
    b.wire_across(57, 13, 57, 6);
    b.wire_across(40, 30, 50, 30);
    b.wire_across(52, 60, 60, 60);
    b.wire_across(52, 90, 66, 90);

    // Parks: Tom McCall Waterfront Park, Pioneer Courthouse Square, Jamison Square, Laurelhurst, Peninsula, Irving, Kenton,
    // Overlook, Westmoreland, Laurelhurst, Woodstock, Cathedral Park.
    b.park_rect(55, 56, 55, 72);
    b.park_rect(50, 63, 51, 64);
    b.park_rect(48, 51, 49, 52);
    b.park_rect(78, 59, 81, 61);
    b.park_rect(66, 33, 69, 34);
    b.park_rect(72, 43, 74, 44);
    b.park_rect(56, 27, 58, 28);
    b.park_rect(50, 31, 52, 32);
    b.park_rect(66, 87, 69, 88);
    b.park_rect(88, 83, 90, 84);
    b.park_rect(32, 27, 33, 30);

    // Police: the Portland Police Bureau's Central, North and East
    // precincts, and the neighbourhood offices.
    b.building_around(TOOL_POLICESTATION, 3, 52, 64);
    b.building_around(TOOL_POLICESTATION, 3, 66, 26);
    b.building_around(TOOL_POLICESTATION, 3, 104, 62);
    b.building_around(TOOL_POLICESTATION, 3, 36, 30);
    b.building_around(TOOL_POLICESTATION, 3, 80, 42);
    b.building_around(TOOL_POLICESTATION, 3, 72, 84);
    b.building_around(TOOL_POLICESTATION, 3, 36, 84);
    // Fire stations.
    b.building_around(TOOL_FIRESTATION, 3, 40, 56);
    b.building_around(TOOL_FIRESTATION, 3, 40, 68);
    b.building_around(TOOL_FIRESTATION, 3, 42, 88);
    b.building_around(TOOL_FIRESTATION, 3, 34, 24);
    b.building_around(TOOL_FIRESTATION, 3, 56, 20);
    b.building_around(TOOL_FIRESTATION, 3, 54, 36);
    b.building_around(TOOL_FIRESTATION, 3, 70, 48);
    b.building_around(TOOL_FIRESTATION, 3, 70, 66);
    b.building_around(TOOL_FIRESTATION, 3, 74, 34);
    b.building_around(TOOL_FIRESTATION, 3, 90, 28);
    b.building_around(TOOL_FIRESTATION, 3, 92, 46);
    b.building_around(TOOL_FIRESTATION, 3, 88, 64);
    b.building_around(TOOL_FIRESTATION, 3, 76, 92);
    b.building_around(TOOL_FIRESTATION, 3, 100, 82);
    b.building_around(TOOL_FIRESTATION, 3, 108, 30);
    b.building_around(TOOL_FIRESTATION, 3, 116, 70);
}

} // namespace

int main(int argc, char **argv)
{
    static Builder b;
    b.name = "portland_map";
    b.terrain_at = terrain_at;
    b.zone_kind = zone_kind;
    b.core_x = 50;
    b.core_y = 60;
    b.green_min = 40;
    RunOptions options;
    options.seed = 2020;
    options.first_year = 1990;
    options.years = 30;
    options.tax = 5;
    return build_and_write(b, argc, argv, layout, options);
}

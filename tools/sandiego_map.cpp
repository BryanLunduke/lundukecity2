// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/sandiego.cty, the map of the San Diego (2025) Zoo
// Escape scenario.
//
// The map is drawn here from a hand-made outline of San Diego's real
// geography (no other game's map is used): the Pacific along the west,
// Mission Bay with Fiesta Island and Vacation Isle behind Mission Beach,
// the San Diego River down Mission Valley, Point Loma with Shelter and
// Harbor Islands inside it, San Diego Bay curving from the harbour mouth
// past the airport and downtown to National City, and Coronado with North
// Island and the Silver Strand across the bay. Map north is real north;
// one tile is about 165 m east-west and 180 m north-south (the map spans
// Ocean Beach to La Mesa's edge and Clairemont to National City).
//
// The city is laid out as it stands in the 2020s: downtown and the East
// Village on the bay, Balboa Park with the San Diego Zoo, Hillcrest and
// North Park, Mission Valley, Old Town, Point Loma, Pacific Beach and
// Ocean Beach, Coronado, I-5, I-8, I-15, SR-163 through the park, SR-94,
// the Coronado Bridge, the airport on the bay, the marine terminals,
// Petco Park and Snapdragon Stadium, North Island and the Naval Base, and
// Kearny Mesa. San Diego's power comes from plants outside the city; here
// it is a few plants (coal-plant tiles) in the industrial districts. After
// the city has grown, the Zoo (a City 2 landmark) goes in Balboa Park.
//
//   sandiego_map <output.cty> [preview.ppm]
//
// The run is deterministic (fixed seed).

#include "map_builder.hpp"

using namespace mapgen;

namespace {

// ---- Geography outline (tile coordinates, x east, y south) --------------

// The mainland, from the north-west corner clockwise; San Diego Bay is the
// water left between it and Coronado.
const std::vector<Point> kMainland = {
    {3, 0},    {120, 0},  {120, 100}, {91, 100}, {87, 93},  {81, 87},  {73, 82},  {67, 78},  {64, 75},
    {61, 71},  {57, 68.5}, {54.5, 64}, {53, 60}, {48, 58.5}, {44, 58.5}, {36, 58.5}, {31, 58},
    {27, 59},  {22, 62},  {18, 66},  {16, 72},  {15, 80},  {14, 88},  {12, 95},  {10, 92},  {8, 84},
    {6, 72},   {5, 62},   {4, 50},   {4, 41},   {4, 35},   {3, 32},
};
// Mission Bay and its channel to the sea.
const std::vector<Point> kMissionBay = {
    {7, 12}, {14, 8}, {24, 9}, {31, 12}, {34, 19}, {33, 28}, {28, 33}, {18, 34}, {8, 34}, {6.5, 28}, {6.5, 18},
};
// The San Diego River's flood channel, Ocean Beach up Mission Valley.
const std::vector<LinePoint> kRiver = {{0, 35.5, 3}, {9, 36, 2}, {24, 36.5, 2}, {40, 37, 1.6}};
// Coronado, North Island and the Silver Strand.
const std::vector<Point> kCoronado = {
    {28, 84}, {30, 77}, {36, 72.5}, {44, 71.5}, {52, 72.5}, {56, 75}, {58, 80}, {60, 86},
    {62.5, 92}, {64.5, 100}, {60, 100}, {58.5, 94}, {54, 92}, {46, 91}, {38, 90}, {32, 88},
};

// 'W' water, 'T' woods, 'L' land.
char terrain_at(int tx, int ty)
{
    const double x = tx + 0.5, y = ty + 0.5;
    const bool fiesta = in_ellipse(x, y, 26, 24, 3.5, 3);
    const bool vacation = in_ellipse(x, y, 16, 20, 3, 2);
    const bool harbor = in_rect(x, y, 37, 60, 46, 61.5);
    const bool shelter = in_ellipse(x, y, 21, 65, 2.5, 1.2);
    const bool land = (inside(kMainland, x, y) && !inside(kMissionBay, x, y) && !in_rect(x, y, 0, 32, 8, 34.5) &&
                       !in_line(kRiver, x, y)) ||
                      inside(kCoronado, x, y) || fiesta || vacation || harbor || shelter;
    if (!land) {
        return 'W';
    }
    // Woods: Balboa Park (but not SR-163 and Park Boulevard; the Zoo is
    // put in later), Presidio Park, Tecolote Canyon, Fiesta Island, Fort
    // Rosecrans and Cabrillo on Point Loma, Sunset Cliffs, Mission Trails,
    // Chollas Lake, Florida Canyon, Kate Sessions Park.
    const bool balboa = in_rect(x, y, 62, 42, 72, 58) && tx != 62 && tx != 68;
    if (balboa || fiesta || in_ellipse(x, y, 36, 41, 2, 1.5) || in_rect(x, y, 37, 12, 40, 26) ||
        in_rect(x, y, 8, 80, 15, 96) || in_rect(x, y, 4.5, 52, 6.5, 62) || in_ellipse(x, y, 114, 8, 6, 6) ||
        in_ellipse(x, y, 106, 62, 2.5, 2) || in_ellipse(x, y, 13, 4, 2, 2)) {
        return 'T';
    }
    return 'L';
}

// Zone kind for a 3x3 lot centred on (x, y).
EditingTool zone_kind(int x, int y)
{
    // Industry: Barrio Logan and the Naval Base along the bay, North
    // Island, Kearny Mesa, the Midway district by the airport, Morena.
    if (in_rect(x, y, 63, 72, 90, 99) || in_rect(x, y, 29, 72, 52, 82) || in_rect(x, y, 70, 4, 104, 18) ||
        in_rect(x, y, 30, 43, 47, 50) || in_rect(x, y, 34, 27, 44, 35)) {
        return TOOL_INDUSTRIAL;
    }
    // Downtown and the East Village, with apartments mixed in.
    if (in_rect(x, y, 52, 56, 61, 71)) {
        return ((x / 3) + (y / 4)) % 5 >= 3 ? TOOL_RESIDENTIAL : TOOL_COMMERCIAL;
    }
    // Main streets and centres: Mission Valley's malls and hotels,
    // Hillcrest, University Avenue through North Park and City Heights,
    // Pacific Beach, Ocean Beach, Point Loma Village, Orange Avenue in
    // Coronado, Clairemont, Old Town, El Cajon Boulevard.
    if (in_rect(x, y, 44, 40, 119, 43) || in_rect(x, y, 58, 36, 72, 42) || in_rect(x, y, 70, 45, 110, 47) ||
        in_rect(x, y, 3, 0, 14, 8) || in_rect(x, y, 4, 39, 12, 45) || in_rect(x, y, 18, 60, 26, 64) ||
        in_rect(x, y, 46, 78, 52, 88) || in_rect(x, y, 40, 4, 60, 8) || in_rect(x, y, 30, 38, 38, 42) ||
        in_rect(x, y, 70, 51, 115, 53)) {
        return TOOL_COMMERCIAL;
    }
    const int d = std::abs(x - 57) + std::abs(y - 63);
    const int every = d < 36 ? 4 : 6;
    if (((x / 3) + 2 * (y / 4)) % every == 0) {
        return TOOL_COMMERCIAL;
    }
    return TOOL_RESIDENTIAL;
}

void layout(Builder &b)
{
    // Big buildings before the streets: the airport on the bay, the marine
    // terminals at National City and North Island, Petco Park and
    // Snapdragon Stadium, and the plants.
    if (!b.clear_land(37, 51, 6, 6) || !b.tool(TOOL_AIRPORT, 38, 52)) {
        std::fprintf(stderr, "sandiego_map: could not place the airport\n");
    }
    const struct {
        EditingTool tool;
        int x, y;
    } big[] = {
        {TOOL_SEAPORT, 82, 84},    {TOOL_SEAPORT, 40, 74},    {TOOL_STADIUM, 56, 62},    {TOOL_STADIUM, 78, 34},
        {TOOL_COALPOWER, 68, 72},  {TOOL_COALPOWER, 80, 76},  {TOOL_COALPOWER, 90, 78},  {TOOL_COALPOWER, 90, 10},
        {TOOL_COALPOWER, 100, 12}, {TOOL_COALPOWER, 33, 76},  {TOOL_COALPOWER, 46, 76},  {TOOL_COALPOWER, 32, 45},
        {TOOL_COALPOWER, 38, 29},  {TOOL_COALPOWER, 80, 10},  {TOOL_COALPOWER, 100, 84},
    };
    for (const auto &item : big) {
        if (!b.building(item.tool, 4, item.x, item.y)) {
            std::fprintf(stderr, "sandiego_map: could not place tool %d at %d,%d\n", static_cast<int>(item.tool),
                         item.x, item.y);
        }
    }

    // I-5 down past Mission Bay and Old Town, round the airport and
    // downtown, and south-east past Barrio Logan to National City. I-8
    // along Mission Valley, I-15, SR-163 up through Balboa Park, SR-94
    // east from downtown, the Coronado Bridge, Mission Boulevard, Park
    // Boulevard past the Zoo, and the Silver Strand.
    b.road_line(30, 0, 30, 42, false);
    b.road_stairs(30, 42, 48, 51);
    b.road_stairs(48, 51, 61, 56);
    b.road_line(61, 56, 61, 72, false);
    b.road_stairs(61, 72, 88, 99);
    b.road_line(5, 39, 119, 39, false);
    b.road_line(88, 0, 88, 68, false);
    b.road_line(62, 56, 62, 0, false);
    b.road_line(61, 68, 119, 68, false);
    b.road_line(55, 76, 66, 76, true);
    b.road_line(4, 0, 4, 31, false);
    b.road_line(68, 40, 68, 66, false);
    b.road_line(62, 86, 62, 99, false);

    // Streets: a street every fourth row, an avenue every fourth column
    // downtown and every seventh elsewhere, kept clear of the freeways.
    auto central = [](int x, int y) { return x >= 50 && x <= 60 && y >= 55 && y <= 72; };
    auto clear_of = [](int x, int y) {
        const int spines[][3] = {{30, 0, 42}, {61, 56, 72}, {88, 0, 68}, {62, 0, 56}, {4, 0, 31}, {68, 40, 66}};
        for (const auto &sp : spines) {
            if (y >= sp[1] && y <= sp[2] && std::abs(sp[0] - x) <= 2 && sp[0] != x) {
                return false;
            }
        }
        return true;
    };
    b.grid([&](int x, int y) {
        if (y >= 37 && y <= 42) {
            return false; // the river and I-8
        }
        return y % 4 == 2 && x > 0;
    }, [&](int x, int y) {
        if (x < 1 || x > W - 2) {
            return false;
        }
        if (central(x, y)) {
            return x % 4 == 1 && clear_of(x, y);
        }
        return x % 7 == 3 && clear_of(x, y);
    });

    // Power across the water: Coronado from Barrio Logan beside the
    // bridge, Harbor and Shelter Islands, Vacation Isle.
    b.wire_across(54, 77, 66, 77);
    b.wire_across(41, 57, 41, 61);
    b.wire_across(21, 61, 21, 65);

    // Parks: Waterfront Park, Seaport Village, Horton Plaza Park, Old Town,
    // Mission Bay's shore parks, Morley Field, Trolley Barn, Spreckels
    // Park, Tidelands Park.
    b.park_rect(53, 59, 53, 61);
    b.park_rect(55, 66, 56, 67);
    b.park_rect(57, 60, 58, 60);
    b.park_rect(34, 38, 35, 38);
    b.park_rect(7, 14, 7, 30);
    b.park_rect(30, 16, 32, 26);
    b.park_rect(70, 44, 72, 44);
    b.park_rect(48, 82, 49, 83);
    b.park_rect(56, 78, 57, 79);

    // Police: SDPD Central, Western, Northern, Northeastern, Mid-City,
    // Southeastern and Eastern divisions, and Coronado. None by the park.
    b.building_around(TOOL_POLICESTATION, 3, 56, 66);
    b.building_around(TOOL_POLICESTATION, 3, 28, 50);
    b.building_around(TOOL_POLICESTATION, 3, 14, 4);
    b.building_around(TOOL_POLICESTATION, 3, 50, 12);
    b.building_around(TOOL_POLICESTATION, 3, 96, 50);
    b.building_around(TOOL_POLICESTATION, 3, 100, 84);
    b.building_around(TOOL_POLICESTATION, 3, 112, 28);
    b.building_around(TOOL_POLICESTATION, 3, 48, 86);
    b.building_around(TOOL_POLICESTATION, 3, 14, 60);
    // Fire stations.
    b.building_around(TOOL_FIRESTATION, 3, 58, 70);
    b.building_around(TOOL_FIRESTATION, 3, 10, 14);
    b.building_around(TOOL_FIRESTATION, 3, 10, 46);
    b.building_around(TOOL_FIRESTATION, 3, 12, 72);
    b.building_around(TOOL_FIRESTATION, 3, 40, 20);
    b.building_around(TOOL_FIRESTATION, 3, 22, 4);
    b.building_around(TOOL_FIRESTATION, 3, 64, 30);
    b.building_around(TOOL_FIRESTATION, 3, 76, 48);
    b.building_around(TOOL_FIRESTATION, 3, 80, 62);
    b.building_around(TOOL_FIRESTATION, 3, 80, 24);
    b.building_around(TOOL_FIRESTATION, 3, 104, 44);
    b.building_around(TOOL_FIRESTATION, 3, 104, 76);
    b.building_around(TOOL_FIRESTATION, 3, 112, 12);
    b.building_around(TOOL_FIRESTATION, 3, 52, 82);
    b.building_around(TOOL_FIRESTATION, 3, 40, 84);
    b.building_around(TOOL_FIRESTATION, 3, 74, 76);
}

// The San Diego Zoo in Balboa Park, beside Park Boulevard, with a power
// line east under the boulevard to the nearest powered block.
void after_growth(Builder &b)
{
    place_landmark(b, SPECIAL_ZOO, 65, 46);
    for (int x = 68; x < W; ++x) {
        const MapValue value = b.sim.map[x][47];
        if (x > 68 && (value & CONDBIT)) {
            break;
        }
        const MapTile tile = value & LOMASK;
        if (tile >= WOODS_LOW && tile <= WOODS_HIGH) {
            b.tool(TOOL_BULLDOZER, x, 47);
        }
        b.tool(TOOL_WIRE, x, 47);
    }
}

} // namespace

int main(int argc, char **argv)
{
    static Builder b;
    b.name = "sandiego_map";
    b.terrain_at = terrain_at;
    b.zone_kind = zone_kind;
    b.core_x = 57;
    b.core_y = 63;
    b.green_min = 40;
    RunOptions options;
    options.seed = 2025;
    options.first_year = 1995;
    options.years = 30;
    options.tax = 5;
    options.after_growth = after_growth;
    return build_and_write(b, argc, argv, layout, options);
}

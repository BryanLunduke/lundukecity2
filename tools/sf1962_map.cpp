// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/sanfrancisco1962.cty, the map of the San Francisco
// (1962) Prison Escape scenario.
//
// The map is drawn here from a hand-made outline of San Francisco's real
// geography (no other game's map is used): the Pacific to the west, the
// Golden Gate with the Marin Headlands across it, the Bay to the north and
// east with Alcatraz, Yerba Buena and Treasure Island, Lake Merced in the
// south-west, and the bay shore from the Embarcadero down past China Basin,
// Islais Creek and Hunters Point to Candlestick Point. Map north is real
// north; one tile is about 120 m east-west and 140 m north-south (the map
// spans Ocean Beach to Yerba Buena Island and the Marin shore to the county
// line). Alcatraz sits about 900 m off Fisherman's Wharf here instead of the
// real 2 km, so the island stays on the map with the city. The prison has no
// road to the mainland: in the scenario the escapees cross the water and
// come ashore at Fisherman's Wharf (CitySession::kSf1962LandingX/Y), which
// is where the fires and the crime are.
//
// The city is laid out as it stood in 1962: the Golden Gate and Bay
// Bridges, the Embarcadero and its new double-deck freeway, the Central and
// Bayshore Freeways, the Southern Pacific line from Fourth and Townsend,
// Golden Gate Park, the Presidio, Kezar Stadium and the new Candlestick
// Park, the port's piers and Hunters Point shipyard, and the fuel-fired
// plants at Potrero and Hunters Point (coal-plant tiles). No BART, no
// Transamerica Pyramid, no Interstate 280. After the city has grown, the
// federal penitentiary goes on Alcatraz with a cable to the Wharf.
//
//   sf1962_map <output.cty> [preview.ppm]
//
// The run is deterministic (fixed seed).

#include "map_builder.hpp"

using namespace mapgen;

namespace {

// ---- Geography outline (tile coordinates, x east, y south) --------------

// San Francisco itself, from the south-west corner clockwise.
const std::vector<Point> kCity = {
    {4, 100}, {3.5, 80}, {3.5, 60}, {4, 46}, {6, 41}, {8, 38}, {14, 37.5}, {18, 37}, {22, 33}, {25, 27},
    {27.5, 20}, {32, 21.5}, {41, 23}, {50, 22.5}, {56, 22}, {61, 21}, {64, 20.5}, {69, 21}, {75, 20.5},
    {80, 20.5}, {83.5, 22}, {88, 27}, {91.5, 31}, {95, 35.5}, {96.5, 42}, {97, 51}, {101, 60},
    {100.5, 67}, {103, 74}, {106, 79}, {112, 82}, {116.5, 85.5}, {113, 89}, {108, 91}, {104, 95},
    {101, 99}, {98, 100},
};
// The Marin Headlands across the Golden Gate.
const std::vector<Point> kMarin = {
    {0, 0}, {50, 0}, {47, 1.5}, {40, 3.5}, {33, 3}, {28, 3}, {21, 4.5}, {12, 5.5}, {0, 6.5},
};
// China Basin's channel (Mission Creek) and Islais Creek.
const std::vector<Point> kChinaBasin = {{89, 45.5}, {97, 45}, {97, 47.5}, {89, 47.5}};
const std::vector<Point> kIslais = {{93, 71.5}, {102, 71}, {102, 73.5}, {93, 73.5}};

// 'W' water, 'T' woods, 'L' land.
char terrain_at(int tx, int ty)
{
    const double x = tx + 0.5, y = ty + 0.5;
    const bool alcatraz = in_ellipse(x, y, 69.5, 12, 4.2, 3.2);
    const bool yerba_buena = in_ellipse(x, y, 109.5, 36.5, 5.5, 4);
    const bool treasure = in_rect(x, y, 105, 9, 115, 25) || in_rect(x, y, 108.5, 25, 110.5, 33);
    const bool land = inside(kCity, x, y) || inside(kMarin, x, y) || alcatraz || yerba_buena || treasure;
    if (!land || inside(kChinaBasin, x, y) || inside(kIslais, x, y) ||
        in_ellipse(x, y, 17, 90, 6, 4.5)) { // Lake Merced
        return 'W';
    }
    // Woods: the Marin Headlands, the Presidio, Lincoln Park and Land's End,
    // Golden Gate Park west of Kezar, Mount Sutro and Twin Peaks, Mount
    // Davidson, Glen Canyon, McLaren Park, Fort Funston, and the slopes of
    // Yerba Buena Island.
    if (inside(kMarin, x, y) || in_rect(x, y, 13, 24, 44, 33) || in_rect(x, y, 6, 38, 15, 43) ||
        in_rect(x, y, 5, 48, 44, 53) || in_ellipse(x, y, 47, 60, 3.5, 2.5) || in_ellipse(x, y, 54, 64, 3, 3.5) ||
        in_ellipse(x, y, 47, 74, 2.5, 2) || in_ellipse(x, y, 60, 70, 2, 2) || in_ellipse(x, y, 73, 90, 5, 3) ||
        in_rect(x, y, 4, 82, 8, 100) || (yerba_buena && !in_rect(x, y, 104, 35, 115, 38))) {
        return 'T';
    }
    return 'L';
}

// Zone kind for a 3x3 lot centred on (x, y).
EditingTool zone_kind(int x, int y)
{
    // Industry: South of Market by the rail yards, Mission Bay, Dogpatch
    // and Potrero Point, Islais Creek, Bayview and Hunters Point, the
    // Navy's Treasure Island.
    if (in_rect(x, y, 84, 40, 96, 54) || in_rect(x, y, 86, 54, 101, 70) || in_rect(x, y, 86, 70, 114, 96) ||
        in_rect(x, y, 104, 8, 116, 26)) {
        return TOOL_INDUSTRIAL;
    }
    // Downtown: the Financial District, Union Square, Chinatown and Market
    // Street, with apartments mixed in; Fisherman's Wharf along the shore.
    if (in_rect(x, y, 74, 26, 93, 42)) {
        return ((x / 3) + (y / 4)) % 5 >= 3 ? TOOL_RESIDENTIAL : TOOL_COMMERCIAL;
    }
    if (in_rect(x, y, 62, 20, 84, 23)) {
        return TOOL_COMMERCIAL;
    }
    // Neighbourhood shopping streets: Geary and Clement, Haight, Mission
    // Street, Twenty-fourth, Irving, Ocean Avenue, Chestnut and Union.
    if (in_rect(x, y, 8, 38, 60, 41) || in_rect(x, y, 48, 52, 56, 55) || in_rect(x, y, 74, 46, 78, 84) ||
        in_rect(x, y, 66, 64, 84, 67) || in_rect(x, y, 10, 55, 44, 57) || in_rect(x, y, 30, 82, 60, 85) ||
        in_rect(x, y, 50, 24, 66, 27)) {
        return TOOL_COMMERCIAL;
    }
    const int d = std::abs(x - 84) + std::abs(y - 34);
    const int every = d < 36 ? 4 : 6;
    if (((x / 3) + 2 * (y / 4)) % every == 0) {
        return TOOL_COMMERCIAL;
    }
    return TOOL_RESIDENTIAL;
}

void layout(Builder &b)
{
    // Southern Pacific: Fourth and Townsend down the bay side.
    b.rail_line(86, 48, 86, 99);
    b.rail_line(87, 48, 92, 48);

    // Big buildings before the streets: Kezar Stadium at the park's east
    // end, Candlestick Park on its point, the port's piers south of the
    // Ferry Building and the Hunters Point shipyard, and the plants at
    // Potrero (Station P), Islais Creek and Hunters Point.
    const struct {
        EditingTool tool;
        int x, y;
    } big[] = {
        {TOOL_STADIUM, 45, 54}, {TOOL_STADIUM, 97, 94},
        {TOOL_SEAPORT, 91, 37}, {TOOL_SEAPORT, 108, 83},
        {TOOL_COALPOWER, 96, 58}, {TOOL_COALPOWER, 96, 63}, {TOOL_COALPOWER, 103, 86},
        {TOOL_COALPOWER, 96, 75}, {TOOL_COALPOWER, 90, 88}, {TOOL_COALPOWER, 110, 14},
        {TOOL_COALPOWER, 91, 66}, {TOOL_COALPOWER, 98, 80}, {TOOL_COALPOWER, 92, 80},
    };
    // San Francisco International (really ten miles south, past the
    // county line) at the foot of the bay shore, as Boeing Field is in the
    // Seattle map: the city's shops need an airport to grow.
    if (!b.clear_land(90, 92, 6, 6) || !b.tool(TOOL_AIRPORT, 91, 93)) {
        std::fprintf(stderr, "sf1962_map: could not place the airport\n");
    }
    for (const auto &item : big) {
        if (!b.building(item.tool, 4, item.x, item.y)) {
            std::fprintf(stderr, "sf1962_map: could not place tool %d at %d,%d\n", static_cast<int>(item.tool),
                         item.x, item.y);
        }
    }

    // The Golden Gate Bridge from Fort Point to the Headlands, carrying
    // US 101 down Park Presidio and Nineteenth Avenue; Lombard Street to
    // Van Ness. The Bay Bridge from Rincon Hill over Yerba Buena Island,
    // with the causeway to Treasure Island.
    b.road_line(28, 99, 28, 0, true);
    b.road_line(28, 25, 69, 25, false);
    b.road_line(69, 21, 69, 47, false);
    b.road_line(94, 36, 119, 36, true);
    b.road_line(110, 35, 110, 9, true);
    // The Embarcadero along the piers, and the double-deck Embarcadero
    // Freeway (1959) from the Bay Bridge to Broadway.
    b.road_stairs(83, 23, 94, 36);
    b.road_stairs(84, 23, 95, 36);
    // Market Street from the Ferry Building to Castro, and the Central
    // and Bayshore Freeways from Market south along the bay side.
    b.road_stairs(91, 32, 61, 58);
    b.road_line(70, 47, 82, 47, false);
    b.road_line(82, 47, 82, 99, false);
    // Geary Boulevard, Sloat and the Great Highway along Ocean Beach.
    b.road_line(5, 39, 60, 39, false);
    b.road_line(5, 81, 60, 81, false);
    b.road_line(5, 41, 5, 99, false);

    // Streets: a street every fourth row downtown, North Beach and the
    // Tenderloin (single-lot blocks), every seventh row elsewhere; avenues
    // every seventh column downtown and every seventh elsewhere too, kept clear
    // of the spines and the rail line.
    auto central = [](int x, int y) { return x >= 61 && x <= 95 && y >= 20 && y <= 47; };
    auto clear_of = [](int x, int y) {
        const int spines[][3] = {{28, 0, 99}, {69, 21, 47}, {82, 47, 99}, {86, 48, 99}, {5, 41, 99}, {110, 9, 35}};
        for (const auto &sp : spines) {
            if (y >= sp[1] && y <= sp[2] && std::abs(sp[0] - x) <= 2 && sp[0] != x) {
                return false;
            }
        }
        return true;
    };
    b.grid([&](int x, int y) {
        if (y == 25 || y == 39 || y == 81 || y == 36) {
            return false; // laid above
        }
        return central(x, y) ? y % 4 == 1 : (y % 7 == 4 && (y < 20 || y > 47 || x < 60));
    }, [&](int x, int y) {
        if (x < 1 || x > W - 2) {
            return false;
        }
        if (central(x, y)) {
            return x % 7 == 4 && clear_of(x, y);
        }
        return x % 7 == 4 && clear_of(x, y);
    });
    // Alcatraz: the dock road up to the cellhouse.
    b.road_line(71, 10, 71, 11, false);

    // Underwater power: to Treasure Island along the causeway (the road
    // does not carry it), across China Basin and Islais Creek.
    b.wire_across(111, 38, 111, 24);
    b.wire_across(91, 44, 91, 49);
    b.wire_across(95, 70, 95, 75);

    // Parks: Union Square, Washington Square, Portsmouth Square, Civic
    // Center Plaza, Alamo Square, Dolores Park, Buena Vista, Lafayette and
    // Alta Plaza, the Marina Green, Aquatic Park, Bernal Heights, Stern
    // Grove, the Panhandle.
    b.park_rect(80, 37, 82, 38);
    b.park_rect(78, 28, 79, 29);
    b.park_rect(84, 32, 84, 33);
    b.park_rect(72, 43, 74, 45);
    b.park_rect(62, 44, 64, 45);
    b.park_rect(68, 58, 71, 60);
    b.park_rect(56, 51, 58, 52);
    b.park_rect(64, 31, 66, 32);
    b.park_rect(56, 31, 58, 32);
    b.park_rect(50, 22, 58, 23);
    b.park_rect(66, 21, 67, 22);
    b.park_rect(73, 70, 77, 72);
    b.park_rect(24, 72, 27, 74);
    b.park_rect(49, 50, 58, 50);

    // Police (the 1962 SFPD districts): Central, Northern, Park, Richmond,
    // Taraval, Ingleside, Mission, Southern, Potrero, and the Hall of
    // Justice downtown. None by the Wharf: the islands were the federal
    // government's business.
    b.building_around(TOOL_POLICESTATION, 3, 82, 31);
    b.building_around(TOOL_POLICESTATION, 3, 64, 38);
    b.building_around(TOOL_POLICESTATION, 3, 53, 56);
    b.building_around(TOOL_POLICESTATION, 3, 24, 44);
    b.building_around(TOOL_POLICESTATION, 3, 24, 70);
    b.building_around(TOOL_POLICESTATION, 3, 64, 84);
    b.building_around(TOOL_POLICESTATION, 3, 72, 62);
    b.building_around(TOOL_POLICESTATION, 3, 84, 44);
    b.building_around(TOOL_POLICESTATION, 3, 90, 60);
    b.building_around(TOOL_POLICESTATION, 3, 98, 82);
    // Fire stations.
    b.building_around(TOOL_FIRESTATION, 3, 86, 36);
    b.building_around(TOOL_FIRESTATION, 3, 76, 30);
    b.building_around(TOOL_FIRESTATION, 3, 58, 30);
    b.building_around(TOOL_FIRESTATION, 3, 40, 40);
    b.building_around(TOOL_FIRESTATION, 3, 14, 46);
    b.building_around(TOOL_FIRESTATION, 3, 14, 62);
    b.building_around(TOOL_FIRESTATION, 3, 36, 66);
    b.building_around(TOOL_FIRESTATION, 3, 14, 76);
    b.building_around(TOOL_FIRESTATION, 3, 44, 88);
    b.building_around(TOOL_FIRESTATION, 3, 62, 62);
    b.building_around(TOOL_FIRESTATION, 3, 66, 46);
    b.building_around(TOOL_FIRESTATION, 3, 78, 56);
    b.building_around(TOOL_FIRESTATION, 3, 76, 78);
    b.building_around(TOOL_FIRESTATION, 3, 92, 52);
    b.building_around(TOOL_FIRESTATION, 3, 100, 80);
    b.building_around(TOOL_FIRESTATION, 3, 80, 92);
}

// The federal penitentiary on Alcatraz, fed by a cable from the Wharf.
void after_growth(Builder &b)
{
    place_landmark(b, SPECIAL_PRISON, 67, 10);
    for (int y = 12; y <= 22; ++y) {
        if (!(b.sim.map[71][y] & CONDBIT)) {
            b.tool(TOOL_WIRE, 71, y);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    static Builder b;
    b.name = "sf1962_map";
    b.terrain_at = terrain_at;
    b.zone_kind = zone_kind;
    b.core_x = 84;
    b.core_y = 34;
    b.green_min = 46;
    RunOptions options;
    options.seed = 1962;
    options.first_year = 1932;
    options.years = 30;
    options.tax = 5;
    options.after_growth = after_growth;
    return build_and_write(b, argc, argv, layout, options);
}

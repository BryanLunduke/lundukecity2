// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Builds data/scenarios/seattle.cty, the map of the Seattle scenario.
//
// The map is drawn here from a hand-made outline of Seattle's real
// geography (no other game's map is used): Puget Sound and Elliott Bay to
// the west, the Ship Canal from Salmon Bay through Fremont into Lake Union,
// Portage Bay and Union Bay out to Lake Washington in the east, Green Lake
// in the north and the Duwamish waterways around Harbor Island in the
// south. Map north is real north; one tile is about 115 m east-west and
// 120 m north-south (the map spans roughly N 85th St to S Spokane St and
// Puget Sound to Lake Washington).
//
// The city is then laid out with the engine's own tools (streets, I-5 and
// Aurora spines, bridges, rail, zones, parks, police, fire, power) and run
// for a few decades with disasters off so the zones grow in. The result is
// written in the classic 27,120-byte city format the scenario loader reads.
//
//   seattle_map <output.cty> [preview.ppm]
//
// The run is deterministic (fixed seed).

#include "micropolis.h"
#include "terrain_paint.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int W = WORLD_W;
constexpr int H = WORLD_H;

using Point = std::pair<double, double>;

// ---- Geography outline (tile coordinates, x east, y south) --------------

// Puget Sound and Elliott Bay: everything west of this shore.
const std::vector<Point> kSound = {
    {0, 0}, {16, 0}, {15, 10}, {16, 19}, {14, 22}, {8, 25}, {4, 28}, {8, 31},
    {9, 36}, {8, 42}, {10, 48}, {14, 52}, {30, 53}, {31, 51}, {36, 51}, {37, 53},
    {44, 57}, {50, 62}, {55, 67}, {59, 71}, {61, 74}, {61, 79}, {58, 82}, {57, 83},
    {45, 83}, {38, 82}, {31, 79}, {27, 81}, {20, 86}, {12, 91}, {6, 95}, {0, 96},
};
// Lake Union.
const std::vector<Point> kLakeUnion = {
    {57.5, 35}, {63, 33.5}, {68, 34.5}, {70, 38}, {70.5, 45}, {69.5, 50}, {67.5, 54},
    {64, 56.5}, {60, 56.5}, {59, 52}, {58, 46}, {57, 40},
};
// Portage Bay and the Montlake Cut.
const std::vector<Point> kPortage = {
    {67, 34}, {72, 32.5}, {77, 33}, {79, 34.5}, {86, 33.5}, {86, 35.5}, {79, 36.5}, {76, 37.5}, {71, 37}, {68, 36.5},
};
// Union Bay.
const std::vector<Point> kUnionBay = {
    {85, 32}, {90, 28}, {96, 26.5}, {100, 29}, {100, 38}, {93, 39.5}, {86, 37},
};
// Lake Washington: everything east of this shore.
const std::vector<Point> kLakeWashington = {
    {98, 0}, {120, 0}, {120, 100}, {103, 100}, {104.5, 95}, {101, 92}, {98, 86}, {97, 80},
    {99, 72}, {99, 62}, {101, 56}, {101, 46}, {100.5, 40}, {98.5, 37}, {99, 30}, {96.5, 26},
    {97, 22}, {98, 14}, {97.5, 6},
};
// The Duwamish: West Waterway, East Waterway and the river south of
// Harbor Island.
const std::vector<Point> kWestWaterway = {{44.5, 82.5}, {47.5, 82.5}, {49.5, 100}, {46.5, 100}};
const std::vector<Point> kEastWaterway = {{56.5, 82.5}, {59.5, 82.5}, {59.5, 97.5}, {56.5, 97.5}};
const std::vector<Point> kDuwamish = {{47, 95.5}, {59.5, 95.5}, {59.5, 97.5}, {55.5, 97.5}, {55.5, 100}, {52.5, 100}, {52.5, 97.5}, {47.5, 97.5}};

// The Ship Canal: Salmon Bay, the Ballard Locks and Fremont, as a line
// with a width.
struct CanalPoint {
    double x, y, width;
};
const std::vector<CanalPoint> kCanal = {
    {13, 22, 3.6}, {20, 22.5, 3.4}, {24, 23.2, 2.4}, {30, 24.2, 3.0}, {38, 25.2, 2.6},
    {44, 27.2, 2.4}, {50, 30.2, 2.2}, {54, 32.2, 2.2}, {58.5, 35, 2.4},
};

bool inside(const std::vector<Point> &poly, double x, double y)
{
    bool in = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].first, yi = poly[i].second;
        const double xj = poly[j].first, yj = poly[j].second;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            in = !in;
        }
    }
    return in;
}

double segment_distance(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double len = dx * dx + dy * dy;
    double t = len > 0 ? ((px - ax) * dx + (py - ay) * dy) / len : 0;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = ax + t * dx - px, cy = ay + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

bool in_canal(double x, double y)
{
    for (std::size_t i = 0; i + 1 < kCanal.size(); ++i) {
        const CanalPoint &a = kCanal[i];
        const CanalPoint &b = kCanal[i + 1];
        const double width = (a.width + b.width) / 2;
        if (segment_distance(x, y, a.x, a.y, b.x, b.y) <= width / 2) {
            return true;
        }
    }
    return false;
}

bool in_rect(double x, double y, double x0, double y0, double x1, double y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

bool in_ellipse(double x, double y, double cx, double cy, double rx, double ry)
{
    const double dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0;
}

// 'W' water, 'T' woods, 'L' land.
char terrain_at(int tx, int ty)
{
    const double x = tx + 0.5, y = ty + 0.5;
    if (inside(kSound, x, y) || inside(kLakeUnion, x, y) || inside(kPortage, x, y) ||
        inside(kUnionBay, x, y) || inside(kLakeWashington, x, y) || inside(kWestWaterway, x, y) ||
        inside(kEastWaterway, x, y) || inside(kDuwamish, x, y) || in_canal(x, y) ||
        in_ellipse(x, y, 62.5, 7.5, 4.6, 3.4)) { // Green Lake
        return 'W';
    }
    // Big natural parks: Discovery Park, Golden Gardens bluff, Woodland
    // Park, the Arboretum, Seward Park, Lincoln Park, Interlaken.
    if (in_ellipse(x, y, 10.5, 30.5, 5.5, 4.5) || in_rect(x, y, 15.5, 1, 18, 12) ||
        in_rect(x, y, 56, 9.5, 58, 14) || in_rect(x, y, 89, 42, 92, 57) ||
        in_ellipse(x, y, 102, 96.5, 2.6, 2.8) || in_rect(x, y, 4, 95.5, 10, 100) ||
        in_rect(x, y, 81, 42, 86, 44.5)) {
        return 'T';
    }
    return 'L';
}

// ---- Layout --------------------------------------------------------------

struct Gen {
    Micropolis sim;
    std::vector<char> terrain = std::vector<char>(W * H, 'L');
    int placed_roads = 0;
    int placed_zones = 0;

    char &ter(int x, int y) { return terrain[y * W + x]; }
    MapTile tile(int x, int y) const { return sim.map[x][y] & LOMASK; }
    bool dirt(int x, int y) const { return Micropolis::testBounds(x, y) && tile(x, y) == DIRT; }
    bool water(int x, int y) const
    {
        const MapTile t = tile(x, y);
        return t >= WATER_LOW && t <= WATER_HIGH;
    }
    bool road(int x, int y) const
    {
        if (!Micropolis::testBounds(x, y)) {
            return false;
        }
        const MapTile t = tile(x, y);
        return (t >= ROADBASE && t <= LASTROAD);
    }
    bool rail(int x, int y) const
    {
        if (!Micropolis::testBounds(x, y)) {
            return false;
        }
        const MapTile t = tile(x, y);
        return t >= RAILBASE && t <= LASTRAIL;
    }

    void paint_terrain()
    {
        std::vector<unsigned short> tiles(W * H, DIRT);
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                ter(x, y) = terrain_at(x, y);
                unsigned short tile = DIRT;
                if (ter(x, y) == 'W') {
                    tile = static_cast<unsigned short>(terrain_paint::piece_tile(terrain_paint::Piece::Water));
                } else if (ter(x, y) == 'T') {
                    tile = static_cast<unsigned short>(terrain_paint::piece_tile(terrain_paint::Piece::Trees));
                }
                tiles[y * W + x] = tile;
            }
        }
        terrain_paint::fix_edges(tiles, W, H, 0, 0, W - 1, H - 1);
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                sim.map[x][y] = tiles[y * W + x];
            }
        }
    }

    bool tool(EditingTool t, int x, int y) { return sim.doTool(t, static_cast<short>(x), static_cast<short>(y)) == TOOLRESULT_OK; }

    // A straight road. Land tiles always; water only when bridge is set
    // (then the run must come from land).
    void road_line(int x0, int y0, int x1, int y1, bool bridge = false)
    {
        const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
        int x = x0, y = y0;
        bool coming_from_road = false;
        while (true) {
            if (Micropolis::testBounds(x, y)) {
                const MapTile t = tile(x, y);
                bool ok = false;
                if (water(x, y)) {
                    if (bridge && coming_from_road) {
                        ok = tool(TOOL_ROAD, x, y);
                    }
                } else if (t == DIRT || rail(x, y) || road(x, y)) {
                    ok = road(x, y) || tool(TOOL_ROAD, x, y);
                }
                coming_from_road = ok;
                placed_roads += ok ? 1 : 0;
            }
            if (x == x1 && y == y1) {
                break;
            }
            x += dx;
            y += dy;
        }
    }

    void rail_line(int x0, int y0, int x1, int y1)
    {
        const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
        int x = x0, y = y0;
        while (true) {
            if (Micropolis::testBounds(x, y) && !rail(x, y)) {
                tool(TOOL_RAILROAD, x, y);
            }
            if (x == x1 && y == y1) {
                break;
            }
            x += dx;
            y += dy;
        }
    }

    // Power line across water, from the last land tile on one side to the
    // first on the other.
    void wire_across(int x0, int y0, int x1, int y1)
    {
        const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
        std::vector<std::pair<int, int>> run;
        int x = x0, y = y0;
        while (true) {
            run.emplace_back(x, y);
            if (x == x1 && y == y1) {
                break;
            }
            x += dx;
            y += dy;
        }
        int first = -1, last = -1;
        for (int i = 0; i < static_cast<int>(run.size()); ++i) {
            if (water(run[i].first, run[i].second)) {
                if (first < 0) {
                    first = i;
                }
                last = i;
            }
        }
        if (first <= 0 || last + 1 >= static_cast<int>(run.size())) {
            return;
        }
        for (int i = first - 1; i <= last + 1; ++i) {
            tool(TOOL_WIRE, run[i].first, run[i].second);
        }
    }

    bool clear_land(int x0, int y0, int w, int h) const
    {
        for (int x = x0; x < x0 + w; ++x) {
            for (int y = y0; y < y0 + h; ++y) {
                if (!dirt(x, y)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool touches_road(int x0, int y0, int w, int h) const
    {
        for (int x = x0 - 1; x <= x0 + w; ++x) {
            for (int y = y0 - 1; y <= y0 + h; ++y) {
                if ((x == x0 - 1 || x == x0 + w || y == y0 - 1 || y == y0 + h) && road(x, y)) {
                    return true;
                }
            }
        }
        return false;
    }

    // A 3x3 or 4x4 building with its top-left at (x0, y0). The engine's
    // tool position is the centre (3x3) or one in from the top-left (4x4).
    bool building(EditingTool t, int size, int x0, int y0)
    {
        if (!clear_land(x0, y0, size, size)) {
            return false;
        }
        return tool(t, x0 + 1, y0 + 1);
    }

    // The building on the clear spot next to a road nearest (tx, ty).
    bool building_around(EditingTool t, int size, int tx, int ty)
    {
        for (int r = 0; r < 14; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) {
                        continue;
                    }
                    const int x0 = tx + dx, y0 = ty + dy;
                    if (clear_land(x0, y0, size, size) && touches_road(x0, y0, size, size) && building(t, size, x0, y0)) {
                        return true;
                    }
                }
            }
        }
        std::fprintf(stderr, "seattle_map: no room for tool %d around %d,%d\n", static_cast<int>(t), tx, ty);
        return false;
    }

    void park_rect(int x0, int y0, int x1, int y1)
    {
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                if (dirt(x, y)) {
                    tool(TOOL_PARK, x, y);
                }
            }
        }
    }

    void layout();
    void zone_fill();
    void power_crossings();
    void finish_leftovers();
    void connect_power();
    void seed_buildings();
    int green_empty_lots(bool mark_only);
    std::vector<char> empty_since = std::vector<char>(W * H, 0);
};

// Zone kind for a 3x3 lot centred on (x, y).
EditingTool zone_kind(int x, int y)
{
    // Chinatown-International District, south of downtown.
    if (in_rect(x, y, 64, 73, 71, 79)) {
        return TOOL_COMMERCIAL;
    }
    // Industry: SoDo and Georgetown, Harbor Island, Interbay and Smith
    // Cove, the Ballard and Fremont canal front, the West Seattle side of
    // the Duwamish, the west slope of Beacon Hill and the Rainier Valley
    // rail corridor.
    if (in_rect(x, y, 57, 79, 71, 100) || in_rect(x, y, 47, 83, 57, 96) || in_rect(x, y, 29, 25, 37, 53) ||
        in_rect(x, y, 13, 16, 47, 23) || in_rect(x, y, 40, 22, 62, 34) || in_rect(x, y, 28, 82, 46, 100) ||
        in_rect(x, y, 73, 86, 80, 100) || in_rect(x, y, 89, 78, 98, 89) || in_rect(x, y, 14, 46, 28, 53)) {
        return TOOL_INDUSTRIAL;
    }
    // Commerce: the downtown core (with some homes mixed in), Pike/Pine,
    // the U District's Ave, Ballard's Market Street, Fremont, Wallingford's
    // 45th, Queen Anne's top, Columbia City, the West Seattle Junction and
    // Northgate.
    if (in_rect(x, y, 52, 57, 71, 79)) {
        return ((x / 3) + (y / 4)) % 3 == 2 ? TOOL_RESIDENTIAL : TOOL_COMMERCIAL;
    }
    if (in_rect(x, y, 72, 61, 79, 64) || in_rect(x, y, 73, 12, 79, 29) || in_rect(x, y, 19, 13, 36, 16) ||
        in_rect(x, y, 44, 24, 54, 29) || in_rect(x, y, 56, 13, 71, 16) || in_rect(x, y, 40, 36, 48, 40) ||
        in_rect(x, y, 88, 91, 96, 96) || in_rect(x, y, 20, 91, 30, 96) || in_rect(x, y, 66, 0, 77, 4)) {
        return TOOL_COMMERCIAL;
    }
    // Every neighbourhood has its shops, close in more of them: one lot in
    // three within about 4 km of Pioneer Square, one in five beyond.
    const int d = std::abs(x - 62) + std::abs(y - 68);
    const int every = d < 34 ? 3 : 5;
    if (((x / 3) + 2 * (y / 4)) % every == 0) {
        return TOOL_COMMERCIAL;
    }
    return TOOL_RESIDENTIAL;
}

void Gen::layout()
{
    // Rail first so streets cross it. BNSF: SoDo up to King Street
    // Station, and the Interbay line north through Ballard. Link light
    // rail down Martin Luther King Jr Way in the Rainier Valley.
    rail_line(62, 99, 62, 76);
    rail_line(34, 53, 34, 0);
    rail_line(93, 99, 93, 78);

    // The big buildings go down before the streets so the 4x4 lots stay
    // whole: the two SoDo stadiums and Husky Stadium by Union Bay;
    // seaports on Harbor Island and at Smith Cove (Terminal 91); coal
    // plants in SoDo (Georgetown), Interbay, Ballard, Harbor Island and on
    // the West Seattle side of the Duwamish.
    const struct {
        EditingTool tool;
        int x, y;
    } big[] = {
        {TOOL_STADIUM, 64, 78}, {TOOL_STADIUM, 64, 83}, {TOOL_STADIUM, 81, 29},
        {TOOL_SEAPORT, 48, 84}, {TOOL_SEAPORT, 30, 47},
        {TOOL_COALPOWER, 30, 32}, {TOOL_COALPOWER, 17, 15},
        {TOOL_COALPOWER, 52, 91}, {TOOL_COALPOWER, 40, 93}, {TOOL_COALPOWER, 30, 38},
        {TOOL_COALPOWER, 38, 17}, {TOOL_COALPOWER, 66, 89},
    };
    // Boeing Field (really just south of the map) at the foot of SoDo.
    if (!clear_land(64, 94, 6, 6) || !tool(TOOL_AIRPORT, 65, 95)) {
        std::fprintf(stderr, "seattle_map: could not place the airport\n");
    }
    for (const auto &b : big) {
        if (!building(b.tool, 4, b.x, b.y)) {
            std::fprintf(stderr, "seattle_map: could not place tool %d at %d,%d\n", static_cast<int>(b.tool), b.x, b.y);
        }
    }

    // Spines, laid from land so their bridges connect. I-5 crosses
    // Portage Bay on the Ship Canal Bridge; Aurora crosses on the Aurora
    // Bridge; the Fremont, Ballard and Montlake bridges cross the cut.
    road_line(72, 0, 72, 99, true);              // I-5
    road_line(55, 0, 55, 62, true);              // Aurora Ave / SR 99
    road_line(50, 0, 50, 40, true);              // Fremont Ave and the Fremont Bridge
    road_line(27, 0, 27, 34, true);              // 15th Ave NW and the Ballard Bridge
    road_line(80, 0, 80, 41, true);              // 25th Ave NE and the Montlake Bridge
    road_line(72, 85, 119, 85, true);            // I-90 and its floating bridge
    road_line(72, 41, 119, 41, true);            // SR 520 and its floating bridge
    road_line(72, 89, 30, 89, true);             // West Seattle Bridge over Harbor Island

    // East-west streets. Downtown, Belltown, South Lake Union, Capitol
    // Hill, First Hill and the International District have a street every
    // fourth row (single-lot blocks); everywhere else every seventh row,
    // two lots deep.
    auto central = [](int x, int y) { return x >= 44 && x <= 88 && y >= 53 && y <= 81; };
    for (int y = 0; y < H; ++y) {
        int start = -1;
        for (int x = 0; x <= W; ++x) {
            const bool want = x < W && (central(x, y) ? (y % 4 == 1) : (y % 7 == 1 && (y < 53 || y > 81)));
            if (want && start < 0) {
                start = x;
            } else if (!want && start >= 0) {
                road_line(start, y, x - 1, y, false);
                start = -1;
            }
        }
    }

    // North-south streets: every fourth column in the centre, every ninth
    // elsewhere (two lots and a strip of green between avenues), kept
    // clear of the spines, Interbay's two avenues and the rail lines.
    struct Spine {
        int x, y0, y1;
    };
    const Spine spines[] = {
        {27, 0, 34}, {50, 0, 40}, {55, 0, 62}, {72, 0, 99}, {80, 0, 41}, {29, 25, 53}, {37, 25, 53},
        {34, 0, 53}, {62, 76, 99}, {93, 78, 99},
    };
    road_line(29, 25, 29, 53, false); // Interbay: 20th Ave W
    road_line(37, 25, 37, 53, false); // Interbay: 15th Ave W / Elliott Ave
    for (int x = 1; x < W - 1; ++x) {
        int start = -1;
        for (int y = 0; y <= H; ++y) {
            bool want = false;
            if (y < H) {
                if (central(x, y)) {
                    want = x % 7 == 2;
                } else if (x % 9 == 0) {
                    want = true;
                    for (const Spine &sp : spines) {
                        if (y >= sp.y0 && y <= sp.y1 && std::abs(sp.x - x) <= 2 && sp.x != x) {
                            want = false;
                        }
                    }
                }
            }
            if (want && start < 0) {
                start = y;
            } else if (!want && start >= 0) {
                road_line(x, start, x, y - 1, false);
                start = -1;
            }
        }
    }

    // Power across the water (underwater lines) so one grid serves the
    // whole city: the Ship Canal at Fremont and Ballard, Portage Bay, the
    // two Duwamish waterways.
    wire_across(53, 26, 53, 36);
    wire_across(23, 18, 23, 28);
    wire_across(74, 30, 74, 40);
    wire_across(61, 87, 54, 87);
    wire_across(50, 87, 42, 87);

    // Parks: Cal Anderson (two blocks on Capitol Hill), Volunteer Park,
    // Seattle Center, Gas Works, Jefferson Park, Kerry Park, the Green Lake
    // shore, Myrtle Edwards on the waterfront, Pioneer Square's plaza.
    park_rect(73, 58, 78, 60);
    park_rect(77, 46, 83, 52);
    park_rect(52, 56, 54, 58);
    park_rect(63, 31, 68, 33);
    park_rect(81, 90, 87, 96);
    park_rect(45, 42, 47, 43);
    park_rect(56, 2, 69, 13);
    park_rect(57, 81, 58, 83);
    park_rect(76, 21, 78, 22);

    // Police: Downtown (West Precinct), North, Ballard, Queen Anne,
    // Magnolia, the U District, SoDo, Beacon Hill, the Rainier Valley,
    // West Seattle. None on Capitol Hill: the East Precinct was boarded up.
    building_around(TOOL_POLICESTATION, 3, 60, 70);
    building_around(TOOL_POLICESTATION, 3, 80, 6);
    building_around(TOOL_POLICESTATION, 3, 28, 9);
    building_around(TOOL_POLICESTATION, 3, 45, 46);
    building_around(TOOL_POLICESTATION, 3, 17, 42);
    building_around(TOOL_POLICESTATION, 3, 88, 20);
    building_around(TOOL_POLICESTATION, 3, 65, 90);
    building_around(TOOL_POLICESTATION, 3, 79, 94);
    building_around(TOOL_POLICESTATION, 3, 93, 82);
    building_around(TOOL_POLICESTATION, 3, 26, 94);
    // Fire stations.
    building_around(TOOL_FIRESTATION, 3, 57, 64);
    building_around(TOOL_FIRESTATION, 3, 42, 6);
    building_around(TOOL_FIRESTATION, 3, 66, 18);
    building_around(TOOL_FIRESTATION, 3, 89, 10);
    building_around(TOOL_FIRESTATION, 3, 22, 38);
    building_around(TOOL_FIRESTATION, 3, 47, 50);
    building_around(TOOL_FIRESTATION, 3, 81, 62);
    building_around(TOOL_FIRESTATION, 3, 92, 68);
    building_around(TOOL_FIRESTATION, 3, 69, 92);
    building_around(TOOL_FIRESTATION, 3, 84, 86);
    building_around(TOOL_FIRESTATION, 3, 32, 90);
    building_around(TOOL_FIRESTATION, 3, 77, 28);
    building_around(TOOL_FIRESTATION, 3, 28, 22);

}

void Gen::zone_fill()
{
    for (int y = 0; y + 3 <= H; ++y) {
        for (int x = 0; x + 3 <= W; ++x) {
            if (!clear_land(x, y, 3, 3) || !touches_road(x, y, 3, 3)) {
                continue;
            }
            if (building(zone_kind(x + 1, y + 1), 3, x, y)) {
                ++placed_zones;
            }
        }
    }
}

// Connect the blocks on either side of a street with a power line over it
// on every third straight street tile that has buildings on both sides.
void Gen::power_crossings()
{
    auto building_tile = [&](int x, int y) {
        if (!Micropolis::testBounds(x, y)) {
            return false;
        }
        return (sim.map[x][y] & CONDBIT) != 0 && !road(x, y);
    };
    for (int x = 1; x < W - 1; ++x) {
        for (int y = 1; y < H - 1; ++y) {
            const MapTile t = tile(x, y);
            if (t != ROADS && t != ROADS2) {
                continue;
            }
            const bool horizontal = t == ROADS;
            const bool across = horizontal ? (building_tile(x, y - 1) && building_tile(x, y + 1))
                                           : (building_tile(x - 1, y) && building_tile(x + 1, y));
            if (!across) {
                continue;
            }
            if ((horizontal ? x : y) % 3 != 0) {
                continue;
            }
            tool(TOOL_WIRE, x, y);
        }
    }
}

// A head start: zoned lots start built up, densest near Pioneer Square and
// thinning out with distance, the way the real city is. The decades of
// simulation that follow keep, grow or empty each lot on its own merits.
void Gen::seed_buildings()
{
    auto plop = [&](int cx, int cy, int base) {
        int i = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                sim.map[cx + dx][cy + dy] = static_cast<MapValue>((base + i) | BNCNBIT);
                ++i;
            }
        }
        sim.map[cx][cy] |= ZONEBIT | BULLBIT;
    };
    for (int x = 1; x < W - 1; ++x) {
        for (int y = 1; y < H - 1; ++y) {
            if (!(sim.map[x][y] & ZONEBIT)) {
                continue;
            }
            const MapTile t = tile(x, y);
            const int d = std::abs(x - 62) + std::abs(y - 68);
            if (t == FREEZ) {
                const int den = d < 22 ? 3 : d < 36 ? 2 : d < 52 ? 1 : 0;
                const int value = d < 22 ? 3 : d < 40 ? 2 : 1;
                plop(x, y, (value * 4 + den) * 9 + RZB - 4);
            } else if (t == COMCLR) {
                const int den = d < 12 ? 4 : d < 26 ? 3 : d < 42 ? 2 : 1;
                const int value = d < 18 ? 3 : d < 36 ? 2 : 1;
                plop(x, y, (value * 5 + den) * 9 + CZB - 4);
            } else if (t == INDCLR) {
                plop(x, y, (1 * 4 + 2) * 9 + IND1);
            }
        }
    }
}

// Lots that stayed empty for decades out in the neighbourhoods become
// pocket parks (Seattle's many small parks and greenbelts).
int Gen::green_empty_lots(bool mark_only)
{
    int changed = 0;
    for (int x = 1; x < W - 1; ++x) {
        for (int y = 1; y < H - 1; ++y) {
            if (!(sim.map[x][y] & ZONEBIT)) {
                continue;
            }
            const MapTile t = tile(x, y);
            if (t != FREEZ) {
                continue;
            }
            const int d = std::abs(x - 62) + std::abs(y - 68);
            if (d < 40) {
                continue;
            }
            int houses = 0;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const MapTile u = tile(x + dx, y + dy);
                    houses += (u >= LHTHR && u <= HHTHR) ? 1 : 0;
                }
            }
            if (houses > 0) {
                empty_since[y * W + x] = 0;
                continue;
            }
            if (!mark_only && empty_since[y * W + x] >= 3) {
                // Clear the zone and its rubble down to open land.
                tool(TOOL_BULLDOZER, x, y);
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        sim.map[x + dx][y + dy] = DIRT;
                    }
                }
                ++changed;
            } else if (mark_only) {
                ++empty_since[y * W + x];
            }
        }
    }
    if (!mark_only) {
        // Lines around the new parks where a lot used to carry the power.
        connect_power();
        finish_leftovers();
    }
    return changed;
}

// Every block that the street crossings left without power gets a line to
// the grid: the shortest run over open lots and straight streets.
void Gen::connect_power()
{
    const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
    std::vector<char> given_up(W * H, 0);
    for (int round = 0; round < 400; ++round) {
        // What the plants reach now.
        std::vector<char> reached(W * H, 0);
        std::vector<std::pair<int, int>> stack;
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                const MapTile t = tile(x, y);
                if (t == POWERPLANT || t == NUCLEAR) {
                    reached[y * W + x] = 1;
                    stack.emplace_back(x, y);
                }
            }
        }
        while (!stack.empty()) {
            const auto at = stack.back();
            stack.pop_back();
            for (int k = 0; k < 4; ++k) {
                const int nx = at.first + ddx[k], ny = at.second + ddy[k];
                if (Micropolis::testBounds(nx, ny) && !reached[ny * W + nx] && (sim.map[nx][ny] & CONDBIT)) {
                    reached[ny * W + nx] = 1;
                    stack.emplace_back(nx, ny);
                }
            }
        }
        int zx = -1, zy = -1;
        for (int x = 0; x < W && zx < 0; ++x) {
            for (int y = 0; y < H; ++y) {
                if ((sim.map[x][y] & ZONEBIT) && !reached[y * W + x] && !given_up[y * W + x]) {
                    zx = x;
                    zy = y;
                    break;
                }
            }
        }
        if (zx < 0) {
            return;
        }
        // The unpowered group, then a search out from it.
        std::vector<int> parent(W * H, -2);
        std::vector<std::pair<int, int>> queue;
        std::vector<std::pair<int, int>> group;
        stack.emplace_back(zx, zy);
        parent[zy * W + zx] = -1;
        while (!stack.empty()) {
            const auto at = stack.back();
            stack.pop_back();
            group.push_back(at);
            queue.push_back(at);
            for (int k = 0; k < 4; ++k) {
                const int nx = at.first + ddx[k], ny = at.second + ddy[k];
                if (Micropolis::testBounds(nx, ny) && parent[ny * W + nx] == -2 && (sim.map[nx][ny] & CONDBIT)) {
                    parent[ny * W + nx] = -1;
                    stack.emplace_back(nx, ny);
                }
            }
        }
        int goal = -1;
        for (std::size_t head = 0; head < queue.size() && goal < 0; ++head) {
            const auto at = queue[head];
            for (int k = 0; k < 4; ++k) {
                const int nx = at.first + ddx[k], ny = at.second + ddy[k];
                if (!Micropolis::testBounds(nx, ny) || parent[ny * W + nx] != -2) {
                    continue;
                }
                if (reached[ny * W + nx]) {
                    goal = at.second * W + at.first;
                    break;
                }
                const MapTile t = tile(nx, ny);
                if (t == DIRT || t == ROADS || t == ROADS2 || (t >= WOODS_LOW && t <= WOODS_HIGH)) {
                    parent[ny * W + nx] = at.second * W + at.first;
                    queue.emplace_back(nx, ny);
                }
            }
        }
        if (goal < 0) {
            // No way to reach it: the lots go back to open land.
            for (const auto &g : group) {
                given_up[g.second * W + g.first] = 1;
            }
            for (const auto &g : group) {
                const MapTile t = tile(g.first, g.second);
                if (t != POWERPLANT && t != NUCLEAR) {
                    sim.map[g.first][g.second] = DIRT;
                }
            }
            continue;
        }
        for (int at = goal; at >= 0 && parent[at] >= 0; at = parent[at]) {
            const MapTile t = tile(at % W, at / W);
            if (t >= WOODS_LOW && t <= WOODS_HIGH) {
                tool(TOOL_BULLDOZER, at % W, at / W);
            }
            tool(TOOL_WIRE, at % W, at / W);
        }
    }
}

// Lots too small for a zone become parks beside streets and buildings,
// woods elsewhere.
void Gen::finish_leftovers()
{
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            if (!dirt(x, y)) {
                continue;
            }
            tool(TOOL_PARK, x, y);
        }
    }
}

void write_preview(const Micropolis &sim, const char *path)
{
    std::ofstream out(path, std::ios::binary);
    const int scale = 4;
    out << "P6\n" << W * scale << " " << H * scale << "\n255\n";
    for (int y = 0; y < H; ++y) {
        std::string row;
        for (int x = 0; x < W; ++x) {
            const MapTile t = sim.map[x][y] & LOMASK;
            unsigned char r = 180, g = 150, b = 100;
            if (t >= WATER_LOW && t <= WATER_HIGH) { r = 40; g = 80; b = 190; }
            else if (t >= TREEBASE && t <= LASTTREE) { r = 30; g = 120; b = 40; }
            else if (t >= FIREBASE && t < ROADBASE) { r = 200; g = 60; b = 0; }
            else if (t >= ROADBASE && t <= LASTROAD) { r = 60; g = 60; b = 60; }
            else if (t >= POWERBASE && t <= LASTPOWER) { r = 220; g = 220; b = 0; }
            else if (t >= RAILBASE && t <= LASTRAIL) { r = 120; g = 70; b = 40; }
            else if (t >= RESBASE && t < COMBASE) { r = 90; g = 200; b = 90; }
            else if (t >= COMBASE && t < INDBASE) { r = 90; g = 120; b = 230; }
            else if (t >= INDBASE && t < PORTBASE) { r = 230; g = 200; b = 60; }
            else if (t >= PORTBASE) { r = 200; g = 200; b = 200; }
            for (int i = 0; i < scale; ++i) {
                row.push_back(static_cast<char>(r));
                row.push_back(static_cast<char>(g));
                row.push_back(static_cast<char>(b));
            }
        }
        for (int i = 0; i < scale; ++i) {
            out << row;
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: seattle_map <output.cty> [preview.ppm]\n");
        return 2;
    }
    // Thirty years of growth at the scenario's own 7% tax, so the city it
    // hands over is settled rather than still filling in or emptying out.
    const int years = 30;

    static Gen gen;
    Micropolis &sim = gen.sim;
    sim.generateSomeCity(2020);
    // generateSomeCity() reseeds from the clock when it resets the city;
    // generating the throwaway terrain again seeds the engine's random
    // numbers with a fixed value, so every run builds the same city.
    sim.generateMap(2020);
    sim.setEnableDisasters(false);
    sim.setGameLevel(LEVEL_EASY);
    sim.setFunds(50000000);
    gen.paint_terrain();
    gen.layout();
    gen.zone_fill();
    gen.seed_buildings();
    gen.power_crossings();
    gen.connect_power();
    gen.finish_leftovers();
    std::fprintf(stderr, "seattle_map: %d street tiles, %d zones\n", gen.placed_roads, gen.placed_zones);

    sim.setCityTax(7);
    sim.setAutoBudget(true);
    sim.setSpeed(3);
    sim.setPasses(1);
    sim.cityTime = (1990 - 1900) * 48;
    const long end = sim.cityTime + years * 48L;
    long last_year = -1;
    bool greened = false;
    int marked = 0;
    while (sim.cityTime < end) {
        sim.simTick();
        if (sim.totalFunds < 1000000) {
            sim.setFunds(5000000);
        }
        // Lots still empty at three looks, two years apart, go green six
        // years before the end, so the city settles around the new parks.
        if (marked < 3 && sim.cityTime >= end - (12 - 2 * marked) * 48) {
            ++marked;
            gen.green_empty_lots(true);
        }
        if (!greened && sim.cityTime >= end - 6 * 48) {
            greened = true;
            std::fprintf(stderr, "seattle_map: %d empty lots became parks\n", gen.green_empty_lots(false));
        }
        const long year = sim.cityTime / 48;
        if (year != last_year && year % 5 == 0) {
            last_year = year;
            std::fprintf(stderr, "seattle_map: %ld, crime %d, pollution %d, score %d\n", year + 1900,
                         static_cast<int>(sim.crimeAverage), static_cast<int>(sim.pollutionAverage),
                         static_cast<int>(sim.cityScore));
        }
    }

    if (argc > 2 && argv[2][0] != '\0') {
        write_preview(sim, argv[2]);
    }
    const std::string out = argv[1];
    if (!sim.saveFile(out.c_str(), true)) {
        std::fprintf(stderr, "seattle_map: could not write %s\n", out.c_str());
        return 1;
    }
    // Keep the classic part only: the scenario loader reads 27,120 bytes.
    std::ifstream in(out, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (bytes.size() < 27120) {
        std::fprintf(stderr, "seattle_map: short save\n");
        return 1;
    }
    bytes.resize(27120);
    std::ofstream trimmed(out, std::ios::binary | std::ios::trunc);
    trimmed.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return trimmed.good() ? 0 : 1;
}

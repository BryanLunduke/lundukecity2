// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Shared pieces of the scenario map generators (tools/*_map.cpp): the
// geometry used to draw a coastline, and a Builder that lays a city out
// with the engine's own tools, grows it, and writes the classic
// 27,120-byte city file the scenario loader reads. Each generator supplies
// its own outline (terrain_at), its zoning (zone_kind) and its layout.
// The code follows tools/seattle_map.cpp, which predates this header.

#pragma once

#include "micropolis.h"
#include "special_buildings.hpp"
#include "terrain_paint.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace mapgen {

constexpr int W = WORLD_W;
constexpr int H = WORLD_H;

using Point = std::pair<double, double>;

// ---- Geometry (tile coordinates, x east, y south) -----------------------

inline bool inside(const std::vector<Point> &poly, double x, double y)
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

inline double segment_distance(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double len = dx * dx + dy * dy;
    double t = len > 0 ? ((px - ax) * dx + (py - ay) * dy) / len : 0;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = ax + t * dx - px, cy = ay + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

inline bool in_rect(double x, double y, double x0, double y0, double x1, double y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

inline bool in_ellipse(double x, double y, double cx, double cy, double rx, double ry)
{
    const double dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0;
}

// True within width/2 of the polyline (a river or canal of that width).
struct LinePoint {
    double x, y, width;
};
inline bool in_line(const std::vector<LinePoint> &line, double x, double y)
{
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const LinePoint &a = line[i];
        const LinePoint &b = line[i + 1];
        if (segment_distance(x, y, a.x, a.y, b.x, b.y) <= (a.width + b.width) / 4) {
            return true;
        }
    }
    return false;
}

// ---- Builder -------------------------------------------------------------

struct Builder {
    // Log prefix ("sf_map").
    const char *name = "map";
    // 'W' water, 'T' woods, 'L' land for each tile.
    std::function<char(int, int)> terrain_at;
    // The zone for a 3x3 lot centred on (x, y).
    std::function<EditingTool(int, int)> zone_kind;
    // The old core: seeded lots are densest here, and lots that stay empty
    // at least green_min tiles (Manhattan) away become parks.
    int core_x = 60;
    int core_y = 50;
    int green_min = 40;
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

    // A diagonal street as a staircase of single steps (roads only join
    // north, south, east and west), from (x0, y0) to (x1, y1).
    void road_stairs(int x0, int y0, int x1, int y1)
    {
        const int steps_x = std::abs(x1 - x0), steps_y = std::abs(y1 - y0);
        const int sx = (x1 > x0) - (x1 < x0), sy = (y1 > y0) - (y1 < y0);
        int x = x0, y = y0, done_x = 0, done_y = 0;
        road_line(x, y, x, y, false);
        while (done_x < steps_x || done_y < steps_y) {
            // Step along whichever axis is further behind the straight line.
            const bool step_x = done_y >= steps_y ||
                                (done_x < steps_x && (done_x + 1) * steps_y <= (done_y + 1) * steps_x);
            if (step_x) {
                x += sx;
                ++done_x;
            } else {
                y += sy;
                ++done_y;
            }
            road_line(x, y, x, y, false);
        }
    }

    // Street grid: an east-west street wherever row_wanted(x, y), a
    // north-south one wherever col_wanted(x, y), each laid as runs.
    void grid(const std::function<bool(int, int)> &row_wanted, const std::function<bool(int, int)> &col_wanted)
    {
        for (int y = 0; y < H; ++y) {
            int start = -1;
            for (int x = 0; x <= W; ++x) {
                const bool want = x < W && row_wanted(x, y);
                if (want && start < 0) {
                    start = x;
                } else if (!want && start >= 0) {
                    road_line(start, y, x - 1, y, false);
                    start = -1;
                }
            }
        }
        for (int x = 0; x < W; ++x) {
            int start = -1;
            for (int y = 0; y <= H; ++y) {
                const bool want = y < H && col_wanted(x, y);
                if (want && start < 0) {
                    start = y;
                } else if (!want && start >= 0) {
                    road_line(x, start, x, y - 1, false);
                    start = -1;
                }
            }
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
        std::fprintf(stderr, "%s: no room for tool %d around %d,%d\n", name, static_cast<int>(t), tx, ty);
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

    std::vector<char> empty_since = std::vector<char>(W * H, 0);

    void zone_fill()
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
    void power_crossings()
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

    // Lots that stayed empty for decades out in the neighbourhoods become
    // pocket parks.
    int green_empty_lots(bool mark_only)
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
                const int d = std::abs(x - core_x) + std::abs(y - core_y);
                if (d < green_min) {
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
    void connect_power()
    {
        const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
        auto passable = [&](int x, int y) {
            const MapTile t = tile(x, y);
            return t == DIRT || t == ROADS || t == ROADS2 || t == HRAIL || t == VRAIL ||
                   (t >= WOODS_LOW && t <= WOODS_HIGH);
        };
        for (int round = 0; round < 1000; ++round) {
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
            std::vector<std::pair<int, int>> queue;
            while (!stack.empty()) {
                const auto at = stack.back();
                stack.pop_back();
                queue.push_back(at);
                for (int k = 0; k < 4; ++k) {
                    const int nx = at.first + ddx[k], ny = at.second + ddy[k];
                    if (Micropolis::testBounds(nx, ny) && !reached[ny * W + nx] && (sim.map[nx][ny] & CONDBIT)) {
                        reached[ny * W + nx] = 1;
                        stack.emplace_back(nx, ny);
                    }
                }
            }
            // Out from the grid over open lots, straight streets and rail to
            // the closest unpowered building.
            std::vector<int> parent(W * H, -2);
            for (const auto &at : queue) {
                parent[at.second * W + at.first] = -1;
            }
            int goal = -1;
            for (std::size_t head = 0; head < queue.size() && goal < 0; ++head) {
                const auto at = queue[head];
                for (int k = 0; k < 4; ++k) {
                    const int nx = at.first + ddx[k], ny = at.second + ddy[k];
                    if (!Micropolis::testBounds(nx, ny) || parent[ny * W + nx] != -2) {
                        continue;
                    }
                    if ((sim.map[nx][ny] & CONDBIT) && !reached[ny * W + nx]) {
                        goal = at.second * W + at.first;
                        break;
                    }
                    if (passable(nx, ny)) {
                        parent[ny * W + nx] = at.second * W + at.first;
                        queue.emplace_back(nx, ny);
                    }
                }
            }
            if (goal < 0) {
                // Nothing more can be reached: those lots go back to open land.
                for (int x = 0; x < W; ++x) {
                    for (int y = 0; y < H; ++y) {
                        if ((sim.map[x][y] & ZONEBIT) && !reached[y * W + x]) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                for (int dy = -1; dy <= 1; ++dy) {
                                    if (Micropolis::testBounds(x + dx, y + dy)) {
                                        sim.map[x + dx][y + dy] = DIRT;
                                    }
                                }
                            }
                        }
                    }
                }
                return;
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
    void finish_leftovers()
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


    // A head start: zoned lots start built up, densest around the core and
    // thinning out with distance, the way a real city is. The decades of
    // simulation that follow keep, grow or empty each lot on its own merits.
    void seed_buildings()
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
                const int d = std::abs(x - core_x) + std::abs(y - core_y);
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

};

inline void write_preview(const Micropolis &sim, const char *path)
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

// A City 2 landmark (Prison, Zoo, ...) with its top-left at (left, top),
// put down once the city has grown: the engine run that grows the city
// knows nothing of landmarks. Whatever stands on the footprint is cleared.
inline bool place_landmark(Builder &b, int center_tile, int left, int top)
{
    const int index = special_index_for_tile(center_tile);
    if (index < 0) {
        return false;
    }
    const SpecialBuilding &row = special_building(index);
    for (int x = left; x < left + row.width; ++x) {
        for (int y = top; y < top + row.height; ++y) {
            if (Micropolis::testBounds(x, y)) {
                b.sim.map[x][y] = DIRT;
            }
        }
    }
    std::string error;
    if (!place_special_building(&b.sim, index, left + row.anchor_x, top + row.anchor_y, error)) {
        std::fprintf(stderr, "%s: could not place %s at %d,%d: %s\n", b.name, row.name, left, top, error.c_str());
        return false;
    }
    return true;
}

struct RunOptions {
    // Seeds the engine's random numbers (every run builds the same city).
    int seed = 2020;
    // The growth run: from first_year, for years years, at tax percent.
    int first_year = 1990;
    int years = 30;
    int tax = 7;
    // Called once the city has grown, before it is written.
    std::function<void(Builder &)> after_growth;
};

// usage: <name> <output.cty> [preview.ppm]
inline int build_and_write(Builder &b, int argc, char **argv, const std::function<void(Builder &)> &layout,
                           const RunOptions &options)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output.cty> [preview.ppm]\n", b.name);
        return 2;
    }
    Micropolis &sim = b.sim;
    sim.generateSomeCity(options.seed);
    // generateSomeCity() reseeds from the clock when it resets the city;
    // generating the throwaway terrain again seeds the engine's random
    // numbers with a fixed value.
    sim.generateMap(options.seed);
    sim.setEnableDisasters(false);
    sim.setGameLevel(LEVEL_EASY);
    sim.setFunds(50000000);
    b.paint_terrain();
    layout(b);
    b.zone_fill();
    b.seed_buildings();
    b.power_crossings();
    b.connect_power();
    b.finish_leftovers();
    std::fprintf(stderr, "%s: %d street tiles, %d zones\n", b.name, b.placed_roads, b.placed_zones);

    sim.setCityTax(options.tax);
    sim.setAutoBudget(true);
    sim.setSpeed(3);
    sim.setPasses(1);
    sim.cityTime = (options.first_year - 1900) * 48L;
    const long end = sim.cityTime + options.years * 48L;
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
            b.green_empty_lots(true);
        }
        if (!greened && sim.cityTime >= end - 6 * 48) {
            greened = true;
            std::fprintf(stderr, "%s: %d empty lots became parks\n", b.name, b.green_empty_lots(false));
        }
        const long year = sim.cityTime / 48;
        if (year != last_year && year % 5 == 0) {
            last_year = year;
            std::fprintf(stderr, "%s: %ld, pop %ld, crime %d, pollution %d, score %d\n", b.name, year + 1900,
                         static_cast<long>(sim.cityPop), static_cast<int>(sim.crimeAverage),
                         static_cast<int>(sim.pollutionAverage), static_cast<int>(sim.cityScore));
        }
    }
    if (options.after_growth) {
        options.after_growth(b);
    }

    if (argc > 2 && argv[2][0] != '\0') {
        write_preview(sim, argv[2]);
    }
    const std::string out = argv[1];
    if (!sim.saveFile(out.c_str(), true)) {
        std::fprintf(stderr, "%s: could not write %s\n", b.name, out.c_str());
        return 1;
    }
    // Keep the classic part only: the scenario loader reads 27,120 bytes.
    std::ifstream in(out, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (bytes.size() < 27120) {
        std::fprintf(stderr, "%s: short save\n", b.name);
        return 1;
    }
    bytes.resize(27120);
    std::ofstream trimmed(out, std::ios::binary | std::ios::trunc);
    trimmed.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return trimmed.good() ? 0 : 1;
}

} // namespace mapgen

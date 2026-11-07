// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <functional>
#include <string>

// What a committed placement would do at one tile, without touching the map.
// Costs follow the engine tables (road $10, bridge $50, and the same for the
// other tools). The corner mark is a shape as well as a colour.

enum class PlaceMark {
    Ready = 0,
    Caution,
    Blocked,
    Short
};

struct PlaceForecast {
    PlaceMark mark = PlaceMark::Blocked;
    int cost = 0;
    std::string tag;
    int left = 0;
    int top = 0;
    int width = 1;
    int height = 1;
};

struct StrokeForecast {
    PlaceForecast here;
    int tiles = 0;
    int cost = 0;
    std::string summary;
};

// map_at returns the raw map word. Coordinates outside the world are not read.
using PlaceMap = std::function<int(int x, int y)>;

PlaceForecast forecast_tile(int engine_tool, int tile_x, int tile_y, int special_index, long long funds,
                            bool auto_bulldoze, const PlaceMap &map_at);

// The line a drag would commit, in the engine's step order, spending as it goes.
StrokeForecast forecast_stroke(int engine_tool, int from_x, int from_y, int to_x, int to_y, int special_index,
                               long long funds, bool auto_bulldoze, const PlaceMap &map_at);

// Short label for a failure the engine already reported.
std::string short_build_reason(const std::string &message);

// True when the map word is the kind of tile this tool leaves behind.
bool tile_matches_tool(int engine_tool, int word);

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "placement_hint.hpp"

#include "micropolis.h"
#include "special_buildings.hpp"

#include <cstring>
#include <unordered_map>

namespace {

constexpr int kWorldW = 120;
constexpr int kWorldH = 100;

int tile_of(int word)
{
    return word & LOMASK;
}

int neutralize_road(int tile)
{
    tile &= LOMASK;
    if (tile >= 64 && tile <= 207) {
        tile = (tile & 0x000F) + 64;
    }
    return tile;
}

bool tally_tile(int tile)
{
    return (tile >= FIRSTRIVEDGE && tile <= LASTRUBBLE) || (tile >= POWERBASE + 2 && tile <= POWERBASE + 12) ||
           (tile >= TINYEXP && tile <= LASTTINYEXP + 2);
}

bool auto_clearable_word(int raw)
{
    return special_auto_clearable(raw);
}

bool water_tile(int tile)
{
    return tile >= RIVER && tile <= WATER_HIGH;
}

std::string money(int amount)
{
    const bool neg = amount < 0;
    int n = neg ? -amount : amount;
    std::string digits = std::to_string(n);
    std::string grouped;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            grouped.push_back(',');
        }
        grouped.push_back(digits[i]);
    }
    return (neg ? "\u2212$" : "$") + grouped;
}

struct Scratch {
    PlaceMap base;
    std::unordered_map<int, int> over;

    int get(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
            return -1;
        }
        const int key = y * kWorldW + x;
        const auto found = over.find(key);
        if (found != over.end()) {
            return found->second;
        }
        return base ? base(x, y) : 0;
    }

    void set(int x, int y, int word)
    {
        if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
            return;
        }
        over[y * kWorldW + x] = word;
    }
};

enum class Outcome { Ok, Caution, Blocked, Bulldoze };

struct Attempt {
    Outcome outcome = Outcome::Blocked;
    int cost = 0;
    const char *reason = "Cannot build here";
    int left = 0;
    int top = 0;
    int width = 1;
    int height = 1;
};

Attempt blocked_at(const char *reason, int x, int y, int w = 1, int h = 1)
{
    Attempt attempt;
    attempt.outcome = Outcome::Blocked;
    attempt.reason = reason;
    attempt.left = x;
    attempt.top = y;
    attempt.width = w;
    attempt.height = h;
    return attempt;
}

bool road_anchor_h(int tile)
{
    tile = neutralize_road(tile);
    return tile == VRAILROAD || tile == HBRIDGE || (tile >= ROADS && tile <= HROADPOWER);
}

bool road_anchor_h_left(int tile)
{
    tile = neutralize_road(tile);
    return tile == VRAILROAD || tile == HBRIDGE || (tile >= ROADS && tile <= INTERSECTION);
}

bool road_anchor_v(int tile)
{
    tile = neutralize_road(tile);
    return tile == HRAILROAD || tile == VROADPOWER || (tile >= VBRIDGE && tile <= INTERSECTION);
}

bool rail_anchor_east(int tile)
{
    tile = neutralize_road(tile);
    return tile == RAILHPOWERV || tile == HRAIL || (tile >= LHRAIL && tile <= HRAILROAD);
}

bool rail_anchor_west(int tile)
{
    tile = neutralize_road(tile);
    return tile == RAILHPOWERV || tile == HRAIL || (tile > VRAIL && tile < VRAILROAD);
}

bool rail_anchor_south(int tile)
{
    tile = neutralize_road(tile);
    return tile == RAILVPOWERH || tile == VRAILROAD || (tile > HRAIL && tile < HRAILROAD);
}

bool rail_anchor_north(int tile)
{
    tile = neutralize_road(tile);
    return tile == RAILVPOWERH || tile == VRAILROAD || (tile > HRAIL && tile < HRAILROAD);
}

bool wire_land_h(int word)
{
    if ((word & CONDBIT) == 0) {
        return false;
    }
    int tile = neutralize_road(tile_of(word));
    return tile != HROADPOWER && tile != RAILHPOWERV && tile != HPOWER;
}

bool wire_land_v(int word)
{
    if ((word & CONDBIT) == 0) {
        return false;
    }
    int tile = neutralize_road(tile_of(word));
    return tile != VROADPOWER && tile != RAILVPOWERH && tile != VPOWER;
}

void prepare_line(Scratch &map, int x, int y, bool auto_bulldoze, int &extra)
{
    int word = map.get(x, y);
    if (word < 0 || !auto_bulldoze || (word & BULLBIT) == 0) {
        return;
    }
    const int tile = neutralize_road(tile_of(word));
    // Same exception as connectTile. Tiles 53-55 are civic centres, not waste.
    if (civicGapTile(tile)) {
        return;
    }
    if ((tile >= TINYEXP && tile <= LASTTINYEXP) || (tile < HBRIDGE && tile != DIRT)) {
        extra += 1;
        map.set(x, y, DIRT);
    }
}

Attempt lay_road(Scratch &map, int x, int y, bool auto_bulldoze)
{
    if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
        return blocked_at("Off the map", x, y);
    }
    int extra = 0;
    prepare_line(map, x, y, auto_bulldoze, extra);
    const int word = map.get(x, y);
    const int tile = tile_of(word);
    Attempt attempt;
    attempt.left = x;
    attempt.top = y;
    if (tile == DIRT) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 10 + extra;
        attempt.reason = "Road $10";
        map.set(x, y, ROADS | BULLBIT | BURNBIT);
        return attempt;
    }
    if (tile == RIVER || tile == REDGE || tile == CHANNEL) {
        bool placed = false;
        if (x + 1 < kWorldW && road_anchor_h(tile_of(map.get(x + 1, y)))) {
            placed = true;
        } else if (x > 0 && road_anchor_h_left(tile_of(map.get(x - 1, y)))) {
            placed = true;
        } else if (y + 1 < kWorldH && road_anchor_v(tile_of(map.get(x, y + 1)))) {
            placed = true;
        } else if (y > 0 && road_anchor_v(tile_of(map.get(x, y - 1)))) {
            placed = true;
        }
        if (!placed) {
            return blocked_at("No bridge anchor", x, y);
        }
        attempt.outcome = Outcome::Caution;
        attempt.cost = 50 + extra;
        attempt.reason = "Water: bridge cost";
        map.set(x, y, HBRIDGE | BULLBIT);
        return attempt;
    }
    if (tile == LHPOWER || tile == LVPOWER || tile == LHRAIL || tile == LVRAIL) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 10 + extra;
        attempt.reason = "Road $10";
        map.set(x, y, ROADS | BULLBIT | BURNBIT);
        return attempt;
    }
    return blocked_at("Cannot build here", x, y);
}

Attempt lay_rail(Scratch &map, int x, int y, bool auto_bulldoze)
{
    if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
        return blocked_at("Off the map", x, y);
    }
    int extra = 0;
    prepare_line(map, x, y, auto_bulldoze, extra);
    const int tile = neutralize_road(tile_of(map.get(x, y)));
    Attempt attempt;
    attempt.left = x;
    attempt.top = y;
    if (tile == DIRT) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 20 + extra;
        attempt.reason = "Rail $20";
        map.set(x, y, LHRAIL | BULLBIT | BURNBIT);
        return attempt;
    }
    if (tile == RIVER || tile == REDGE || tile == CHANNEL) {
        bool placed = false;
        if (x + 1 < kWorldW && rail_anchor_east(tile_of(map.get(x + 1, y)))) {
            placed = true;
        } else if (x > 0 && rail_anchor_west(tile_of(map.get(x - 1, y)))) {
            placed = true;
        } else if (y + 1 < kWorldH && rail_anchor_south(tile_of(map.get(x, y + 1)))) {
            placed = true;
        } else if (y > 0 && rail_anchor_north(tile_of(map.get(x, y - 1)))) {
            placed = true;
        }
        if (!placed) {
            return blocked_at("No bridge anchor", x, y);
        }
        attempt.outcome = Outcome::Caution;
        attempt.cost = 100 + extra;
        attempt.reason = "Rail bridge $100";
        map.set(x, y, HRAIL | BULLBIT);
        return attempt;
    }
    if (tile == LHPOWER || tile == LVPOWER || tile == ROADS || tile == ROADS2) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 20 + extra;
        attempt.reason = "Rail $20";
        map.set(x, y, LHRAIL | BULLBIT | BURNBIT);
        return attempt;
    }
    return blocked_at("Cannot build here", x, y);
}

Attempt lay_wire(Scratch &map, int x, int y, bool auto_bulldoze)
{
    if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
        return blocked_at("Off the map", x, y);
    }
    int extra = 0;
    prepare_line(map, x, y, auto_bulldoze, extra);
    const int tile = neutralize_road(tile_of(map.get(x, y)));
    Attempt attempt;
    attempt.left = x;
    attempt.top = y;
    if (tile == DIRT) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 5 + extra;
        attempt.reason = "Wire $5";
        map.set(x, y, LHPOWER | CONDBIT | BURNBIT | BULLBIT);
        return attempt;
    }
    if (tile == RIVER || tile == REDGE || tile == CHANNEL) {
        bool placed = false;
        if (x + 1 < kWorldW && wire_land_h(map.get(x + 1, y))) {
            placed = true;
        } else if (x > 0 && wire_land_h(map.get(x - 1, y))) {
            placed = true;
        } else if (y + 1 < kWorldH && wire_land_v(map.get(x, y + 1))) {
            placed = true;
        } else if (y > 0 && wire_land_v(map.get(x, y - 1))) {
            placed = true;
        }
        if (!placed) {
            return blocked_at("No wire landing", x, y);
        }
        attempt.outcome = Outcome::Caution;
        attempt.cost = 25 + extra;
        attempt.reason = "Underwater wire $25";
        map.set(x, y, HPOWER | CONDBIT | BULLBIT);
        return attempt;
    }
    if (tile == ROADS || tile == ROADS2 || tile == LHRAIL || tile == LVRAIL) {
        attempt.outcome = Outcome::Ok;
        attempt.cost = 5 + extra;
        attempt.reason = "Wire $5";
        map.set(x, y, LHPOWER | CONDBIT | BURNBIT | BULLBIT);
        return attempt;
    }
    return blocked_at("Cannot build here", x, y);
}

struct BuildingSpec {
    int width = 1;
    int height = 1;
    int cost = 0;
    const char *name = "Build";
    bool park = false;
};

BuildingSpec building_spec(int tool)
{
    switch (tool) {
    case TOOL_RESIDENTIAL:
        return {3, 3, 100, "Residential", false};
    case TOOL_COMMERCIAL:
        return {3, 3, 100, "Commercial", false};
    case TOOL_INDUSTRIAL:
        return {3, 3, 100, "Industrial", false};
    case TOOL_FIRESTATION:
        return {3, 3, 500, "Fire dept", false};
    case TOOL_POLICESTATION:
        return {3, 3, 500, "Police", false};
    case TOOL_STADIUM:
        return {4, 4, 5000, "Stadium", false};
    case TOOL_SEAPORT:
        return {4, 4, 3000, "Seaport", false};
    case TOOL_COALPOWER:
        return {4, 4, 3000, "Coal power", false};
    case TOOL_NUCLEARPOWER:
        return {4, 4, 5000, "Nuclear", false};
    case TOOL_AIRPORT:
        return {6, 6, 10000, "Airport", false};
    case TOOL_PARK:
        return {1, 1, 10, "Park", true};
    default:
        return {};
    }
}

const char *special_name(int index)
{
    if (index >= 0 && index < special_building_count()) {
        return special_building(index).name;
    }
    return "Special";
}

Attempt lay_building(Scratch &map, int tool, int x, int y, bool auto_bulldoze)
{
    const BuildingSpec spec = building_spec(tool);
    const int left = x - 1;
    const int top = y - 1;
    if (spec.park) {
        if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
            return blocked_at("Off the map", x, y);
        }
        if (map.get(x, y) != DIRT) {
            return blocked_at("Bulldoze first", x, y);
        }
        Attempt attempt;
        attempt.outcome = Outcome::Ok;
        attempt.cost = spec.cost;
        attempt.reason = "Park $10";
        attempt.left = x;
        attempt.top = y;
        map.set(x, y, WOODS2 | BULLBIT | BURNBIT);
        return attempt;
    }
    if (spec.width < 2) {
        return blocked_at("Cannot build here", x, y);
    }
    if (left < 0 || top < 0 || left + spec.width > kWorldW || top + spec.height > kWorldH) {
        return blocked_at("Off the map", left, top, spec.width, spec.height);
    }
    int extra = 0;
    for (int dy = 0; dy < spec.height; ++dy) {
        for (int dx = 0; dx < spec.width; ++dx) {
            const int word = map.get(left + dx, top + dy);
            const int tile = tile_of(word);
            if (tile == DIRT) {
                continue;
            }
            if (!auto_bulldoze || !tally_tile(tile)) {
                return blocked_at("Bulldoze first", left, top, spec.width, spec.height);
            }
            extra += 1;
        }
    }
    Attempt attempt;
    attempt.outcome = Outcome::Ok;
    attempt.cost = spec.cost + extra;
    attempt.left = left;
    attempt.top = top;
    attempt.width = spec.width;
    attempt.height = spec.height;
    attempt.reason = spec.name;
    for (int dy = 0; dy < spec.height; ++dy) {
        for (int dx = 0; dx < spec.width; ++dx) {
            map.set(left + dx, top + dy, DIRT);
        }
    }
    return attempt;
}

int scratch_word(int x, int y, const void *ctx)
{
    const Scratch *map = static_cast<const Scratch *>(ctx);
    if (map == nullptr) {
        return -1;
    }
    return map->get(x, y);
}

bool wet_tile(int tile)
{
    return water_tile(tile) || (tile >= FLOOD && tile <= LASTFLOOD);
}

Attempt lay_special(Scratch &map, int index, int x, int y, bool auto_bulldoze)
{
    if (index < 0 || index >= special_building_count()) {
        return blocked_at("Cannot build here", x, y, 3, 3);
    }
    const SpecialBuilding &building = special_building(index);
    const int left = x - building.anchor_x;
    const int top = y - building.anchor_y;
    const int width = building.width;
    const int height = building.height;
    if (left < 0 || top < 0 || left + width > kWorldW || top + height > kWorldH) {
        return blocked_at("Off the map", left, top, width, height);
    }
    bool water = false;
    bool land = false;
    for (int dy = 0; dy < height; ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            const int raw = map.get(left + dx, top + dy);
            const int tile = tile_of(raw);
            if (tile == DIRT) {
                continue;
            }
            // Same dock-row exception as placement. Water above that row blocks.
            if (building.needs_south_water && dy == height - 1 && wet_tile(tile)) {
                continue;
            }
            if (auto_bulldoze && auto_clearable_word(raw)) {
                continue;
            }
            if (wet_tile(tile)) {
                water = true;
            } else {
                land = true;
            }
        }
    }
    if (water && !land) {
        return blocked_at("Can't build on water", left, top, width, height);
    }
    if (water || land) {
        return blocked_at("Bulldoze first", left, top, width, height);
    }
    const std::string required = special_requirement_at(index, left, top, scratch_word, &map);
    if (!required.empty()) {
        // Attempt stores a pointer, so the sentence has to be a literal.
        const char *stable = "Cannot build here";
        if (required.find("shoreline") != std::string::npos) {
            stable = "Needs a shoreline to the south.";
        } else if (required.find("power and a road") != std::string::npos) {
            stable = "Needs power and a road.";
        } else if (required.find("power") != std::string::npos) {
            stable = "Needs power.";
        } else if (required.find("road") != std::string::npos) {
            stable = "Needs a road.";
        }
        return blocked_at(stable, left, top, width, height);
    }
    Attempt attempt;
    attempt.outcome = Outcome::Ok;
    attempt.cost = building.cost;
    attempt.reason = special_name(index);
    attempt.left = left;
    attempt.top = top;
    attempt.width = width;
    attempt.height = height;
    return attempt;
}

Attempt lay_bulldoze(Scratch &map, int x, int y)
{
    if (x < 0 || y < 0 || x >= kWorldW || y >= kWorldH) {
        return blocked_at("Off the map", x, y);
    }
    const int word = map.get(x, y);
    const int tile = tile_of(word);
    const bool special = special_index_for_tile(tile) >= 0 || tile == kSpecialBodyTile;
    if ((word & BULLBIT) == 0 && (word & ZONEBIT) == 0 && !special) {
        const char *why = "Cannot clear this";
        if (tile == DIRT) {
            why = "Nothing to clear";
        } else if (tile >= FIRE && tile <= LASTFIRE) {
            why = "Cannot clear fire";
        } else if (tile >= FLOOD && tile <= LASTFLOOD) {
            why = "Cannot clear flood";
        }
        return blocked_at(why, x, y);
    }
    Attempt attempt;
    attempt.outcome = Outcome::Ok;
    attempt.cost = 1;
    attempt.reason = "Bulldoze $1";
    attempt.left = x;
    attempt.top = y;
    map.set(x, y, DIRT);
    return attempt;
}

Attempt attempt_one(Scratch &map, int tool, int x, int y, int special_index, bool auto_bulldoze)
{
    if (tool == kSpecialEngineId || tool < 0) {
        return lay_special(map, special_index, x, y, auto_bulldoze);
    }
    switch (tool) {
    case TOOL_ROAD:
        return lay_road(map, x, y, auto_bulldoze);
    case TOOL_RAILROAD:
        return lay_rail(map, x, y, auto_bulldoze);
    case TOOL_WIRE:
        return lay_wire(map, x, y, auto_bulldoze);
    case TOOL_BULLDOZER:
        return lay_bulldoze(map, x, y);
    case TOOL_PARK:
    case TOOL_RESIDENTIAL:
    case TOOL_COMMERCIAL:
    case TOOL_INDUSTRIAL:
    case TOOL_FIRESTATION:
    case TOOL_POLICESTATION:
    case TOOL_STADIUM:
    case TOOL_SEAPORT:
    case TOOL_COALPOWER:
    case TOOL_NUCLEARPOWER:
    case TOOL_AIRPORT:
        return lay_building(map, tool, x, y, auto_bulldoze);
    default:
        return blocked_at("Cannot build here", x, y);
    }
}

PlaceForecast finish_forecast(const Attempt &attempt, long long funds)
{
    PlaceForecast forecast;
    forecast.left = attempt.left;
    forecast.top = attempt.top;
    forecast.width = attempt.width;
    forecast.height = attempt.height;
    forecast.cost = attempt.cost;
    if (attempt.outcome == Outcome::Blocked) {
        forecast.mark = attempt.reason != nullptr && std::string(attempt.reason) == "Bulldoze first" ? PlaceMark::Blocked
                                                                                                      : PlaceMark::Blocked;
        forecast.cost = 0;
        forecast.tag = attempt.reason;
        return forecast;
    }
    if (funds < attempt.cost) {
        forecast.mark = PlaceMark::Short;
        forecast.tag = "Need " + money(attempt.cost);
        return forecast;
    }
    forecast.mark = attempt.outcome == Outcome::Caution ? PlaceMark::Caution : PlaceMark::Ready;
    if (attempt.outcome == Outcome::Ok && attempt.reason != nullptr && std::strchr(attempt.reason, '$') == nullptr &&
        attempt.cost > 0) {
        forecast.tag = std::string(attempt.reason) + " " + money(attempt.cost);
    } else if (attempt.outcome == Outcome::Caution) {
        forecast.tag = attempt.reason;
    } else {
        forecast.tag = attempt.reason != nullptr ? attempt.reason : "";
    }
    // Auto-bulldoze adds a dollar the canned label does not mention.
    if (attempt.outcome == Outcome::Ok && attempt.cost > 0) {
        const std::string billed = money(attempt.cost);
        if (forecast.tag.find('$') != std::string::npos && forecast.tag.find(billed) == std::string::npos) {
            const auto space = forecast.tag.rfind(' ');
            if (space != std::string::npos) {
                forecast.tag = forecast.tag.substr(0, space + 1) + billed;
            }
        }
    }
    return forecast;
}

int tool_span(int tool)
{
    switch (tool) {
    case TOOL_RESIDENTIAL:
    case TOOL_COMMERCIAL:
    case TOOL_INDUSTRIAL:
    case TOOL_FIRESTATION:
    case TOOL_POLICESTATION:
    case kSpecialEngineId:
        return 3;
    case TOOL_STADIUM:
    case TOOL_SEAPORT:
    case TOOL_COALPOWER:
    case TOOL_NUCLEARPOWER:
        return 4;
    case TOOL_AIRPORT:
        return 6;
    default:
        return 1;
    }
}

void walk_line(int from_x, int from_y, int to_x, int to_y, const std::function<void(int, int)> &lay)
{
    if (from_x == to_x && from_y == to_y) {
        return;
    }
    const int dir_x = to_x > from_x ? 1 : -1;
    const int dir_y = to_y > from_y ? 1 : -1;
    lay(from_x, from_y);
    if (from_x == to_x) {
        while (from_y != to_y) {
            from_y += dir_y;
            lay(from_x, from_y);
        }
        return;
    }
    if (from_y == to_y) {
        while (from_x != to_x) {
            from_x += dir_x;
            lay(from_x, from_y);
        }
        return;
    }
    const int dx = std::abs(from_x - to_x);
    const int dy = std::abs(from_y - to_y);
    int sub_x = 0;
    int sub_y = 0;
    const int steps = dx < dy ? dx : dy;
    while (from_x != to_x || from_y != to_y) {
        sub_x += steps;
        if (sub_x >= dy) {
            sub_x -= dy;
            from_x += dir_x;
            lay(from_x, from_y);
        }
        sub_y += steps;
        if (sub_y >= dx) {
            sub_y -= dx;
            from_y += dir_y;
            lay(from_x, from_y);
        }
    }
}

} // namespace

PlaceForecast forecast_tile(int engine_tool, int tile_x, int tile_y, int special_index, long long funds,
                            bool auto_bulldoze, const PlaceMap &map_at)
{
    Scratch map;
    map.base = map_at;
    const int tool = engine_tool < 0 ? kSpecialEngineId : engine_tool;
    return finish_forecast(attempt_one(map, tool, tile_x, tile_y, special_index, auto_bulldoze), funds);
}

StrokeForecast forecast_stroke(int engine_tool, int from_x, int from_y, int to_x, int to_y, int special_index,
                               long long funds, bool auto_bulldoze, const PlaceMap &map_at)
{
    StrokeForecast stroke;
    Scratch map;
    map.base = map_at;
    const int tool = engine_tool < 0 ? kSpecialEngineId : engine_tool;
    long long left = funds;
    auto lay = [&](int x, int y) {
        const Attempt attempt = attempt_one(map, tool, x, y, special_index, auto_bulldoze);
        const PlaceForecast forecast = finish_forecast(attempt, left);
        stroke.here = forecast;
        if (forecast.mark == PlaceMark::Ready || forecast.mark == PlaceMark::Caution) {
            stroke.tiles += 1;
            stroke.cost += forecast.cost;
            left -= forecast.cost;
        }
    };
    if (tool_span(tool) > 1 || (from_x == to_x && from_y == to_y)) {
        lay(to_x, to_y);
    } else {
        walk_line(from_x, from_y, to_x, to_y, lay);
    }
    if (stroke.tiles > 1) {
        stroke.summary = std::to_string(stroke.tiles) + " tiles \u00b7 " + money(stroke.cost);
    } else if (stroke.tiles == 1) {
        stroke.summary = stroke.here.tag;
    } else {
        stroke.summary = stroke.here.tag;
    }
    return stroke;
}

std::string short_build_reason(const std::string &message)
{
    if (message.find("Bulldoze") != std::string::npos) {
        return "Bulldoze first";
    }
    if (message.find("funds") != std::string::npos || message.find("Funds") != std::string::npos) {
        return "Not enough funds";
    }
    if (message.find("off the map") != std::string::npos) {
        return "Off the map";
    }
    if (message.find("anchor") != std::string::npos) {
        return "No bridge anchor";
    }
    if (message.find("power line") != std::string::npos) {
        return "No wire landing";
    }
    if (message.find("water") != std::string::npos) {
        return "Can't build on water";
    }
    if (message.find("Cannot build") != std::string::npos) {
        return "Cannot build here";
    }
    if (message.empty()) {
        return "Cannot build here";
    }
    return message;
}

bool tile_matches_tool(int engine_tool, int word)
{
    const int tile = tile_of(word);
    switch (engine_tool) {
    case TOOL_ROAD:
        return tile >= ROADBASE && tile <= LASTROAD;
    case TOOL_RAILROAD:
        return tile >= RAILBASE && tile <= LASTRAIL;
    case TOOL_WIRE:
        return (word & CONDBIT) != 0 && tile >= POWERBASE && tile <= LASTPOWER;
    case TOOL_PARK:
        return tile >= WOODS_LOW && tile <= WOODS5;
    default:
        return false;
    }
}

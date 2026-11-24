// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Learn to Play (Brookside): the seven teaching steps and the predicates
// that decide which one is current. The checks are pure functions of a
// map census plus two UI flags. They never touch the simulator RNG.

// The including translation unit must include micropolis.h first
// (that header has no include guard).

#include <cstdint>
#include <string>

namespace tutorial {

inline constexpr int kStepCount = 7;
inline constexpr int kRoadTiles = 12;
inline constexpr int kParkTiles = 1;
inline constexpr int kPopulationGoal = LEARN_TO_PLAY_POPULATION_GOAL;

// South of the river, in the open field the map opens on.
inline constexpr int kFieldX = 58;
inline constexpr int kFieldY = 58;

enum Step : int {
    Roads = 0,
    Power,
    Zones,
    Safety,
    Parks,
    InspectBudget,
    Population,
};

struct Status {
    int roads = 0;
    int power_plants = 0;
    int res_zones = 0;
    int com_zones = 0;
    int ind_zones = 0;
    int police = 0;
    int fire = 0;
    int parks = 0;
    std::int64_t population = 0;
    bool inspected = false;
    bool budget_seen = false;
};

inline const char *title(int step)
{
    switch (step) {
    case Roads:
        return "Roads";
    case Power:
        return "Power";
    case Zones:
        return "Zones";
    case Safety:
        return "Police and fire";
    case Parks:
        return "Parks";
    case InspectBudget:
        return "Inspect and Budget";
    case Population:
        return "2,000 people";
    default:
        return "";
    }
}

inline const char *hint(int step)
{
    switch (step) {
    case Roads:
        return "Select the Road tool and draw a road across the open field. "
               "People need roads to reach their homes and jobs.";
    case Power:
        return "A city needs electricity. Place a power plant on open land, "
               "then run power lines toward where you will zone.";
    case Zones:
        return "Zone land along the roads: green for homes (R), blue for shops "
               "(C) and yellow for industry (I). The demand bars at the top "
               "show what people want most.";
    case Safety:
        return "Place a police station and a fire station near the zones. "
               "Police keep crime down; firefighters put out fires.";
    case Parks:
        return "Add a park near the homes. Parks raise land value and keep "
               "people happier.";
    case InspectBudget:
        return "Click Inspect, then click a zone or building to see how it is "
               "doing. Open Budget (under Tools) to see taxes and funding for "
               "roads, police and fire.";
    case Population:
        return "Keep zoning and connecting power. Reach 2,000 people and you "
               "have learned the basics — the city is yours.";
    default:
        return "";
    }
}

inline const char *done_toast(int step)
{
    switch (step) {
    case Roads:
        return "Roads are in. Next: give the city power.";
    case Power:
        return "The plant is up. Next: zone land for homes, shops and industry.";
    case Zones:
        return "The first neighborhoods are zoned. Next: police and fire.";
    case Safety:
        return "Police and fire are on the map. Next: add a park.";
    case Parks:
        return "A park is in. Next: Inspect a tile and open the Budget.";
    case InspectBudget:
        return "You have used Inspect and Budget. Next: grow to 2,000 people.";
    case Population:
        return "Brookside has 2,000 people. You have learned the basics.";
    default:
        return "";
    }
}

inline bool is_road_tile(MapTile tile)
{
    return tile >= ROADBASE && tile <= LASTROAD;
}

inline bool is_park_tile(MapTile tile)
{
    return tile == FOUNTAIN || (tile >= WOODS2 && tile <= WOODS5);
}

inline bool is_power_plant_center(MapTile tile)
{
    return tile == POWERPLANT || tile == NUCLEAR;
}

inline bool is_zone_center(MapTile tile, MapTile lo, MapTile hi, MapTile center)
{
    (void)lo;
    (void)hi;
    return tile == center;
}

inline bool step_complete(int step, const Status &s)
{
    switch (step) {
    case Roads:
        return s.roads >= kRoadTiles;
    case Power:
        return s.power_plants >= 1;
    case Zones:
        return s.res_zones >= 1 && s.com_zones >= 1 && s.ind_zones >= 1;
    case Safety:
        return s.police >= 1 && s.fire >= 1;
    case Parks:
        return s.parks >= kParkTiles;
    case InspectBudget:
        return s.inspected && s.budget_seen;
    case Population:
        return s.population >= kPopulationGoal;
    default:
        return false;
    }
}

// The first incomplete step, or kStepCount when the tutorial is won.
inline int current_step(const Status &s)
{
    for (int step = 0; step < kStepCount; ++step) {
        if (!step_complete(step, s)) {
            return step;
        }
    }
    return kStepCount;
}

inline std::string meter(int step, const Status &s)
{
    if (step >= kStepCount) {
        return "2,000 people — done";
    }
    if (step == Population) {
        const std::int64_t people = s.population > 0 ? s.population : 0;
        return "Step 7 of 7 · " + std::to_string(static_cast<long long>(people)) + " → 2,000";
    }
    return "Step " + std::to_string(step + 1) + " of 7 · " + title(step);
}

inline std::string meter_short(int step, const Status &s)
{
    if (step >= kStepCount) {
        return "2,000 done";
    }
    if (step == Population) {
        const std::int64_t people = s.population > 0 ? s.population : 0;
        return std::to_string(static_cast<long long>(people)) + "→2,000";
    }
    return std::to_string(step + 1) + "/7 " + title(step);
}

inline std::string meter_compact(int step, const Status &s)
{
    if (step >= kStepCount) {
        return "Done";
    }
    if (step == Population) {
        const std::int64_t people = s.population > 0 ? s.population : 0;
        return std::to_string(static_cast<long long>(people)) + "→2k";
    }
    return std::to_string(step + 1) + "/7";
}

inline std::string progress_line(int step)
{
    if (step >= kStepCount) {
        return "You reached 2,000 people. There is no deadline.";
    }
    return std::string("Step ") + std::to_string(step + 1) + " of 7: " + title(step) + ". " + hint(step);
}

// Count buildings and zones from a raw map. zone centers are the empty-zone
// tiles (RESCLR and friends) or the first house/shop/factory of a grown 3x3.
template <typename TileAt>
inline Status census(const TileAt &tile_at, int width, int height, std::int64_t population, bool inspected,
                     bool budget_seen)
{
    Status s;
    s.population = population;
    s.inspected = inspected;
    s.budget_seen = budget_seen;
    auto at = [&](int x, int y) -> MapTile {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return DIRT;
        }
        return static_cast<MapTile>(tile_at(x, y) & LOMASK);
    };
    auto is_kind = [](MapTile tile, MapTile lo, MapTile hi) { return tile >= lo && tile < hi; };
    auto zone_at = [&](int x, int y, MapTile lo, MapTile hi) {
        // A 3x3 zone is counted once, from its centre tile.
        const MapTile mid = at(x, y);
        if (!is_kind(mid, lo, hi)) {
            return false;
        }
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                if (!is_kind(at(x + dx, y + dy), lo, hi)) {
                    return false;
                }
            }
        }
        return true;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const MapTile tile = at(x, y);
            if (is_road_tile(tile)) {
                ++s.roads;
            }
            if (is_park_tile(tile)) {
                ++s.parks;
            }
            if (tile == POWERPLANT || tile == NUCLEAR) {
                ++s.power_plants;
            }
            if (tile == POLICESTATION) {
                ++s.police;
            }
            if (tile == FIRESTATION) {
                ++s.fire;
            }
            if (zone_at(x, y, RESBASE, COMBASE)) {
                ++s.res_zones;
            }
            if (zone_at(x, y, COMBASE, INDBASE)) {
                ++s.com_zones;
            }
            if (zone_at(x, y, INDBASE, PORTBASE)) {
                ++s.ind_zones;
            }
        }
    }
    return s;
}

} // namespace tutorial

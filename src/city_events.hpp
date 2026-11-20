// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// City 2's lasting disasters: Riot, Autonomous Zone, Lion Escape and Prison
// Escape. The engine keeps their state (Micropolis::cityEvents, saved in
// the LCD1 trailer); this file starts them, steps them, and answers what
// the map, the tools and the Inspect card need. Everything here is
// deterministic: the same city and the same time give the same result.

#include <string>
#include <utility>
#include <vector>

class Micropolis;

namespace city_events {

enum class Kind { Riot = 0, AutonomousZone = 1, LionEscape = 2, PrisonEscape = 3 };
inline constexpr int kKindCount = 4;

// One row per disaster. key is the name a scenario file uses ("riot",
// "autonomous_zone", "lion_escape", "prison_escape").
struct KindInfo {
    Kind kind;
    const char *key;
    const char *title;
    const char *question;
    const char *detail;
    // The NEWS line and the Messages log.
    const char *notice;
};

const KindInfo &info(Kind kind);
bool kind_from_key(const std::string &key, Kind &kind);

struct Note {
    std::string text;
    int x = 0;
    int y = 0;
    bool has_place = false;
};

// ---- Numbers (reported to the editor; tests read them) -----------------

// Riot: fires on this many commercial buildings, and extra crime on every
// developed block of the crime map (0..250) for kRiotSteps city-time steps
// (4 a month: 6 months): full strength for the first half, then a straight
// fade to nothing.
inline constexpr int kRiotFires = 6;
inline constexpr int kRiotCrimePeak = 40;
inline constexpr int kRiotSteps = 24;
// A riot started downtown by a scenario (start_riot_at) only sets fires
// within this many tiles of the spot it is given.
inline constexpr int kRiotDowntownRadius = 10;

// Autonomous Zone: a block is a road block, the connected land (4-way)
// that roads, rail, water and the map edge close in. An open area larger
// than kZoneBlockMaxTiles is cut into kZoneCapCell x kZoneCapCell squares
// of the map grid; each building goes whole with the square its centre
// tile sits in, so no cut runs through a building. Two blocks are
// neighbours when they touch or face each other across up to
// kZoneStreetWidth tiles of road or rail.
//
// It forms on two neighbouring blocks, both closed in by streets (not cut
// pieces of an open area) and neither on the map edge, each with
// at least kZoneMinBuilt building tiles and kZoneBuiltPercent of their
// land built on: the pair with the least police (tile average), then the
// more built. Every kZoneReviewSteps (2 months) the zone's police coverage
// (tile average) decides: below kZoneGrowBelow it takes the neighbouring
// block with the least police; at kZoneShrinkAt or more it gives up the
// block it took last; in between it holds. kZoneMaxBlocks caps it. The
// streets between its blocks belong to it too.
inline constexpr int kZoneBlockMaxTiles = 144;
inline constexpr int kZoneCapCell = 12;
inline constexpr int kZoneStreetWidth = 2;
inline constexpr int kZoneMinBuilt = 9;
inline constexpr int kZoneBuiltPercent = 40;
inline constexpr int kZoneStartBlocks = 2;
inline constexpr int kZoneReviewSteps = 8;
inline constexpr int kZoneGrowBelow = 40;
inline constexpr int kZoneShrinkAt = 80;
inline constexpr int kZoneMaxBlocks = 48;
inline constexpr int kZoneLandInside = 50;
inline constexpr int kZoneLandBand = 4;

// Lion Escape: 2 map pixels a frame (a tile every 8 frames, two tiles a
// week). Every kLionMealSteps it eats from the nearest home within
// kLionMealRadius tiles. It is caught where police coverage reaches
// kLionCatchCoverage after a month loose, or by its keepers after
// kLionMaxSteps (6 months).
inline constexpr int kLionPixelsPerFrame = 2;
inline constexpr int kLionMealSteps = 2;
inline constexpr int kLionMealRadius = 2;
inline constexpr int kLionCatchCoverage = 60;
inline constexpr int kLionCatchAfterSteps = 4;
inline constexpr int kLionMaxSteps = 24;

// Prison Escape: fires on up to kPrisonFires buildings within
// kPrisonFireRadius tiles of the prison, and extra crime within
// kPrisonCrimeRadius tiles that starts at kPrisonCrimePeak and fades by
// 2 + police coverage / 40 each step (faster with police nearby).
inline constexpr int kPrisonFires = 4;
inline constexpr int kPrisonFireRadius = 8;
inline constexpr int kPrisonCrimeRadius = 12;
inline constexpr int kPrisonCrimePeak = 60;
inline constexpr int kPrisonMaxSteps = 48;

// ---- Starting ----------------------------------------------------------

// Empty when the disaster can start now; otherwise one sentence why not
// ("Lion Escape needs a Zoo.").
std::string unavailable_reason(const Micropolis *sim, Kind kind);
// Starts it. False (with the reason in notes) when it cannot.
bool start(Micropolis *sim, Kind kind, std::vector<Note> &notes);
// The Autonomous Zone on the road block holding tile (x, y) and its
// least-policed neighbour, for scenarios. False when (x, y) is not on a
// block, the block has no neighbour, or a zone already stands. None of
// these four is ever picked by the random disaster roll.
bool start_zone_at(Micropolis *sim, int x, int y, std::vector<Note> &notes);
// Prison Escape from a prison on an island, for scenarios: the escapees
// come ashore at (x, y), so the fires (kPrisonFires within
// kPrisonFireRadius) and the crime (within kPrisonCrimeRadius, fading with
// the police coverage there) are around that landing instead of around
// the prison. The notice points at the prison. False (with the reason in
// notes) when there is no prison or an escape is already going on.
bool start_prison_landing(Micropolis *sim, int x, int y, std::vector<Note> &notes);
// Riot started downtown, for scenarios: the fires are on commercial zones
// within kRiotDowntownRadius of (x, y) (anywhere when there are none), and
// the crime surge is city-wide as usual. False (with the reason in notes)
// when a riot is already going on.
bool start_riot_at(Micropolis *sim, int x, int y, std::vector<Note> &notes);

// ---- Running -----------------------------------------------------------

// Every stepped frame: the lion walks; once per city-time step the riot,
// zone, lion and prison clocks run.
void frame(Micropolis *sim, std::vector<Note> &notes);
// The engine's crime and land passes call these after the special
// buildings have made their changes.
void apply_crime(Micropolis *sim);
void apply_land(Micropolis *sim);

void clear(Micropolis *sim);
bool any_active(const Micropolis *sim);

// ---- Questions ---------------------------------------------------------

bool in_zone(const Micropolis *sim, int x, int y);
// True when this engine tool, used at (x, y), would touch the zone.
bool edit_blocked(const Micropolis *sim, int tool, int x, int y);
// True when a w x h footprint with its top-left at (left, top) touches it.
bool footprint_in_zone(const Micropolis *sim, int left, int top, int w, int h);
// The first tile of each of the zone's blocks, in the order it took them.
std::vector<std::pair<int, int>> zone_blocks(const Micropolis *sim);
// The road block holding (x, y): its tiles, scan order. Empty on a road,
// rail or water tile.
std::vector<std::pair<int, int>> road_block_at(const Micropolis *sim, int x, int y);

struct LionView {
    bool active = false;
    // Centre, in 16px map pixels.
    int x = 0;
    int y = 0;
    int dir = 2;
    int frame = 0;
};
LionView lion(const Micropolis *sim);
int riot_crime_now(const Micropolis *sim);

// Barricade pieces along the zone border, each drawn inside tile (x, y)
// against one edge: side 0 north, 1 east, 2 south, 3 west. Where the zone
// meets a street the piece sits on the street tile, against the curb, so
// it never covers a building. side 4..7 is a corner post in the north-west,
// north-east, south-east or south-west corner of the tile, closing the gap
// where two street-side lines meet.
struct Barricade {
    int x;
    int y;
    int side;
};
std::vector<Barricade> barricades(const Micropolis *sim);

} // namespace city_events

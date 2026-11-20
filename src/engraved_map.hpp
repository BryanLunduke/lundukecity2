// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// Engraved minimap: the city drawn like an old engraved map on paper.
// Ink only: streets are double lines with paper blocks, homes, shops and
// industry are told apart by their hatching, water gets a shore rule,
// waterlines and wavy lines, woods are stippled, rail has cross ties,
// civic buildings and plants are solid ink, and landmarks are double ruled.
//
// The picture is built from cached layers. The paper and the frame are made
// once per size. Water, zones and streets are redrawn only when tiles of
// that kind change, and the shell throttles those rebuilds. Every frame is
// a single blit of the finished sheet.

#include <cairo.h>

#include <cstdint>
#include <string>
#include <vector>

namespace engraved {

constexpr int kW = 120;
constexpr int kH = 100;

// Per-tile class. The code stored per tile is class | flags.
enum Cls : std::uint8_t {
    Land = 0,
    Water,
    Woods,
    Road,
    Rail,
    Wire,
    Res,
    Com,
    Ind,
    Civic,     // police, fire, hospital, stadium, port, airport, plants
    Landmark,  // 3x3 specials, Data Center
    Symbol,    // 1x1 specials: micro nuclear plant, fountain
    Ruin,
    Bridge,    // road over water
    RailBridge,
    WireWater, // power line over water
};
constexpr std::uint8_t kClsMask = 0x1f;
constexpr std::uint8_t kEmptyLot = 0x20; // a zone with nothing built yet
constexpr std::uint8_t kCentre = 0x40;   // the centre tile of a landmark or symbol

std::uint8_t classify_tile(int raw);
// Whole-map classification. Special-building bodies take their centre's
// class. raw is row-major: raw[y * kW + x].
std::vector<std::uint8_t> classify_map(const int *raw);

bool in_water_layer(std::uint8_t code);
bool in_zone_layer(std::uint8_t code);
bool in_road_layer(std::uint8_t code);

struct Dirty {
    bool water = false;
    bool zones = false;
    bool roads = false;
    bool any() const { return water || zones || roads; }
};
// Which layers differ between two classifications. A traffic or power
// change on a road tile is the same class, so it does not redraw anything.
Dirty compare(const std::vector<std::uint8_t> &before, const std::vector<std::uint8_t> &after);

// Sheet geometry. The map fills the minimap's land box. The paper reaches
// margin pixels past it on the left, top and right, and margin + caption
// below it, where the title, the north arrow and the scale bar sit.
struct SheetGeom {
    int map_w = 0;
    int map_h = 0;
    int margin = 0;
    int caption = 0;
    int sheet_w() const { return map_w + 2 * margin; }
    int sheet_h() const { return map_h + 2 * margin + caption; }
    // Height the sheet adds under the land box.
    int below() const { return margin + caption; }
};
SheetGeom sheet_geom(double land_w, double land_h, double unit);
int sheet_margin(double unit);
int sheet_caption(double unit);

// A point relative to the land box's top left. True only inside the map
// itself; the margin and the caption strip are not part of the map.
bool land_fraction(double x, double y, double land_w, double land_h, double &fx, double &fy);

// Rebuild throttle. A change is drawn at once if nothing was drawn for
// interval seconds, otherwise when the interval has passed.
class RebuildGate {
public:
    explicit RebuildGate(double interval = 1.25) : interval_(interval) {}
    void mark() { pending_ = true; }
    bool pending() const { return pending_; }
    bool due(double now);
    void reset() { pending_ = false; last_ = -1e9; }

private:
    double interval_;
    double last_ = -1e9;
    bool pending_ = false;
};

// Overlay plates over the faded paper. Values per tile come from the
// session's overlay layer: codes for the on/off layers, -1..1 for heat.
enum class Plate { None, Power, Water, Zones, Res, Com, Ind, Transport, Heat, Growth };
// True when the plate prints in red, so the view frame turns dark ink.
bool plate_is_red(Plate plate);

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // Size of the map area in pixels, the UI unit (1 at 100%), the title.
    void set_geometry(int map_w, int map_h, double unit, const std::string &title);
    void set_night(bool on);
    // Classifies the map and redraws the layers whose tiles changed.
    // Returns the layers that were redrawn.
    Dirty update_tiles(const int *raw);
    void set_plate(Plate plate, const std::vector<float> &values);
    void clear_plate();
    // Builds the finished sheet if anything changed. Cheap otherwise.
    void compose();
    cairo_surface_t *sheet() const { return sheet_; }
    const SheetGeom &geom() const { return geom_; }
    Plate plate() const { return plate_; }

    struct Timing {
        double paper = 0;
        double frame = 0;
        double water = 0;
        double zones = 0;
        double roads = 0;
        double plate = 0;
        double compose = 0;
    };
    const Timing &timing() const { return timing_; }
    struct Counts {
        int paper = 0;
        int water = 0;
        int zones = 0;
        int roads = 0;
        int plate = 0;
        int compose = 0;
    };
    const Counts &counts() const { return counts_; }

private:
    void drop_surfaces();
    void build_paper();
    void build_frame();
    void build_water();
    void build_zones();
    void build_roads();
    void build_plate();
    void ensure_spans();
    float cover(int px, int py, bool (*want)(std::uint8_t)) const;

    SheetGeom geom_;
    double unit_ = 1.0;
    double scale_ = 1.0; // pixels per tile
    std::string title_;
    bool night_ = false;
    std::vector<std::uint8_t> codes_;
    bool have_codes_ = false;
    Plate plate_ = Plate::None;
    std::vector<float> plate_values_;

    cairo_surface_t *paper_ = nullptr; // sheet size, RGB24
    cairo_surface_t *frame_ = nullptr; // paper + rules + caption
    cairo_surface_t *water_ = nullptr; // map size, ARGB32
    cairo_surface_t *zones_ = nullptr;
    cairo_surface_t *roads_ = nullptr;
    cairo_surface_t *plate_layer_ = nullptr;
    cairo_surface_t *sheet_ = nullptr;
    bool frame_dirty_ = true;
    bool water_dirty_ = true;
    bool zones_dirty_ = true;
    bool roads_dirty_ = true;
    bool plate_dirty_ = false;
    bool compose_dirty_ = true;

    struct Span {
        int tile;
        float weight;
    };
    std::vector<std::vector<Span>> xspans_;
    std::vector<std::vector<Span>> yspans_;

    Timing timing_;
    Counts counts_;
};

} // namespace engraved

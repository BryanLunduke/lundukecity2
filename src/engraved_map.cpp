// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "engraved_map.hpp"

#include "heat_field.hpp"

#include "micropolis.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace engraved {

namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = 3.14159265358979323846;

double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Ink and paper.
constexpr double kInkR = 0x2a / 255.0, kInkG = 0x1f / 255.0, kInkB = 0x17 / 255.0;
constexpr double kWaterInkR = 0x1f / 255.0, kWaterInkG = 0x33 / 255.0, kWaterInkB = 0x46 / 255.0;
constexpr double kPaperR = 0xef / 255.0, kPaperG = 0xe3 / 255.0, kPaperB = 0xc6 / 255.0;
constexpr double kStreetR = 0xf5 / 255.0, kStreetG = 0xec / 255.0, kStreetB = 0xd6 / 255.0;

std::uint32_t hash3(std::uint32_t x, std::uint32_t y, std::uint32_t seed)
{
    std::uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

double unit_hash(int x, int y, std::uint32_t seed)
{
    return (hash3(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) & 0xffffff) / 16777216.0;
}

// Premultiplied ARGB32 pixel buffer with float accumulation helpers.
struct Layer {
    int w = 0;
    int h = 0;
    std::vector<float> a; // coverage of the ink colour
    std::vector<float> r, g, b;
    Layer(int width, int height)
        : w(width), h(height), a(static_cast<std::size_t>(width * height), 0.0f),
          r(a.size(), 0.0f), g(a.size(), 0.0f), b(a.size(), 0.0f)
    {
    }
    // Paint colour with alpha over what is already there.
    void over(int x, int y, double cr, double cg, double cb, double alpha)
    {
        if (alpha <= 0.0) {
            return;
        }
        alpha = std::min(1.0, alpha);
        const std::size_t i = static_cast<std::size_t>(y * w + x);
        r[i] = static_cast<float>(cr * alpha + r[i] * (1.0 - alpha));
        g[i] = static_cast<float>(cg * alpha + g[i] * (1.0 - alpha));
        b[i] = static_cast<float>(cb * alpha + b[i] * (1.0 - alpha));
        a[i] = static_cast<float>(alpha + a[i] * (1.0 - alpha));
    }
    void store(cairo_surface_t *surface) const
    {
        cairo_surface_flush(surface);
        unsigned char *data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        for (int y = 0; y < h; ++y) {
            auto *row = reinterpret_cast<std::uint32_t *>(data + y * stride);
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y * w + x);
                auto byte = [](float v) {
                    return static_cast<std::uint32_t>(std::max(0.0f, std::min(255.0f, v * 255.0f + 0.5f)));
                };
                row[x] = (byte(a[i]) << 24) | (byte(r[i]) << 16) | (byte(g[i]) << 8) | byte(b[i]);
            }
        }
        cairo_surface_mark_dirty(surface);
    }
};

cairo_surface_t *fresh(cairo_surface_t *old, cairo_format_t format, int w, int h)
{
    if (old != nullptr && cairo_image_surface_get_width(old) == w && cairo_image_surface_get_height(old) == h &&
        cairo_image_surface_get_format(old) == format) {
        return old;
    }
    if (old != nullptr) {
        cairo_surface_destroy(old);
    }
    return cairo_image_surface_create(format, std::max(1, w), std::max(1, h));
}

void clear_surface(cairo_surface_t *surface)
{
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
}

// Smooth value noise on a coarse lattice, for the paper.
std::vector<float> value_noise(int w, int h, double cell, std::uint32_t seed)
{
    std::vector<float> out(static_cast<std::size_t>(w * h));
    for (int y = 0; y < h; ++y) {
        const double gy = y / cell;
        const int y0 = static_cast<int>(std::floor(gy));
        const double ty = gy - y0;
        const double sy = ty * ty * (3 - 2 * ty);
        for (int x = 0; x < w; ++x) {
            const double gx = x / cell;
            const int x0 = static_cast<int>(std::floor(gx));
            const double tx = gx - x0;
            const double sx = tx * tx * (3 - 2 * tx);
            const double a = unit_hash(x0, y0, seed), b = unit_hash(x0 + 1, y0, seed);
            const double c = unit_hash(x0, y0 + 1, seed), d = unit_hash(x0 + 1, y0 + 1, seed);
            const double top = a + (b - a) * sx;
            const double bottom = c + (d - c) * sx;
            out[static_cast<std::size_t>(y * w + x)] = static_cast<float>((top + (bottom - top) * sy) * 2.0 - 1.0);
        }
    }
    return out;
}

bool is_water(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Water || c == Bridge || c == RailBridge || c == WireWater;
}
bool is_woods(std::uint8_t code) { return (code & kClsMask) == Woods; }
bool is_res(std::uint8_t code) { return (code & kClsMask) == Res; }
bool is_com(std::uint8_t code) { return (code & kClsMask) == Com; }
bool is_ind(std::uint8_t code) { return (code & kClsMask) == Ind; }
bool is_civic(std::uint8_t code) { return (code & kClsMask) == Civic; }
bool is_ruin(std::uint8_t code) { return (code & kClsMask) == Ruin; }
bool is_built(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Res || c == Com || c == Ind || c == Civic || c == Landmark;
}
bool is_street(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Road || c == Bridge;
}
bool is_rail(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Rail || c == RailBridge;
}
bool is_wire(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Wire || c == WireWater;
}

} // namespace

// ------------------------------------------------------------ classification

std::uint8_t classify_tile(int raw)
{
    const int t = raw & LOMASK;
    auto in = [t](int lo, int hi) { return t >= lo && t <= hi; };
    if (t == SPECIAL_MICRONUKE || t == SPECIAL_FOUNTAIN) {
        return Symbol | kCentre;
    }
    if (t == SPECIAL_ZOO || t == SPECIAL_PARK || t == SPECIAL_LIBRARY || t == SPECIAL_MAYOR || t == SPECIAL_CASINO ||
        t == SPECIAL_AMUSEMENT || t == SPECIAL_PRISON || t == SPECIAL_MARINA || t == SPECIAL_DATACENTER ||
        t == SPECIAL_DATACENTER_LOT) {
        return Landmark | kCentre;
    }
    if (t == SPECIAL_BODY) {
        return Landmark;
    }
    if (in(RIVER, LASTRIVEDGE)) {
        return Water;
    }
    if (in(TREEBASE, WOODS5) || t == FOUNTAIN) {
        return Woods;
    }
    if (t == HBRIDGE || t == VBRIDGE || t == BRWH || t == BRWV || in(HBRDG0, HBRDG3) || in(VBRDG0, VBRDG3) ||
        t == BRWXXX1 || t == BRWXXX2 || t == BRWXXX3 || t == BRWXXX4 || t == BRWXXX5 || t == BRWXXX6 ||
        t == BRWXXX7) {
        return Bridge;
    }
    if (in(ROADBASE, LASTROAD) || t == ROADVPOWERH) {
        return Road;
    }
    if (t == HPOWER || t == VPOWER) {
        return WireWater;
    }
    if (t == RAILHPOWERV || t == RAILVPOWERH) {
        return Rail;
    }
    if (in(POWERBASE, LASTPOWER)) {
        return Wire;
    }
    if (t == HRAIL || t == VRAIL) {
        return RailBridge;
    }
    if (in(RAILBASE, LASTRAIL)) {
        return Rail;
    }
    if (in(HOSPITALBASE, HOSPITALBASE + 8)) {
        return Civic;
    }
    if (in(RESBASE, RESBASE + 8) || in(COMBASE, COMBASE + 8) || in(INDBASE, LASTIND)) {
        const int c = t < COMBASE ? Res : (t < INDBASE ? Com : Ind);
        return static_cast<std::uint8_t>(c | kEmptyLot);
    }
    if (in(RESBASE, COMBASE - 1) || in(CHURCH1BASE, CHURCH7LAST)) {
        return Res;
    }
    if (in(COMBASE, INDBASE - 1)) {
        return Com;
    }
    if (in(INDBASE, PORTBASE - 1) || in(INDBASE2, SMOKEBASE + 7) || in(TINYEXPLAST + 1, COALSMOKE1 - 1)) {
        return Ind;
    }
    if (in(PORTBASE, LASTZONE) || in(RADAR0, RADAR7) || in(COALSMOKE1, FOOTBALLGAME2 + 7) ||
        in(NUKESWIRL1, NUKESWIRL4)) {
        return Civic;
    }
    if (in(RUBBLE, LASTRUBBLE) || in(FLOOD, LASTFLOOD) || t == RADTILE || in(FIRE, LASTFIRE) ||
        in(TINYEXP, TINYEXPLAST) || t == LIGHTNINGBOLT) {
        return Ruin;
    }
    return Land;
}

std::vector<std::uint8_t> classify_map(const int *raw)
{
    std::vector<std::uint8_t> codes(static_cast<std::size_t>(kW * kH), Land);
    for (int i = 0; i < kW * kH; ++i) {
        codes[static_cast<std::size_t>(i)] = classify_tile(raw[i]);
    }
    return codes;
}

bool in_water_layer(std::uint8_t code)
{
    return is_water(code);
}

bool in_zone_layer(std::uint8_t code)
{
    const int c = code & kClsMask;
    return c == Woods || c == Res || c == Com || c == Ind || c == Civic || c == Landmark || c == Symbol || c == Ruin;
}

bool in_road_layer(std::uint8_t code)
{
    return is_street(code) || is_rail(code) || is_wire(code);
}

Dirty compare(const std::vector<std::uint8_t> &before, const std::vector<std::uint8_t> &after)
{
    Dirty dirty;
    if (before.size() != after.size()) {
        dirty.water = dirty.zones = dirty.roads = true;
        return dirty;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        const std::uint8_t a = before[i];
        const std::uint8_t b = after[i];
        if (a == b) {
            continue;
        }
        if (in_water_layer(a) != in_water_layer(b)) {
            dirty.water = true;
        }
        if (in_zone_layer(a) || in_zone_layer(b)) {
            dirty.zones = true;
        }
        if (in_road_layer(a) != in_road_layer(b) || ((a ^ b) & kClsMask) != 0) {
            if (in_road_layer(a) || in_road_layer(b)) {
                dirty.roads = true;
            }
        }
    }
    return dirty;
}

// ------------------------------------------------------------ geometry

int sheet_margin(double unit)
{
    return std::max(4, static_cast<int>(std::lround(6.0 * unit)));
}

int sheet_caption(double unit)
{
    return std::max(16, static_cast<int>(std::lround(24.0 * unit)));
}

SheetGeom sheet_geom(double land_w, double land_h, double unit)
{
    SheetGeom geom;
    geom.map_w = std::max(1, static_cast<int>(std::floor(land_w + 0.01)));
    geom.map_h = std::max(1, static_cast<int>(std::floor(land_h + 0.01)));
    geom.margin = sheet_margin(unit);
    geom.caption = sheet_caption(unit);
    return geom;
}

bool land_fraction(double x, double y, double land_w, double land_h, double &fx, double &fy)
{
    if (land_w <= 1.0 || land_h <= 1.0 || x < 0.0 || y < 0.0 || x >= land_w || y >= land_h) {
        return false;
    }
    fx = x / land_w;
    fy = y / land_h;
    return true;
}

bool RebuildGate::due(double now)
{
    if (!pending_) {
        return false;
    }
    if (now - last_ < interval_) {
        return false;
    }
    pending_ = false;
    last_ = now;
    return true;
}

bool plate_is_red(Plate plate)
{
    return plate == Plate::Power || plate == Plate::Heat || plate == Plate::Growth;
}

// ------------------------------------------------------------ renderer

Renderer::~Renderer()
{
    drop_surfaces();
}

void Renderer::drop_surfaces()
{
    for (cairo_surface_t **s : {&paper_, &frame_, &water_, &zones_, &roads_, &plate_layer_, &sheet_}) {
        if (*s != nullptr) {
            cairo_surface_destroy(*s);
            *s = nullptr;
        }
    }
}

void Renderer::set_geometry(int map_w, int map_h, double unit, const std::string &title)
{
    const SheetGeom next = sheet_geom(map_w, map_h, unit);
    const bool size_changed = next.map_w != geom_.map_w || next.map_h != geom_.map_h || next.margin != geom_.margin ||
                              next.caption != geom_.caption || std::fabs(unit - unit_) > 1e-6;
    if (size_changed) {
        geom_ = next;
        unit_ = unit;
        scale_ = static_cast<double>(geom_.map_w) / kW;
        xspans_.clear();
        yspans_.clear();
        build_paper();
        frame_dirty_ = water_dirty_ = zones_dirty_ = roads_dirty_ = true;
        plate_dirty_ = plate_ != Plate::None;
        compose_dirty_ = true;
    }
    if (title != title_) {
        title_ = title;
        frame_dirty_ = true;
        compose_dirty_ = true;
    }
}

void Renderer::set_night(bool on)
{
    if (on != night_) {
        night_ = on;
        compose_dirty_ = true;
    }
}

Dirty Renderer::update_tiles(const int *raw)
{
    std::vector<std::uint8_t> next = classify_map(raw);
    // Special bodies take the class of the centre they belong to, and a
    // 1x1 symbol has no body.
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            std::uint8_t &code = next[static_cast<std::size_t>(y * kW + x)];
            if ((raw[y * kW + x] & LOMASK) != SPECIAL_BODY) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= kW || yy >= kH) {
                        continue;
                    }
                    const std::uint8_t c = next[static_cast<std::size_t>(yy * kW + xx)];
                    if ((c & kCentre) != 0 && (c & kClsMask) == Landmark) {
                        code = Landmark;
                    }
                }
            }
        }
    }
    Dirty dirty;
    if (!have_codes_) {
        dirty.water = dirty.zones = dirty.roads = true;
    } else {
        dirty = compare(codes_, next);
    }
    codes_.swap(next);
    have_codes_ = true;
    water_dirty_ = water_dirty_ || dirty.water;
    zones_dirty_ = zones_dirty_ || dirty.zones;
    roads_dirty_ = roads_dirty_ || dirty.roads;
    if (dirty.any()) {
        compose_dirty_ = true;
        if (plate_ != Plate::None) {
            plate_dirty_ = true;
        }
    }
    return dirty;
}

void Renderer::set_plate(Plate plate, const std::vector<float> &values)
{
    if (plate == plate_ && values == plate_values_) {
        return;
    }
    plate_ = plate;
    plate_values_ = values;
    plate_dirty_ = plate != Plate::None;
    compose_dirty_ = true;
}

void Renderer::clear_plate()
{
    if (plate_ != Plate::None) {
        plate_ = Plate::None;
        plate_values_.clear();
        compose_dirty_ = true;
    }
}

void Renderer::ensure_spans()
{
    if (!xspans_.empty()) {
        return;
    }
    auto spans = [this](int pixels, int tiles) {
        std::vector<std::vector<Span>> out(static_cast<std::size_t>(pixels));
        for (int p = 0; p < pixels; ++p) {
            const double t0 = p / scale_;
            const double t1 = (p + 1) / scale_;
            for (int t = static_cast<int>(std::floor(t0)); t < t1 && t < tiles; ++t) {
                const double lo = std::max(t0, static_cast<double>(t));
                const double hi = std::min(t1, static_cast<double>(t + 1));
                if (hi > lo && t >= 0) {
                    out[static_cast<std::size_t>(p)].push_back(Span{t, static_cast<float>((hi - lo) * scale_)});
                }
            }
        }
        return out;
    };
    xspans_ = spans(geom_.map_w, kW);
    yspans_ = spans(geom_.map_h, kH);
}

float Renderer::cover(int px, int py, bool (*want)(std::uint8_t)) const
{
    float sum = 0.0f;
    for (const Span &sy : yspans_[static_cast<std::size_t>(py)]) {
        for (const Span &sx : xspans_[static_cast<std::size_t>(px)]) {
            if (want(codes_[static_cast<std::size_t>(sy.tile * kW + sx.tile)])) {
                sum += sx.weight * sy.weight;
            }
        }
    }
    return sum;
}

void Renderer::build_paper()
{
    const auto start = Clock::now();
    const int w = geom_.sheet_w();
    const int h = geom_.sheet_h();
    paper_ = fresh(paper_, CAIRO_FORMAT_RGB24, w, h);
    const std::vector<float> broad = value_noise(w, h, std::max(8.0, std::max(w, h) / 5.0), 0x1890u);
    const std::vector<float> mid = value_noise(w, h, 3.0 * unit_, 0x4a11u);
    cairo_surface_flush(paper_);
    unsigned char *data = cairo_image_surface_get_data(paper_);
    const int stride = cairo_image_surface_get_stride(paper_);
    // A few faint foxing spots at fixed, seeded places.
    struct Spot {
        double x, y, r;
    };
    std::vector<Spot> spots;
    const int count = std::max(1, w * h / 20000);
    for (int i = 0; i < count; ++i) {
        spots.push_back(Spot{unit_hash(i, 1, 0x70c5u) * w, unit_hash(i, 2, 0x70c5u) * h,
                             (2.0 + unit_hash(i, 3, 0x70c5u) * 4.0) * unit_});
    }
    const double edge_span = std::min(w, h) * 0.18;
    for (int y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(data + y * stride);
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * w + x);
            const double fine = (unit_hash(x, y, 0x9e37u) - 0.5) * 0.04;
            const double tone = broad[i] * 0.035 + mid[i] * 0.016 + fine;
            double r = kPaperR * (1.0 + tone);
            double g = kPaperG * (1.0 + tone * 1.05);
            double b = kPaperB * (1.0 + tone * 1.25);
            double stain = 0.0;
            for (const Spot &s : spots) {
                const double d2 = ((x - s.x) * (x - s.x) + (y - s.y) * (y - s.y)) / (2.0 * s.r * s.r);
                if (d2 < 6.0) {
                    stain += std::exp(-d2) * 0.16;
                }
            }
            const double e = std::min(std::min(x, w - 1 - x), std::min(y, h - 1 - y));
            double age = std::max(0.0, 1.0 - e / edge_span);
            age = age * age * 0.32;
            // Multiply toward a brown stain colour.
            auto mul = [](double v, double c, double a) { return v * (1.0 - a + a * c); };
            r = mul(mul(r, 0xc9 / 255.0, stain), 0xb9 / 255.0, age);
            g = mul(mul(g, 0xa5 / 255.0, stain), 0x8f / 255.0, age);
            b = mul(mul(b, 0x71 / 255.0, stain), 0x55 / 255.0, age);
            auto byte = [](double v) {
                return static_cast<std::uint32_t>(std::max(0.0, std::min(255.0, v * 255.0 + 0.5)));
            };
            row[x] = 0xff000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
        }
    }
    cairo_surface_mark_dirty(paper_);
    ++counts_.paper;
    timing_.paper = ms_since(start);
}

void Renderer::build_frame()
{
    const auto start = Clock::now();
    const int w = geom_.sheet_w();
    const int h = geom_.sheet_h();
    frame_ = fresh(frame_, CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(frame_);
    cairo_set_source_surface(cr, paper_, 0, 0);
    cairo_paint(cr);
    const double u = unit_;
    const double m = geom_.margin;
    const double lw = std::max(1.0, std::round(u));
    cairo_set_source_rgb(cr, kInkR, kInkG, kInkB);
    cairo_set_line_width(cr, lw);
    // Neatline: thin outer rule, then a rule hugging the map.
    const double o = std::round(u) + lw / 2.0;
    cairo_rectangle(cr, o, o, w - 2 * o, h - 2 * o);
    cairo_stroke(cr);
    const double in = std::round(m - 2 * u) - lw / 2.0;
    cairo_rectangle(cr, in, in, geom_.map_w + 2 * (m - in), geom_.map_h + 2 * (m - in));
    cairo_stroke(cr);

    const double cap_top = m + geom_.map_h + std::round(2 * u);
    const double cap_h = h - cap_top - std::round(2 * u);
    const double mid = cap_top + cap_h / 2.0;
    // North arrow at the left: half inked, half paper.
    const double ax = m + std::round(5 * u);
    const double ah = cap_h * 0.36;
    cairo_set_line_width(cr, std::max(0.8, 0.8 * u));
    cairo_move_to(cr, ax, mid - ah);
    cairo_line_to(cr, ax - ah * 0.40, mid + ah * 0.8);
    cairo_line_to(cr, ax, mid + ah * 0.35);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_move_to(cr, ax, mid - ah);
    cairo_line_to(cr, ax + ah * 0.40, mid + ah * 0.8);
    cairo_line_to(cr, ax, mid + ah * 0.35);
    cairo_close_path(cr);
    cairo_stroke(cr);
    PangoLayout *small = pango_cairo_create_layout(cr);
    PangoFontDescription *small_desc = pango_font_description_from_string("DejaVu Serif Bold");
    pango_font_description_set_absolute_size(small_desc, std::max(6.0, cap_h * 0.36) * PANGO_SCALE);
    pango_layout_set_font_description(small, small_desc);
    pango_layout_set_text(small, "N", -1);
    int nw = 0, nh = 0;
    pango_layout_get_pixel_size(small, &nw, &nh);
    cairo_move_to(cr, ax + ah * 0.5, mid - ah - nh * 0.15);
    pango_cairo_show_layout(cr, small);
    const double left_used = ax + ah * 0.5 + nw + 5 * u;

    // Scale bar at the right: four alternating segments of six tiles.
    const double seg = std::max(4.0, std::round(scale_ * 6.0));
    const double bar_h = std::max(2.0, std::round(cap_h * 0.18));
    const double bx1 = w - m - std::round(2 * u);
    const double bx0 = bx1 - seg * 4;
    const double by = std::round(mid - bar_h / 2.0);
    cairo_set_line_width(cr, lw);
    for (int i = 0; i < 4; ++i) {
        const double x0 = bx0 + i * seg;
        cairo_rectangle(cr, x0 + lw / 2.0, by + lw / 2.0, seg, bar_h);
        if (i % 2 == 0) {
            cairo_fill_preserve(cr);
        }
        cairo_stroke(cr);
    }
    cairo_move_to(cr, bx0 + lw / 2.0, by - std::round(2 * u));
    cairo_line_to(cr, bx0 + lw / 2.0, by + bar_h + std::round(2 * u));
    cairo_move_to(cr, bx1 + lw / 2.0, by - std::round(2 * u));
    cairo_line_to(cr, bx1 + lw / 2.0, by + bar_h + std::round(2 * u));
    cairo_stroke(cr);

    // Title: the city's name in letter-spaced serif capitals, centred in
    // the room between the arrow and the scale bar.
    gchar *upper = g_utf8_strup(title_.c_str(), -1);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string("DejaVu Serif Bold");
    const double room_l = left_used;
    const double room_r = bx0 - 5 * u;
    const double room = std::max(10.0, room_r - room_l);
    double size = cap_h * 0.5;
    int tw = 0, th = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
        pango_layout_set_font_description(layout, desc);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(static_cast<int>(size * 0.12 * PANGO_SCALE)));
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
        pango_layout_set_text(layout, upper, -1);
        pango_layout_set_width(layout, -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        if (tw <= room || size <= cap_h * 0.30) {
            break;
        }
        size *= 0.9;
    }
    if (tw > room) {
        pango_layout_set_width(layout, static_cast<int>(room * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_get_pixel_size(layout, &tw, &th);
    }
    // Centre on the sheet when there is room, else in the free span.
    double tx = (w - tw) / 2.0;
    tx = std::max(room_l, std::min(tx, room_r - tw));
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    const double ty = mid - (ink.y + ink.height / 2.0);
    cairo_move_to(cr, std::round(tx), std::round(ty));
    pango_cairo_show_layout(cr, layout);
    g_free(upper);
    g_object_unref(layout);
    g_object_unref(small);
    pango_font_description_free(desc);
    pango_font_description_free(small_desc);
    cairo_destroy(cr);
    frame_dirty_ = false;
    timing_.frame = ms_since(start);
}

void Renderer::build_water()
{
    const auto start = Clock::now();
    ensure_spans();
    const int w = geom_.map_w;
    const int h = geom_.map_h;
    const double u = unit_;
    water_ = fresh(water_, CAIRO_FORMAT_ARGB32, w, h);
    Layer layer(w, h);
    // Smooth shoreline: blur the tile mask a little, sample it bilinearly
    // and cut at one half, about one pixel wide.
    std::vector<float> mask(static_cast<std::size_t>(kW * kH));
    for (int i = 0; i < kW * kH; ++i) {
        mask[static_cast<std::size_t>(i)] = is_water(codes_[static_cast<std::size_t>(i)]) ? 1.0f : 0.0f;
    }
    heat_field::blur(mask, kW, kH, 0.55);
    std::vector<float> cov(static_cast<std::size_t>(w * h));
    std::vector<float> dist(static_cast<std::size_t>(w * h));
    const float big = 1e6f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double v = heat_field::sample(mask, kW, kH, (x + 0.5) / scale_, (y + 0.5) / scale_);
            const double c = std::max(0.0, std::min(1.0, (v - 0.5) * scale_ * 1.3 + 0.5));
            cov[static_cast<std::size_t>(y * w + x)] = static_cast<float>(c);
            dist[static_cast<std::size_t>(y * w + x)] = c > 0.5 ? big : 0.0f;
        }
    }
    // Two-pass chamfer distance to the nearest land pixel.
    const float d1 = 1.0f, d2 = 1.4142f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float &d = dist[static_cast<std::size_t>(y * w + x)];
            if (d == 0.0f) {
                continue;
            }
            // Off the map edge counts as more water, so the border has no rim.
            float best = d;
            if (x > 0) best = std::min(best, dist[static_cast<std::size_t>(y * w + x - 1)] + d1);
            if (y > 0) best = std::min(best, dist[static_cast<std::size_t>((y - 1) * w + x)] + d1);
            if (x > 0 && y > 0) best = std::min(best, dist[static_cast<std::size_t>((y - 1) * w + x - 1)] + d2);
            if (x + 1 < w && y > 0) best = std::min(best, dist[static_cast<std::size_t>((y - 1) * w + x + 1)] + d2);
            d = best;
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            float &d = dist[static_cast<std::size_t>(y * w + x)];
            if (d == 0.0f) {
                continue;
            }
            float best = d;
            if (x + 1 < w) best = std::min(best, dist[static_cast<std::size_t>(y * w + x + 1)] + d1);
            if (y + 1 < h) best = std::min(best, dist[static_cast<std::size_t>((y + 1) * w + x)] + d1);
            if (x + 1 < w && y + 1 < h) best = std::min(best, dist[static_cast<std::size_t>((y + 1) * w + x + 1)] + d2);
            if (x > 0 && y + 1 < h) best = std::min(best, dist[static_cast<std::size_t>((y + 1) * w + x - 1)] + d2);
            d = best;
        }
    }
    const double spacing = 2.0 * u;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * w + x);
            const double c = cov[i];
            if (c <= 0.0) {
                continue;
            }
            // A cool grey wash so water reads as water at a glance.
            layer.over(x, y, 0xc9 / 255.0, 0xd0 / 255.0, 0xcf / 255.0, 0.5 * c);
            const double d = dist[i] / u;
            double ink = 0.0;
            if (c < 0.98 || d <= 1.01) {
                ink = std::max(ink, 0.95 * (c < 0.98 ? std::min(1.0, c * 2.0) : 1.0)); // shore rule
            }
            auto ring = [&](double at, double strength) {
                const double t = 1.0 - std::fabs(d - at) / 0.75;
                if (t > 0) {
                    ink = std::max(ink, std::min(1.0, t) * strength);
                }
            };
            ring(3.0, 0.7);
            if (d > 4.5) {
                // Wavy lines, phased to the pixel grid so they stay crisp.
                const double wave = std::sin((x / u) / 2.6) * 0.55 * u;
                const double phase = std::fmod(y + wave + 1000.0 * spacing, spacing);
                const double off = std::min(phase, spacing - phase);
                const double t = 0.5 * u + 0.5 - off;
                if (t > 0) {
                    ink = std::max(ink, std::min(1.0, t) * 0.5);
                }
            }
            layer.over(x, y, kWaterInkR, kWaterInkG, kWaterInkB, ink * c);
        }
    }
    layer.store(water_);
    water_dirty_ = false;
    ++counts_.water;
    timing_.water = ms_since(start);
}

void Renderer::build_zones()
{
    const auto start = Clock::now();
    ensure_spans();
    const int w = geom_.map_w;
    const int h = geom_.map_h;
    const double u = unit_;
    zones_ = fresh(zones_, CAIRO_FORMAT_ARGB32, w, h);
    Layer layer(w, h);
    // Woods: smooth mask, then ink stipple.
    std::vector<float> woods(static_cast<std::size_t>(kW * kH));
    for (int i = 0; i < kW * kH; ++i) {
        woods[static_cast<std::size_t>(i)] = is_woods(codes_[static_cast<std::size_t>(i)]) ? 1.0f : 0.0f;
    }
    heat_field::blur(woods, kW, kH, 0.5);
    const int cell = std::max(1, static_cast<int>(std::lround(u)));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double v = heat_field::sample(woods, kW, kH, (x + 0.5) / scale_, (y + 0.5) / scale_);
            const double c = std::max(0.0, std::min(1.0, (v - 0.45) * scale_ * 1.3 + 0.5));
            if (c <= 0.0) {
                continue;
            }
            const int cx = x / cell, cy = y / cell;
            const double r = unit_hash(cx, cy, 0x7ee5u);
            if (r < 0.16) {
                layer.over(x, y, 0x3b / 255.0, 0x40 / 255.0, 0x21 / 255.0, 0.9 * c);
            } else if (r < 0.19) {
                layer.over(x, y, 0x3b / 255.0, 0x40 / 255.0, 0x21 / 255.0, 0.45 * c);
            }
        }
    }
    // Zones. Each kind has its own hatch, phased to the pixel grid so the
    // pattern stays crisp at minimap size: homes a light diagonal, shops
    // horizontal rules, industry a dark cross-hatch. Empty lots are faint.
    std::vector<float> built(static_cast<std::size_t>(w * h), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cx = x / cell, cy = y / cell;
            const float res = cover(x, y, is_res);
            const float com = cover(x, y, is_com);
            const float ind = cover(x, y, is_ind);
            const float civ = cover(x, y, is_civic);
            const float any = cover(x, y, is_built);
            built[static_cast<std::size_t>(y * w + x)] = any;
            if (any <= 0.0f) {
                continue;
            }
            // How much of this pixel is an empty lot.
            const float empty = cover(x, y, [](std::uint8_t c) { return (c & kEmptyLot) != 0; });
            const double lot = any > 0.0f ? 1.0 - 0.6 * std::min(1.0f, empty / std::max(0.01f, any)) : 1.0;
            if (res > 0.0f) {
                const bool on = ((cx + cy) % 3) == 0;
                layer.over(x, y, kInkR, kInkG, kInkB, on ? 0.5 * res * lot : 0.0);
            }
            if (com > 0.0f) {
                const bool on = (cy % 2) == 0;
                layer.over(x, y, kInkR, kInkG, kInkB, on ? 0.72 * com * lot : 0.0);
            }
            if (ind > 0.0f) {
                const bool on = ((cx + cy) % 3) == 0 || (((cx - cy) % 3) + 3) % 3 == 0;
                layer.over(x, y, kInkR, kInkG, kInkB, on ? 0.95 * ind * lot : 0.3 * ind * lot);
            }
            if (civ > 0.0f) {
                layer.over(x, y, kInkR, kInkG, kInkB, 0.86 * civ);
            }
            const float ruin = cover(x, y, is_ruin);
            if (ruin > 0.0f && unit_hash(cx, cy, 0x2b1du) < 0.3) {
                layer.over(x, y, 0x5a / 255.0, 0x46 / 255.0, 0x32 / 255.0, 0.8 * ruin);
            }
        }
    }
    // Block outlines: the rim of built land.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (built[static_cast<std::size_t>(y * w + x)] < 0.5f) {
                continue;
            }
            auto out = [&](int xx, int yy) {
                return xx < 0 || yy < 0 || xx >= w || yy >= h || built[static_cast<std::size_t>(yy * w + xx)] < 0.5f;
            };
            if (out(x - 1, y) || out(x + 1, y) || out(x, y - 1) || out(x, y + 1)) {
                layer.over(x, y, kInkR, kInkG, kInkB, 0.5);
            }
        }
    }
    layer.store(zones_);

    // Landmarks: double-ruled squares. 1x1 symbols: a ringed dot.
    cairo_t *cr = cairo_create(zones_);
    cairo_set_source_rgb(cr, kInkR, kInkG, kInkB);
    const double lw = std::max(1.0, std::round(u));
    cairo_set_line_width(cr, lw);
    for (int ty = 0; ty < kH; ++ty) {
        for (int tx = 0; tx < kW; ++tx) {
            const std::uint8_t code = codes_[static_cast<std::size_t>(ty * kW + tx)];
            if ((code & kCentre) == 0) {
                continue;
            }
            if ((code & kClsMask) == Landmark) {
                const double x0 = std::round((tx - 1) * scale_);
                const double y0 = std::round((ty - 1) * scale_);
                const double x1 = std::round((tx + 2) * scale_);
                const double y1 = std::round((ty + 2) * scale_);
                // Clear what lies under it, then the two rules and a dot.
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_rgba(cr, kStreetR, kStreetG, kStreetB, 0.92);
                cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
                cairo_fill(cr);
                cairo_restore(cr);
                cairo_rectangle(cr, x0 + lw / 2.0, y0 + lw / 2.0, x1 - x0 - lw, y1 - y0 - lw);
                cairo_stroke(cr);
                const double inset = lw * 2.0;
                if (x1 - x0 - 2 * inset >= lw * 2.0) {
                    cairo_rectangle(cr, x0 + inset + lw / 2.0, y0 + inset + lw / 2.0, x1 - x0 - 2 * inset - lw,
                                    y1 - y0 - 2 * inset - lw);
                    cairo_stroke(cr);
                } else {
                    const double c = std::round((x0 + x1) / 2.0 - lw / 2.0);
                    const double d = std::round((y0 + y1) / 2.0 - lw / 2.0);
                    cairo_rectangle(cr, c, d, lw, lw);
                    cairo_fill(cr);
                }
            } else if ((code & kClsMask) == Symbol) {
                const double cx = (tx + 0.5) * scale_;
                const double cy = (ty + 0.5) * scale_;
                cairo_arc(cr, cx, cy, std::max(1.2, 1.25 * u), 0, 2 * kPi);
                cairo_fill(cr);
                cairo_save(cr);
                cairo_set_line_width(cr, std::max(0.6, 0.6 * u));
                cairo_arc(cr, cx, cy, std::max(2.2, 2.3 * u), 0, 2 * kPi);
                cairo_stroke(cr);
                cairo_restore(cr);
            }
        }
    }
    cairo_destroy(cr);
    zones_dirty_ = false;
    ++counts_.zones;
    timing_.zones = ms_since(start);
}

void Renderer::build_roads()
{
    const auto start = Clock::now();
    const int w = geom_.map_w;
    const int h = geom_.map_h;
    const double u = unit_;
    roads_ = fresh(roads_, CAIRO_FORMAT_ARGB32, w, h);
    clear_surface(roads_);
    cairo_t *cr = cairo_create(roads_);
    // Tile centres snapped to the pixel grid so the double lines stay crisp.
    const double lw = std::max(1.0, std::round(u));
    auto centre = [&](int t) { return std::floor((t + 0.5) * scale_) + (std::fmod(lw, 2.0) == 1.0 ? 0.5 : 0.0); };
    auto add_path = [&](bool (*want)(std::uint8_t), bool (*joins)(std::uint8_t)) {
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                if (!want(codes_[static_cast<std::size_t>(y * kW + x)])) {
                    continue;
                }
                const double cx = centre(x), cy = centre(y);
                bool linked = false;
                if (x + 1 < kW && joins(codes_[static_cast<std::size_t>(y * kW + x + 1)])) {
                    cairo_move_to(cr, cx, cy);
                    cairo_line_to(cr, centre(x + 1), cy);
                    linked = true;
                }
                if (y + 1 < kH && joins(codes_[static_cast<std::size_t>((y + 1) * kW + x)])) {
                    cairo_move_to(cr, cx, cy);
                    cairo_line_to(cr, cx, centre(y + 1));
                    linked = true;
                }
                if (!linked) {
                    const bool left = x > 0 && joins(codes_[static_cast<std::size_t>(y * kW + x - 1)]);
                    const bool up = y > 0 && joins(codes_[static_cast<std::size_t>((y - 1) * kW + x)]);
                    if (!left && !up) {
                        cairo_move_to(cr, cx - scale_ * 0.4, cy);
                        cairo_line_to(cr, cx + scale_ * 0.4, cy);
                    }
                }
            }
        }
    };
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    // Power lines first, faint and dotted, under everything else.
    add_path(is_wire, [](std::uint8_t c) { return is_wire(c) || is_street(c) || is_rail(c); });
    const double dots[2] = {lw, lw * 1.5};
    cairo_set_dash(cr, dots, 2, 0);
    cairo_set_line_width(cr, lw * 0.6);
    cairo_set_source_rgba(cr, kInkR, kInkG, kInkB, 0.4);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
    // Streets: an ink stroke three units wide with a one-unit paper core.
    add_path(is_street, is_street);
    cairo_set_line_width(cr, lw * 3.0);
    cairo_set_source_rgba(cr, kInkR, kInkG, kInkB, 0.62);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, lw);
    cairo_set_source_rgb(cr, kStreetR, kStreetG, kStreetB);
    cairo_stroke(cr);
    // Rail: a fine line with cross ties.
    add_path(is_rail, [](std::uint8_t c) { return is_rail(c); });
    cairo_set_line_width(cr, lw);
    cairo_set_source_rgba(cr, 0x12 / 255.0, 0x0c / 255.0, 0x08 / 255.0, 0.85);
    cairo_stroke_preserve(cr);
    const double ties[2] = {lw, lw * 1.5};
    cairo_set_dash(cr, ties, 2, 0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_width(cr, lw * 2.5);
    cairo_set_source_rgba(cr, 0x12 / 255.0, 0x0c / 255.0, 0x08 / 255.0, 0.45);
    cairo_stroke(cr);
    cairo_destroy(cr);
    roads_dirty_ = false;
    ++counts_.roads;
    timing_.roads = ms_since(start);
}

void Renderer::build_plate()
{
    const auto start = Clock::now();
    ensure_spans();
    const int w = geom_.map_w;
    const int h = geom_.map_h;
    const double u = unit_;
    plate_layer_ = fresh(plate_layer_, CAIRO_FORMAT_ARGB32, w, h);
    Layer layer(w, h);
    // Fade the base map toward paper so the plate reads.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            layer.over(x, y, kPaperR, kPaperG, kPaperB, 0.58);
        }
    }
    const std::vector<float> &v = plate_values_;
    const bool have = v.size() == static_cast<std::size_t>(kW * kH);
    const int cell = std::max(1, static_cast<int>(std::lround(u)));
    auto value_cover = [&](int px, int py, float want) {
        float sum = 0.0f;
        for (const Span &sy : yspans_[static_cast<std::size_t>(py)]) {
            for (const Span &sx : xspans_[static_cast<std::size_t>(px)]) {
                if (v[static_cast<std::size_t>(sy.tile * kW + sx.tile)] == want) {
                    sum += sx.weight * sy.weight;
                }
            }
        }
        return sum;
    };
    struct Wash {
        float code;
        double r, g, b, a;
    };
    // Colours match the big map's overlay legends.
    static const Wash kPower[] = {{3, 0.98, 0.84, 0.16, 0.55}, {4, 0.94, 0.55, 0.09, 0.95}};
    static const Wash kWater[] = {{1, 0.16, 0.43, 0.90, 0.6}};
    static const Wash kZones[] = {{1, 0.16, 0.67, 0.19, 0.6}, {2, 0.19, 0.38, 0.82, 0.6}, {3, 0.90, 0.74, 0.13, 0.65},
                                  {4, 0.19, 0.19, 0.20, 0.7}, {5, 0.55, 0.33, 0.14, 0.75}, {8, 0.16, 0.43, 0.90, 0.5}};
    static const Wash kRes[] = {{1, 0.16, 0.67, 0.19, 0.65}};
    static const Wash kCom[] = {{1, 0.19, 0.38, 0.82, 0.65}};
    static const Wash kInd[] = {{1, 0.90, 0.74, 0.13, 0.7}};
    static const Wash kTransport[] = {{1, 0.19, 0.19, 0.20, 0.75}, {2, 0.55, 0.33, 0.14, 0.8}};
    const Wash *washes = nullptr;
    std::size_t wash_count = 0;
    auto use = [&](const Wash *table, std::size_t n) {
        washes = table;
        wash_count = n;
    };
    switch (plate_) {
    case Plate::Power:
        use(kPower, 2);
        break;
    case Plate::Water:
        use(kWater, 1);
        break;
    case Plate::Zones:
        use(kZones, 6);
        break;
    case Plate::Res:
        use(kRes, 1);
        break;
    case Plate::Com:
        use(kCom, 1);
        break;
    case Plate::Ind:
        use(kInd, 1);
        break;
    case Plate::Transport:
        use(kTransport, 2);
        break;
    default:
        break;
    }
    if (have && wash_count > 0) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (std::size_t k = 0; k < wash_count; ++k) {
                    const Wash &wash = washes[k];
                    const float c = value_cover(x, y, wash.code);
                    if (c > 0.0f) {
                        layer.over(x, y, wash.r, wash.g, wash.b, wash.a * c);
                    }
                }
                if (plate_ == Plate::Power) {
                    // No power: a red cross-hatch, like the industrial hatch.
                    const float c = value_cover(x, y, 2);
                    if (c > 0.0f) {
                        const int cx = x / cell, cy = y / cell;
                        const bool on = ((cx + cy) % 2) == 0;
                        layer.over(x, y, 0.80, 0.13, 0.09, (on ? 0.95 : 0.35) * c);
                    }
                }
            }
        }
    }
    if (have && (plate_ == Plate::Heat || plate_ == Plate::Growth)) {
        // Heat: red stipple whose density follows the value, plus ink
        // contour lines between five bands, like an engraver's survey.
        std::vector<float> field = v;
        heat_field::blur(field, kW, kH, 1.5);
        std::vector<float> fine(static_cast<std::size_t>(w * h));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                fine[static_cast<std::size_t>(y * w + x)] =
                    heat_field::sample(field, kW, kH, (x + 0.5) / scale_, (y + 0.5) / scale_);
            }
        }
        auto band = [](float t) { return static_cast<int>(std::floor(std::min(0.999f, std::fabs(t)) * 5.0f)); };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float t = fine[static_cast<std::size_t>(y * w + x)];
                const float a = std::fabs(t);
                if (a < 0.08f) {
                    continue;
                }
                const bool cool = t < 0.0f;
                const double r = cool ? 0.13 : 0.80, g = cool ? 0.35 : 0.13, b = cool ? 0.80 : 0.09;
                layer.over(x, y, r, g, b, 0.10 + 0.34 * a);
                const int cx = x / cell, cy = y / cell;
                if (unit_hash(cx, cy, 0x51a7u) < 0.55 * a * a) {
                    layer.over(x, y, r * 0.75, g * 0.75, b * 0.75, 0.85);
                }
                const int here = band(t);
                const float right = x + 1 < w ? fine[static_cast<std::size_t>(y * w + x + 1)] : t;
                const float below = y + 1 < h ? fine[static_cast<std::size_t>((y + 1) * w + x)] : t;
                if (here > 0 && (band(right) != here || band(below) != here)) {
                    layer.over(x, y, r * 0.55, g * 0.45, b * 0.45, 0.75);
                }
            }
        }
    }
    layer.store(plate_layer_);
    plate_dirty_ = false;
    ++counts_.plate;
    timing_.plate = ms_since(start);
}

void Renderer::compose()
{
    if (geom_.map_w <= 1 || paper_ == nullptr) {
        return;
    }
    if (!have_codes_) {
        return;
    }
    if (frame_dirty_) {
        build_frame();
        compose_dirty_ = true;
    }
    if (water_dirty_) {
        build_water();
        compose_dirty_ = true;
    }
    if (zones_dirty_) {
        build_zones();
        compose_dirty_ = true;
    }
    if (roads_dirty_) {
        build_roads();
        compose_dirty_ = true;
    }
    if (plate_ != Plate::None && (plate_dirty_ || plate_layer_ == nullptr)) {
        build_plate();
        compose_dirty_ = true;
    }
    if (!compose_dirty_ && sheet_ != nullptr) {
        return;
    }
    const auto start = Clock::now();
    const int w = geom_.sheet_w();
    const int h = geom_.sheet_h();
    sheet_ = fresh(sheet_, CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(sheet_);
    cairo_set_source_surface(cr, frame_, 0, 0);
    cairo_paint(cr);
    const double m = geom_.margin;
    for (cairo_surface_t *layer : {water_, zones_, roads_}) {
        cairo_set_source_surface(cr, layer, m, m);
        cairo_paint(cr);
    }
    if (plate_ != Plate::None && plate_layer_ != nullptr) {
        cairo_set_source_surface(cr, plate_layer_, m, m);
        cairo_paint(cr);
    }
    if (night_) {
        // Paper by lamplight: warm, dimmer toward the corners.
        cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
        cairo_pattern_t *lamp = cairo_pattern_create_radial(w * 0.35, h * 0.3, 0, w * 0.35, h * 0.3, std::max(w, h) * 0.9);
        cairo_pattern_add_color_stop_rgb(lamp, 0.0, 0.86, 0.76, 0.62);
        cairo_pattern_add_color_stop_rgb(lamp, 1.0, 0.62, 0.54, 0.44);
        cairo_set_source(cr, lamp);
        cairo_paint(cr);
        cairo_pattern_destroy(lamp);
    }
    cairo_destroy(cr);
    cairo_surface_flush(sheet_);
    compose_dirty_ = false;
    ++counts_.compose;
    timing_.compose = ms_since(start);
}

} // namespace engraved

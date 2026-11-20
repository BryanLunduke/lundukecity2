// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

// Engraved minimap: sheet geometry and click mapping with the caption,
// tile classes, which layers a change redraws, the rebuild throttle,
// determinism, night, overlay plates, and timings.

#include "engraved_map.hpp"
#include "ui_scale.hpp"

#include "micropolis.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using engraved::kH;
using engraved::kW;

// A small town: a river, a street grid with zones, rail, woods, a power
// line, a civic block, a landmark (Data Center) and a micro plant.
std::vector<int> sample_city()
{
    std::vector<int> raw(static_cast<std::size_t>(kW * kH), DIRT);
    auto at = [&](int x, int y) -> int & { return raw[static_cast<std::size_t>(y * kW + x)]; };
    for (int y = 0; y < kH; ++y) {
        for (int x = 54; x < 62; ++x) {
            at(x, y) = RIVER;
        }
    }
    for (int y = 10; y < 90; y += 4) {
        for (int x = 10; x < 110; ++x) {
            at(x, y) = (x >= 54 && x < 62) ? HBRIDGE : ROADS;
        }
    }
    for (int x = 10; x < 110; x += 4) {
        if (x >= 54 && x < 62) {
            continue;
        }
        for (int y = 10; y < 90; ++y) {
            at(x, y) = (at(x, y) == ROADS) ? INTERSECTION : ROADS2;
        }
    }
    for (int by = 11; by < 88; by += 4) {
        for (int bx = 11; bx < 108; bx += 4) {
            if (bx >= 51 && bx < 63) {
                continue;
            }
            int base = RZB;
            if (bx > 70) {
                base = CZB;
            }
            if (by > 60) {
                base = IZB;
            }
            for (int dy = 0; dy < 3; ++dy) {
                for (int dx = 0; dx < 3; ++dx) {
                    at(bx + dx, by + dy) = base - 4 + dy * 3 + dx;
                }
            }
        }
    }
    for (int x = 0; x < kW; ++x) {
        at(x, 95) = LHRAIL;
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 40; ++x) {
            at(x, y) = WOODS;
        }
    }
    for (int x = 70; x < 110; ++x) {
        at(x, 92) = LHPOWER;
    }
    // Data Center at (31..33, 23..25), micro plant at (35, 22).
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            at(31 + dx, 23 + dy) = SPECIAL_BODY;
        }
    }
    at(32, 24) = SPECIAL_DATACENTER | ZONEBIT;
    at(35, 22) = SPECIAL_MICRONUKE | ZONEBIT;
    // A fire station block.
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            at(43 + dx, 43 + dy) = FIRESTBASE + dy * 3 + dx;
        }
    }
    return raw;
}

double mean_luma(cairo_surface_t *s)
{
    cairo_surface_flush(s);
    const unsigned char *d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    const int w = cairo_image_surface_get_width(s);
    const int h = cairo_image_surface_get_height(s);
    double sum = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = d + y * stride + x * 4;
            sum += 0.11 * p[0] + 0.59 * p[1] + 0.3 * p[2];
        }
    }
    return sum / (w * h);
}

bool same_pixels(cairo_surface_t *a, cairo_surface_t *b)
{
    cairo_surface_flush(a);
    cairo_surface_flush(b);
    const int h = cairo_image_surface_get_height(a);
    const int stride = cairo_image_surface_get_stride(a);
    if (h != cairo_image_surface_get_height(b) || stride != cairo_image_surface_get_stride(b)) {
        return false;
    }
    return std::memcmp(cairo_image_surface_get_data(a), cairo_image_surface_get_data(b),
                       static_cast<std::size_t>(stride * h)) == 0;
}

double now_ms()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

int main()
{
    // Geometry: 100% and 200% UI scale.
    {
        const engraved::SheetGeom g = engraved::sheet_geom(199.2, 166.0, 1.0);
        check(g.map_w == 199 && g.map_h == 166, "map area is the land box, floored");
        check(g.margin == 6 && g.caption == 24 && g.below() == 30, "1x margin and caption");
        check(g.sheet_w() == 211 && g.sheet_h() == 202, "1x sheet size");
        const engraved::SheetGeom g2 = engraved::sheet_geom(398.4, 332.0, 2.0);
        check(g2.margin == 12 && g2.caption == 48 && g2.below() == 60, "2x margin and caption scale");
    }
    // Click mapping: the caption strip and the margin are not map.
    {
        double fx = -1, fy = -1;
        check(engraved::land_fraction(0.0, 0.0, 199.0, 166.0, fx, fy) && fx == 0.0 && fy == 0.0, "top left maps to 0,0");
        check(engraved::land_fraction(99.5, 83.0, 199.0, 166.0, fx, fy) && fx == 0.5 && fy == 0.5, "centre maps to 0.5");
        check(!engraved::land_fraction(100.0, 166.0 + 12.0, 199.0, 166.0, fx, fy), "caption strip is not map");
        check(!engraved::land_fraction(-3.0, 50.0, 199.0, 166.0, fx, fy), "left margin is not map");
        check(!engraved::land_fraction(199.0, 50.0, 199.0, 166.0, fx, fy), "right edge is exclusive");
    }
    // Tile classes.
    {
        check((engraved::classify_tile(RIVER) & engraved::kClsMask) == engraved::Water, "river is water");
        check((engraved::classify_tile(ROADS) & engraved::kClsMask) == engraved::Road, "road");
        check((engraved::classify_tile(LTRFBASE + 2) & engraved::kClsMask) == engraved::Road, "traffic road");
        check((engraved::classify_tile(HBRIDGE) & engraved::kClsMask) == engraved::Bridge, "bridge");
        check((engraved::classify_tile(LHRAIL) & engraved::kClsMask) == engraved::Rail, "rail");
        check((engraved::classify_tile(FREEZ) & engraved::kEmptyLot) != 0, "empty residential lot");
        check((engraved::classify_tile(RZB) & engraved::kClsMask) == engraved::Res, "house zone");
        check((engraved::classify_tile(CZB) & engraved::kClsMask) == engraved::Com, "commercial");
        check((engraved::classify_tile(IZB) & engraved::kClsMask) == engraved::Ind, "industrial");
        check((engraved::classify_tile(POLICESTATION) & engraved::kClsMask) == engraved::Civic, "police is civic");
        check((engraved::classify_tile(NUCLEAR) & engraved::kClsMask) == engraved::Civic, "plant is solid ink");
        check((engraved::classify_tile(SPECIAL_DATACENTER) & engraved::kClsMask) == engraved::Landmark,
              "Data Center is double ruled");
        check((engraved::classify_tile(SPECIAL_MICRONUKE) & engraved::kClsMask) == engraved::Symbol,
              "micro plant is a symbol");
        check((engraved::classify_tile(WOODS) & engraved::kClsMask) == engraved::Woods, "woods");
    }
    // Which layers a change redraws.
    {
        const std::vector<int> base = sample_city();
        const std::vector<std::uint8_t> a = engraved::classify_map(base.data());
        std::vector<int> traffic = base;
        traffic[static_cast<std::size_t>(10 * kW + 20)] = HTRFBASE + 2;
        traffic[static_cast<std::size_t>(10 * kW + 21)] |= PWRBIT;
        check(!engraved::compare(a, engraved::classify_map(traffic.data())).any(), "traffic and power redraw nothing");
        std::vector<int> road = base;
        road[static_cast<std::size_t>(50 * kW + 112)] = ROADS;
        const engraved::Dirty dr = engraved::compare(a, engraved::classify_map(road.data()));
        check(dr.roads && !dr.zones && !dr.water, "a new road redraws streets only");
        std::vector<int> zone = base;
        zone[static_cast<std::size_t>(50 * kW + 112)] = RZB;
        const engraved::Dirty dz = engraved::compare(a, engraved::classify_map(zone.data()));
        check(dz.zones && !dz.roads && !dz.water, "a new zone redraws zones only");
        std::vector<int> fill = base;
        fill[static_cast<std::size_t>(51 * kW + 56)] = DIRT;
        const engraved::Dirty dw = engraved::compare(a, engraved::classify_map(fill.data()));
        check(dw.water && !dw.roads && !dw.zones, "landfill redraws water only");
    }
    // Throttle.
    {
        engraved::RebuildGate gate(1.25);
        check(!gate.due(0.0), "nothing pending, nothing due");
        gate.mark();
        check(gate.due(0.0), "first change is drawn at once");
        gate.mark();
        check(!gate.due(0.5), "a change within the interval waits");
        check(gate.pending(), "and stays pending");
        check(gate.due(1.3), "then is drawn once the interval passed");
        check(!gate.due(5.0), "and is not drawn twice");
    }
    // Renderer: layer invalidation counts.
    const std::vector<int> city = sample_city();
    {
        engraved::Renderer r;
        r.set_geometry(199, 166, 1.0, "Testville");
        r.update_tiles(city.data());
        r.compose();
        check(r.sheet() != nullptr, "sheet built");
        check(cairo_image_surface_get_width(r.sheet()) == 211 && cairo_image_surface_get_height(r.sheet()) == 202,
              "sheet has the margin and caption");
        const engraved::Renderer::Counts c0 = r.counts();
        check(c0.paper == 1 && c0.water == 1 && c0.zones == 1 && c0.roads == 1 && c0.compose == 1, "first build");
        r.update_tiles(city.data());
        r.compose();
        r.compose();
        const engraved::Renderer::Counts c1 = r.counts();
        check(c1.water == 1 && c1.zones == 1 && c1.roads == 1 && c1.compose == 1, "unchanged map composes nothing");
        std::vector<int> road = city;
        road[static_cast<std::size_t>(50 * kW + 112)] = ROADS;
        r.update_tiles(road.data());
        r.compose();
        const engraved::Renderer::Counts c2 = r.counts();
        check(c2.roads == 2 && c2.zones == 1 && c2.water == 1 && c2.compose == 2, "a road rebuilds one layer");
        r.set_night(true);
        r.compose();
        check(r.counts().compose == 3 && r.counts().roads == 2, "night recomposes without redrawing layers");
        r.set_geometry(199, 166, 1.0, "Testville");
        r.compose();
        check(r.counts().compose == 3 && r.counts().paper == 1, "same geometry keeps the cache");
        r.set_geometry(199, 166, 1.0, "Renamed");
        r.compose();
        check(r.counts().compose == 4 && r.counts().paper == 1, "a new name redraws only the caption");
    }
    // Determinism and night.
    {
        engraved::Renderer a, b, n;
        for (engraved::Renderer *r : {&a, &b, &n}) {
            r->set_geometry(199, 166, 1.0, "Testville");
            r->update_tiles(city.data());
        }
        n.set_night(true);
        a.compose();
        b.compose();
        n.compose();
        check(same_pixels(a.sheet(), b.sheet()), "fixed seed: two renders are identical");
        check(mean_luma(n.sheet()) < mean_luma(a.sheet()) * 0.85, "night darkens the paper");
    }
    // Plates.
    {
        engraved::Renderer r;
        r.set_geometry(199, 166, 1.0, "Testville");
        r.update_tiles(city.data());
        r.compose();
        const double plain = mean_luma(r.sheet());
        std::vector<float> power(static_cast<std::size_t>(kW * kH), 0.0f);
        for (std::size_t i = 0; i < power.size(); ++i) {
            power[i] = (i % 7 == 0) ? 2.0f : 3.0f;
        }
        r.set_plate(engraved::Plate::Power, power);
        r.compose();
        check(r.counts().plate == 1 && engraved::plate_is_red(r.plate()), "power plate drawn, frame goes dark");
        r.set_plate(engraved::Plate::Power, power);
        r.compose();
        check(r.counts().plate == 1, "same plate values are not redrawn");
        std::vector<float> heat(static_cast<std::size_t>(kW * kH), 0.0f);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                heat[static_cast<std::size_t>(y * kW + x)] = x / static_cast<float>(kW);
            }
        }
        r.set_plate(engraved::Plate::Heat, heat);
        r.compose();
        check(r.counts().plate == 2, "heat plate drawn");
        check(std::fabs(mean_luma(r.sheet()) - plain) > 1.0, "plate changes the picture");
        r.clear_plate();
        r.compose();
        check(!engraved::plate_is_red(r.plate()), "plate cleared");
    }
    // Settings round trip for the minimap toggle.
    {
        ui_scale::StoredSettings s;
        s.minimap = 0;
        const ui_scale::StoredSettings back = ui_scale::settings_parse(ui_scale::settings_text(s));
        check(back.minimap == 0, "minimap=0 (Classic) persists");
        check(ui_scale::settings_parse("look=1\n").minimap == -1, "absent minimap means default (engraved)");
    }
    // Timings (informational).
    for (double unit : {1.0, 2.0}) {
        const int w = static_cast<int>(199 * unit);
        const int h = static_cast<int>(166 * unit);
        engraved::Renderer r;
        r.set_geometry(w, h, unit, "Testville");
        r.update_tiles(city.data());
        r.compose();
        const engraved::Renderer::Timing &t = r.timing();
        cairo_surface_t *target = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w + 100, h + 100);
        cairo_t *cr = cairo_create(target);
        const double start = now_ms();
        const int frames = 500;
        for (int i = 0; i < frames; ++i) {
            r.compose();
            cairo_set_source_surface(cr, r.sheet(), 10, 10);
            cairo_paint(cr);
        }
        const double frame = (now_ms() - start) / frames;
        cairo_destroy(cr);
        cairo_surface_destroy(target);
        std::printf("engraved %gx %dx%d: paper %.2f frame %.2f water %.2f zones %.2f roads %.2f compose %.2f ms; "
                    "per-frame blit %.3f ms\n",
                    unit, w, h, t.paper, t.frame, t.water, t.zones, t.roads, t.compose, frame);
        std::vector<float> power(static_cast<std::size_t>(engraved::kW * engraved::kH), 0.0f);
        std::vector<float> heat(power.size(), 0.0f);
        for (std::size_t i = 0; i < power.size(); ++i) {
            power[i] = static_cast<float>(2 + i % 3);
            heat[i] = static_cast<float>((i * 37) % 100) / 100.0f;
        }
        r.set_plate(engraved::Plate::Power, power);
        r.compose();
        const double power_ms = r.timing().plate;
        r.set_plate(engraved::Plate::Heat, heat);
        r.set_night(true);
        r.compose();
        std::printf("engraved %gx plates: power %.2f heat %.2f ms; night compose %.2f ms\n", unit, power_ms,
                    r.timing().plate, r.timing().compose);
    }
    if (failures == 0) {
        std::printf("engraved_minimap: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}

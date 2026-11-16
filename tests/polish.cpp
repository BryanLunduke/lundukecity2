// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Pure checks for the Slate skin work: the grouped toolbar layout, the
// Inspect card verdicts, the smooth heat field, the night light layer and
// the handheld layout switch. No window, no SDL.

#include "heat_field.hpp"
#include "inspect_report.hpp"
#include "layout_mode.hpp"
#include "night_light.hpp"
#include "tool_groups.hpp"
#include "ui_scale.hpp"

#include "micropolis.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

bool has_reason(const inspect_report::Report &report, const std::string &start)
{
    for (const std::string &reason : report.reasons) {
        if (reason.rfind(start, 0) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    // Grouped toolbar: every button index is reachable, in order, and a
    // click on a header strip selects nothing.
    {
        int covered = 0;
        int last = -1;
        for (int g = 0; g < tool_groups::kGroupCount; ++g) {
            const tool_groups::Group &group = tool_groups::groups()[g];
            check(group.first == last + 1, "tool groups are contiguous");
            last = group.first + group.count - 1;
            covered += group.count;
        }
        check(covered == 16, "tool groups cover the 16 toolbar buttons");
        for (int columns = 2; columns <= 3; ++columns) {
            const int cell = 40;
            const int header = 18;
            for (int i = 0; i < 16; ++i) {
                int x = 0;
                int y = 0;
                check(tool_groups::slot(i, columns, cell, header, x, y), "every tool has a slot");
                check(tool_groups::index_at(x + cell / 2, y + cell / 2, columns, cell, header) == i,
                      "the middle of a slot hits that tool");
            }
            check(tool_groups::index_at(5, 5, columns, cell, header) == -1, "a header is not a button");
            check(tool_groups::index_at(columns * cell + 1, header + 5, columns, cell, header) == -1,
                  "right of the grid is not a button");
            check(tool_groups::grid_height(columns, cell, header) >= 16 / columns * cell + 5 * header,
                  "grid height includes every header");
        }
        int x = 0;
        int y = 0;
        tool_groups::slot(4, 3, 40, 18, x, y);
        check(x == 40 && y == 18 + 40, "Park sits on the second row of BUILD with three columns");
        check(tool_groups::index_at(80 + 20, 18 + 40 + 20, 3, 40, 18) == -1, "the empty end of a short row is not a button");
    }

    // Inspect card verdicts.
    {
        using inspect_report::Facts;
        using inspect_report::Kind;
        using inspect_report::Tone;
        Facts growing;
        growing.kind = Kind::Residential;
        growing.power = 1;
        growing.access = true;
        growing.growth = 40;
        growing.demand = 0.5;
        growing.land_value = 120;
        const inspect_report::Report good = inspect_report::make(growing);
        check(good.status == "Growing" && good.tone == Tone::Good, "a powered, connected growing zone reads Growing");
        check(good.reasons.empty(), "a healthy growing zone lists no problems");

        Facts stuck = growing;
        stuck.power = 0;
        stuck.access = false;
        stuck.growth = 0;
        stuck.demand = -0.2;
        stuck.pollution = 200;
        stuck.crime = 180;
        stuck.land_value = 10;
        const inspect_report::Report bad = inspect_report::make(stuck);
        check(bad.tone == Tone::Bad, "an unpowered zone is a bad verdict");
        check(has_reason(bad, "No power"), "no power is listed");
        check(has_reason(bad, "No road"), "no road is listed");
        check(has_reason(bad, "Little residential demand"), "low demand is listed");
        check(has_reason(bad, "Heavy pollution"), "pollution is listed");
        check(has_reason(bad, "High crime"), "crime is listed");
        check(has_reason(bad, "Low land value"), "land value is listed");

        Facts factory = stuck;
        factory.kind = Kind::Industrial;
        check(!has_reason(inspect_report::make(factory), "Heavy pollution"), "industry is not blamed on pollution");

        Facts declining = growing;
        declining.growth = -30;
        check(inspect_report::make(declining).status == "Declining", "negative growth reads Declining");
        Facts booming = growing;
        booming.growth = 150;
        check(inspect_report::make(booming).status == "Booming", "fast growth reads Booming");
        Facts stalled = growing;
        stalled.power = 0;
        check(inspect_report::make(stalled).status == "Stalled", "a growing reading without power reads Stalled");

        Facts steady = growing;
        steady.growth = 0;
        check(inspect_report::make(steady).status == "Steady", "an untroubled zone with no growth reads Steady");
        steady.demand = -0.1;
        check(inspect_report::make(steady).status == "Not growing", "a zone held back by demand reads Not growing");

        Facts station;
        station.kind = Kind::Building;
        station.power = 1;
        station.access = true;
        check(inspect_report::make(station).status == "Working", "a powered building with a road is Working");
        station.access = false;
        check(inspect_report::make(station).status == "Needs attention", "a building without a road needs attention");
        Facts land;
        check(inspect_report::make(land).status == "Open land" && inspect_report::make(land).reasons.empty(),
              "open land has no complaints");
        check(inspect_report::band_of(255) == 3 && inspect_report::band_of(63) == 0, "bands follow the engine");
        check(inspect_report::land_band(29) == 0 && inspect_report::land_band(150) == 3, "land bands follow the engine");
    }

    // Smooth heat field.
    {
        const int w = 20;
        const int h = 10;
        std::vector<float> field(static_cast<std::size_t>(w * h), 0.0f);
        field[static_cast<std::size_t>(5 * w + 10)] = 1.0f;
        float before = 0.0f;
        for (float v : field) {
            before += v;
        }
        heat_field::blur(field, w, h, 1.1);
        float after = 0.0f;
        for (float v : field) {
            after += v;
        }
        check(std::fabs(before - after) < 0.01f, "the blur keeps the total heat");
        check(field[static_cast<std::size_t>(5 * w + 10)] < 1.0f && field[static_cast<std::size_t>(5 * w + 11)] > 0.0f,
              "the blur spreads a hot spot");
        check(field[static_cast<std::size_t>(5 * w + 11)] < field[static_cast<std::size_t>(5 * w + 10)],
              "the center stays hottest");
        std::vector<float> ramp = {0.0f, 1.0f, 0.0f, 1.0f};
        check(std::fabs(heat_field::sample(ramp, 2, 2, 1.0, 0.5) - 0.5f) < 0.001f, "sampling between centers interpolates");
        check(std::fabs(heat_field::sample(ramp, 2, 2, 1.5, 0.5) - 1.0f) < 0.001f, "a tile center samples its own value");
        check(heat_field::band(0.0f, 5) == 0 && heat_field::band(1.0f, 5) == 4 && heat_field::band(0.5f, 5) == 2,
              "contour bands");
        float r = 0;
        float g = 0;
        float b = 0;
        float a = 0;
        heat_field::ramp(0.0f, r, g, b, a);
        check(a == 0.0f, "no heat is clear");
        heat_field::ramp(1.0f, r, g, b, a);
        check(a > 0.5f && a < 0.8f && r > 0.9f && g < 0.2f, "full heat is red and still shows the city");
    }

    // Night light layer: a powered zone glows, an unpowered one stays dark,
    // and a road gets lamps. The layer is its own buffer.
    {
        const int w = 12;
        const int h = 6;
        std::vector<int> map(static_cast<std::size_t>(w * h), DIRT);
        // Powered commercial zone centered at (2,2), unpowered one at (8,2).
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                map[static_cast<std::size_t>((2 + dy) * w + 2 + dx)] = COMBASE + 20;
                map[static_cast<std::size_t>((2 + dy) * w + 8 + dx)] = COMBASE + 20;
            }
        }
        map[static_cast<std::size_t>(2 * w + 2)] = (COMBASE + 20) | ZONEBIT | PWRBIT;
        map[static_cast<std::size_t>(2 * w + 8)] = (COMBASE + 20) | ZONEBIT;
        for (int x = 0; x < w; ++x) {
            map[static_cast<std::size_t>(5 * w + x)] = ROADBASE + 2;
        }
        const int lw = w * night_light::kPx;
        const int lh = h * night_light::kPx;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(lw * lh), 0xDEADBEEFu);
        night_light::build(map, w, h, reinterpret_cast<unsigned char *>(pixels.data()), lw * 4);
        auto light_in = [&](int tx0, int ty0, int tx1, int ty1) {
            int sum = 0;
            for (int py = ty0 * night_light::kPx; py < (ty1 + 1) * night_light::kPx; ++py) {
                for (int px = tx0 * night_light::kPx; px < (tx1 + 1) * night_light::kPx; ++px) {
                    sum += static_cast<int>(pixels[static_cast<std::size_t>(py * lw + px)] >> 24);
                }
            }
            return sum;
        };
        check(light_in(1, 1, 3, 3) > 0, "a powered zone has lit windows");
        check(light_in(7, 1, 9, 3) == 0, "an unpowered zone is dark");
        check(light_in(0, 5, w - 1, 5) > 0, "a road has street lamps");
        check(night_light::glow_for_tile(DIRT) == night_light::Glow::None, "dirt does not glow");
        check(night_light::glow_for_tile(RESBASE) == night_light::Glow::None, "an empty lot has no windows");
        check(night_light::hash(3, 4, 5) == night_light::hash(3, 4, 5) && night_light::hash(3, 4, 5) != night_light::hash(4, 3, 5),
              "the window pattern is stable");
    }

    // Handheld layout switch.
    {
        using namespace layout_mode;
        check(!use_handheld(Auto, false, false), "a desktop keeps the toolbar layout");
        check(use_handheld(Auto, true, false), "SteamDeck=1 picks the handheld layout");
        check(use_handheld(Auto, false, true), "a handheld board picks the handheld layout");
        check(!use_handheld(Desktop, true, true), "Desktop wins on a Deck");
        check(use_handheld(Handheld, false, false), "Handheld can be chosen on a desktop");
        check(clamp_choice(7) == Auto && clamp_choice(-1) == Auto, "a bad stored value is Auto");
        check(handheld_product("Jupiter\n") && handheld_product("Galileo") && !handheld_product("ThinkPad X1"),
              "Steam Deck boards are handhelds");
    }

    // Settings round trip for the new keys.
    {
        ui_scale::StoredSettings stored;
        stored.skin = 1;
        stored.night = 1;
        stored.layout = 2;
        const ui_scale::StoredSettings back = ui_scale::settings_parse(ui_scale::settings_text(stored));
        check(back.skin == 1 && back.night == 1 && back.layout == 2, "skin, night and layout survive a save");
        const ui_scale::StoredSettings blank = ui_scale::settings_parse("ui_scale=0\n");
        check(blank.skin == -1 && blank.night == -1 && blank.layout == -1, "an old settings file keeps the defaults");
    }

    if (failures != 0) {
        std::cerr << failures << " polish check(s) failed\n";
        return 1;
    }
    std::cout << "polish: all checks passed\n";
    return 0;
}

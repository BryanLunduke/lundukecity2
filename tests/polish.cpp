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

#include <cairo.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
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
        check(covered == 26, "tool groups cover the 26 toolbar buttons");
        static const char *titles[] = {"BUILD",     "ZONES",             "PARKS", "SERVICES",
                                       "UTILITIES", "SPECIAL BUILDINGS", "PORTS"};
        check(tool_groups::kGroupCount == 7, "seven toolbar sections");
        {
            // U9: L1/R1 held jump a section at a time; Inspect is its own stop.
            const auto &gs = tool_groups::groups();
            const int last = tool_groups::kGroupCount - 1;
            const int count = gs[last].first + gs[last].count + 1;
            check(tool_groups::section_jump(gs[0].first, 1, count) == gs[1].first, "R1 held jumps to the next section");
            check(tool_groups::section_jump(gs[1].first + gs[1].count - 1, 1, count) == gs[2].first,
                  "a jump from inside a section lands on the next one's first tool");
            check(tool_groups::section_jump(gs[last].first, 1, count) == count - 1, "after the last section comes Inspect");
            check(tool_groups::section_jump(count - 1, 1, count) == gs[0].first, "after Inspect it wraps to the start");
            check(tool_groups::section_jump(gs[0].first, -1, count) == count - 1, "L1 held from the start goes to Inspect");
            // U8: the active section's slots are big, the rest share the room.
            const double room = 1180.0;
            const tool_groups::QuickBarLayout bar = tool_groups::quick_bar_layout(count, gs[2].first, 56.0, room);
            check(bar.total <= room + 0.01 && bar.big >= bar.small && bar.big > 0.0 && bar.small >= 36.0 - 1e-9,
                  "the quick bar fits and keeps every slot at least 36px");
            for (int i = 0; i < count; ++i) {
                const bool inside = i >= gs[2].first && i < gs[2].first + gs[2].count;
                check(bar.width[i] == (inside ? bar.big : bar.small), "only the active section's slots are big");
            }
            const tool_groups::QuickBarLayout even = tool_groups::quick_bar_layout(count, -1, 56.0, room);
            check(even.big == even.small, "no active section: every slot the same");
        }
        for (int g = 0; g < tool_groups::kGroupCount && g < 7; ++g) {
            check(std::string(tool_groups::groups()[g].title) == titles[g], "toolbar sections in the editor's order");
        }
        check(tool_groups::group_of(7) == 1, "the Data Center sits with the zones");
        check(tool_groups::group_of(16) == 4, "the Micro nuclear plant sits with the utilities");
        check(tool_groups::group_of(20) == 5, "the stadium sits with the special buildings");
        for (int columns = 2; columns <= 4; ++columns) {
            const int cell = 40;
            const int header = 18;
            for (int i = 0; i < 26; ++i) {
                int x = 0;
                int y = 0;
                check(tool_groups::slot(i, columns, cell, header, x, y), "every tool has a slot");
                check(tool_groups::index_at(x + cell / 2, y + cell / 2, columns, cell, header) == i,
                      "the middle of a slot hits that tool");
            }
            check(tool_groups::index_at(5, 5, columns, cell, header) == -1, "a header is not a button");
            check(tool_groups::index_at(columns * cell + 1, header + 5, columns, cell, header) == -1,
                  "right of the grid is not a button");
            check(tool_groups::grid_height(columns, cell, header) >= 26 / columns * cell + 7 * header,
                  "grid height includes every header");
        }
        int x = 0;
        int y = 0;
        // Balanced rows: BUILD's four tools are 2+2 in three columns, centred
        // (half a cell in), so Power lines is the second button of row two.
        tool_groups::slot(3, 3, 40, 18, x, y);
        check(x == 20 + 40 && y == 18 + 40, "Power lines sit second on the second row of BUILD with three columns");
        check(tool_groups::index_at(5, 18 + 40 + 20, 3, 40, 18) == -1, "the margin of a centred short row is not a button");
        check(tool_groups::index_at(100 + 10, 18 + 40 + 20, 3, 40, 18) == -1, "the empty end of a short row is not a button");
        // No section leaves a single orphan button on a row of its own.
        for (int columns = 2; columns <= 4; ++columns) {
            for (int g = 0; g < tool_groups::kGroupCount; ++g) {
                const int rows = tool_groups::rows_in(g, columns);
                int total = 0;
                for (int r = 0; r < rows; ++r) {
                    const int len = tool_groups::row_length(g, columns, r);
                    total += len;
                    check(len >= 1 && len <= columns, "a balanced row fits the columns");
                    check(rows == 1 || tool_groups::groups()[g].count < 2 * rows || len >= 2,
                          "no orphan button on a row of its own");
                }
                check(total == tool_groups::groups()[g].count, "balanced rows hold every tool");
            }
        }
        check(tool_groups::row_length(5, 3, 0) == 3 && tool_groups::row_length(5, 3, 2) == 2,
              "SPECIAL BUILDINGS is 3+2+2 in three columns");
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

    // Night light layers: a powered zone's windows light, an unpowered one
    // stays dark, a road gets lamps, and window light lands only on window
    // pixels of the mask. The layers are their own buffers.
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
        // A sparse made-up mask: every third pixel on even rows.
        std::vector<night_light::MaskRows> masks(static_cast<std::size_t>(TILE_COUNT * night_light::kTile), 0);
        for (int y = 0; y < night_light::kTile; y += 2) {
            masks[static_cast<std::size_t>((COMBASE + 20) * night_light::kTile + y)] = 0x4924; // bits 2,5,8,11,14
        }
        const int lw = w * night_light::kPx;
        const int lh = h * night_light::kPx;
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(lw * lh), 0xDEADBEEFu);
        night_light::build(map, w, h, reinterpret_cast<unsigned char *>(pixels.data()), lw * 4, &masks);
        auto light_in = [&](int tx0, int ty0, int tx1, int ty1) {
            int sum = 0;
            for (int py = ty0 * night_light::kPx; py < (ty1 + 1) * night_light::kPx; ++py) {
                for (int px = tx0 * night_light::kPx; px < (tx1 + 1) * night_light::kPx; ++px) {
                    sum += static_cast<int>(pixels[static_cast<std::size_t>(py * lw + px)] >> 24);
                }
            }
            return sum;
        };
        check(light_in(1, 1, 3, 3) > 0, "a powered zone's windows glow");
        check(light_in(7, 1, 9, 3) == 0, "an unpowered zone is dark");
        check(light_in(0, 5, w - 1, 5) > 0, "a road has street lamps");

        const int ww = w * night_light::kTile;
        const int wh = h * night_light::kTile;
        std::vector<std::uint32_t> windows(static_cast<std::size_t>(ww * wh), 0xDEADBEEFu);
        std::vector<int> keys;
        const int painted = night_light::build_windows(map, w, h, masks, reinterpret_cast<unsigned char *>(windows.data()),
                                                       ww * 4, &keys);
        check(painted == w * h, "the first window pass paints every tile");
        int lit = 0;
        int stray = 0;
        int dark_zone = 0;
        for (int py = 0; py < wh; ++py) {
            for (int px = 0; px < ww; ++px) {
                if ((windows[static_cast<std::size_t>(py * ww + px)] >> 24) == 0) {
                    continue;
                }
                const int tx = px / night_light::kTile;
                const int ty = py / night_light::kTile;
                const int tile = map[static_cast<std::size_t>(ty * w + tx)] & LOMASK;
                const night_light::MaskRows row =
                    masks[static_cast<std::size_t>(tile * night_light::kTile + py % night_light::kTile)];
                if ((row & (1u << (px % night_light::kTile))) == 0) {
                    ++stray;
                }
                if (tx >= 7) {
                    ++dark_zone;
                }
                ++lit;
            }
        }
        check(lit > 0, "a powered zone has lit windows");
        check(stray == 0, "window light lands only on mask pixels");
        check(dark_zone == 0, "an unpowered zone has no lit windows");
        // Not every window: each building lights a share of them.
        check(lit < 9 * 8 * 5, "some windows stay dark");
        check(night_light::build_windows(map, w, h, masks, reinterpret_cast<unsigned char *>(windows.data()), ww * 4,
                                         &keys) == 0,
              "an unchanged map repaints no window tiles");
        map[static_cast<std::size_t>(2 * w + 2)] &= ~PWRBIT;
        check(night_light::build_windows(map, w, h, masks, reinterpret_cast<unsigned char *>(windows.data()), ww * 4,
                                         &keys) == 9,
              "losing power repaints the zone's nine tiles");
        bool any = false;
        for (std::uint32_t p : windows) {
            any = any || (p >> 24) != 0;
        }
        check(!any, "an unpowered city has no lit windows");

        // Mask detection on made-up art: a dark pane in a grey facade is a
        // window; a dark line, a pane on grass and the flat facade are not.
        std::vector<std::uint32_t> art(256, 0xFF909098u);
        art[5 * 16 + 5] = 0xFF303040u;      // pane
        for (int x = 2; x < 12; ++x) {
            art[10 * 16 + x] = 0xFF202020u; // roof edge line
        }
        for (int y = 12; y < 16; ++y) {
            for (int x = 10; x < 16; ++x) {
                art[y * 16 + x] = 0xFF30A030u; // grass
            }
        }
        art[14 * 16 + 13] = 0xFF105010u;    // dark leaf in the grass
        night_light::MaskRows rows[night_light::kTile];
        night_light::window_mask(art.data(), rows);
        int bits = 0;
        for (int y = 0; y < night_light::kTile; ++y) {
            for (int x = 0; x < night_light::kTile; ++x) {
                bits += (rows[y] >> x) & 1;
            }
        }
        check(bits == 1 && (rows[5] & (1u << 5)) != 0, "only the pane is a window");
        check(night_light::wants_windows(COMBASE + 20) && !night_light::wants_windows(ROADBASE) &&
                  !night_light::wants_windows(DIRT) && !night_light::wants_windows(INDBASE2 + 3),
              "only building art gets windows");

#ifdef LUNDUKE_CITY_ASSET_DIR
        // The real sheets: buildings have windows, and none sit on grass.
        for (const char *name : {"tiles.png", "tiles-svga.png"}) {
            const std::string path = std::string(LUNDUKE_CITY_ASSET_DIR) + "/images/micropolisEngine/" + name;
            if (!std::ifstream(path).good()) {
                std::cout << "skip: no " << path << "\n";
                continue;
            }
            cairo_surface_t *sheet = cairo_image_surface_create_from_png(path.c_str());
            if (cairo_surface_status(sheet) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(sheet);
                check(false, "a tile sheet loads");
                continue;
            }
            const int cols = cairo_image_surface_get_width(sheet) / 16;
            const int tiles = cols * (cairo_image_surface_get_height(sheet) / 16);
            const unsigned char *data = cairo_image_surface_get_data(sheet);
            const int stride = cairo_image_surface_get_stride(sheet);
            std::vector<std::uint32_t> packed(static_cast<std::size_t>(tiles) * 256);
            for (int t = 0; t < tiles; ++t) {
                for (int y = 0; y < 16; ++y) {
                    const auto *src = reinterpret_cast<const std::uint32_t *>(data + ((t / cols) * 16 + y) * stride) +
                                      (t % cols) * 16;
                    for (int x = 0; x < 16; ++x) {
                        packed[static_cast<std::size_t>(t) * 256 + y * 16 + x] = src[x];
                    }
                }
            }
            std::vector<night_light::MaskRows> real;
            night_light::window_masks(packed.data(), tiles, real);
            int windowed = 0;
            int on_grass = 0;
            for (int t = COMBASE + 9; t < INDBASE; ++t) {
                int count = 0;
                for (int y = 0; y < 16; ++y) {
                    for (int x = 0; x < 16; ++x) {
                        if ((real[static_cast<std::size_t>(t) * 16 + y] >> x) & 1) {
                            ++count;
                            const std::uint32_t p = packed[static_cast<std::size_t>(t) * 256 + y * 16 + x];
                            const int r = (p >> 16) & 0xFF;
                            const int g = (p >> 8) & 0xFF;
                            const int b = p & 0xFF;
                            if (g > r + 25 && g > b + 25) {
                                ++on_grass;
                            }
                        }
                    }
                }
                windowed += count > 0 ? 1 : 0;
            }
            check(windowed > 60, "most commercial tiles have windows in their art");
            check(on_grass == 0, "no window sits on grass");
            int road_bits = 0;
            for (int y = 0; y < 16; ++y) {
                road_bits += real[static_cast<std::size_t>(ROADBASE + 2) * 16 + y] != 0 ? 1 : 0;
            }
            check(road_bits == 0, "roads have no windows");
            cairo_surface_destroy(sheet);
        }
#endif
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
        check(handheld_product("83L3") && handheld_product("83E1") && handheld_product("Claw A1M") &&
                  handheld_product("Claw 8 AI+ A2VM") && handheld_product("ROG Ally X RC72LA") &&
                  handheld_product("ROG Xbox Ally RC73YA") && handheld_product("AYANEO 2S") &&
                  handheld_product("ONEXPLAYER X1 i") && handheld_product("ZOTAC GAMING ZONE") &&
                  handheld_product("G1618-04"),
              "other handheld boards (C4) are handhelds");
        check(!handheld_product("Galileo Pro") && !handheld_product("Clawhammer") && !handheld_product("") &&
                  !handheld_product("Precision 5570"),
              "desktop and laptop boards stay desktop");
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

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Headless checks for the presentation pass: road access on a large special,
// ghost/placement agreement (including a park the click would refuse),
// query sentence case, the unpowered-special bolt, and the motion hooks.

#include "city_session.hpp"
#include "dialog_copy.hpp"
#include "messages.hpp"
#include "placement_hint.hpp"
#include "present.hpp"
#include "scene_fade.hpp"
#include "theme.hpp"
#include "special_buildings.hpp"
#include "tile_atlas.hpp"
#include "tools.hpp"
#include "ui_scale.hpp"
#include "zoom_keys.hpp"

#include "micropolis.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int fail(int code, const std::string &message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
    return code;
}

int index_named(const char *name)
{
    for (int i = 0; i < special_building_count(); ++i) {
        if (std::string(special_building(i).name) == name) {
            return i;
        }
    }
    return -1;
}

bool open_patch(const CitySession &session, int left, int top, int width, int height)
{
    for (int y = top - 1; y <= top + height; ++y) {
        for (int x = left; x < left + width; ++x) {
            const int raw = session.map_value(x, y);
            const int tile = raw & LOMASK;
            if (tile >= RIVER && tile <= WATER_HIGH) {
                return false;
            }
            if (tile != DIRT && !special_auto_clearable(raw)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    const std::string body = query_body("Residential zone — population 120, land value 40, powered");
    if (body.find("\nPopulation 120\n") == std::string::npos || body.find("Land value 40") == std::string::npos ||
        body.find("\nPowered") == std::string::npos) {
        return fail(1, "query lines are not sentence case: " + body);
    }

    if (notice_is_disaster("More roads needed.") || !notice_is_disaster("A monster has been sighted.") ||
        !notice_is_disaster("An explosion has been detected.")) {
        return fail(2, "disaster notices were classified wrong");
    }
    if (!notice_shakes("An earthquake has been reported.") || !notice_shakes("An explosion has been detected.") ||
        notice_shakes("A monster has been sighted.")) {
        return fail(3, "shake should be earthquake and explosion only");
    }
    if (notice_mark("Fire reported.") != 1) {
        return fail(4, "a fire notice should use the disaster mark");
    }

    if (look_change_fades(false, 0, 2) || !look_change_fades(true, 0, 1) || look_change_fades(true, 1, 1)) {
        return fail(5, "a look fade should wait until a sheet is showing and actually changes");
    }
    if (std::fabs(glide_ease(0.0)) > 0.001 || std::fabs(glide_ease(1.0) - 1.0) > 0.001 || glide_ease(0.5) <= 0.5) {
        return fail(6, "the auto-goto ease should start slow-to-finish and land on the target");
    }
    YearStamp stamp;
    stamp.observe(1900, 0);
    if (stamp.visible()) {
        return fail(7, "the first January should not stamp");
    }
    stamp.observe(1901, 0);
    if (!stamp.visible() || stamp.text() != "Jan 1901") {
        return fail(8, "a new year should stamp Jan and the year");
    }
    stamp.advance(700);
    if (!stamp.visible()) {
        return fail(9, "the year stamp should still be readable after 700 ms");
    }
    stamp.advance(kYearStampMs);
    if (stamp.visible()) {
        return fail(9, "the year stamp should leave after its beat");
    }
    const BannerColors look{1.0, 1.0, 0.33, 0.1, 0.08, 0.0};
    const BannerColors message = message_bar_colors(true, look);
    const BannerColors goal = goal_banner_colors(look);
    if (message.g > 0.05 || message.r < 0.4 || goal.r != look.r || goal.g != look.g || goal.b != look.b ||
        goal.ink_r != look.ink_r) {
        return fail(25, "the goal banner must keep its look colors while the message bar is red");
    }
    if (dust_speck_y(16, 0, 0) < 2 || dust_speck_y(16, 300, 7) < 2) {
        return fail(26, "bulldoze dust should stay inside the tile, under the tag");
    }
    const SelectionBand band = selection_band(48, 22, 40, 36);
    if (band.top > 48.0 || band.top + band.height < 48.0 + 22.0) {
        return fail(27, "the selection highlight does not cover the glyphs");
    }
    if (band.top < 42.0 - 0.01 || band.top + band.height > 74.0 + 0.01) {
        return fail(27, "the selection highlight draws outside the text field");
    }
    const SelectionBand tight = selection_band(48, 40, 40, 36);
    if (tight.top + tight.height > 74.0 + 0.01 || tight.top > 48.0) {
        return fail(27, "a tall selection still leaves the text field");
    }
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    bool svalid = false;
    ui_scale::choose_stored_window(true, true, 40, 50, 1280, 800, 0, 0, 1920, 1080, sx, sy, sw, sh, svalid);
    if (!svalid || sw != 1280 || sh != 800 || sx != 40) {
        return fail(28, "fullscreen size replaced the windowed rectangle");
    }
    if (ui_scale::stored_window_maximized(true, true, false)) {
        return fail(29, "fullscreen was stored as a maximized window");
    }
    const LayoutPixels leaving = layout_pixels_leaving_fullscreen(true, 1920, 1080, 1523, 825, 1.0f);
    const LayoutPixels settled = layout_pixels_leaving_fullscreen(true, 1523, 825, 1523, 825, 1.0f);
    const LayoutPixels ordinary = layout_pixels_leaving_fullscreen(false, 1920, 1080, 1523, 825, 1.0f);
    if (leaving.w != 1523 || leaving.h != 825 || settled.w != 1523 || settled.h != 825 || ordinary.w != 1920 ||
        ordinary.h != 1080) {
        return fail(35, "leaving fullscreen did not lay out at the windowed size");
    }
    const LayoutPoint hit = window_point_to_layout(12, 4, true, 1523, 825, 1920, 1080, 1523, 825, 1.0f);
    if (std::fabs(hit.x - 12.0) > 0.01 || std::fabs(hit.y - 4.0) > 0.01) {
        return fail(35, "a click after leaving fullscreen was scaled to the screen");
    }
    // The window frame is back, but SDL or the renderer is still the screen.
    // A same-size assignment does not refresh that drawable.
    if (!fullscreen_exit_needs_drawable_refresh(1920, 1080, 1920, 1080, 1523, 825, 1.0f) ||
        !fullscreen_exit_needs_drawable_refresh(1523, 825, 1920, 1080, 1523, 825, 1.0f)) {
        return fail(37, "a screen-sized renderer was treated as the restored window");
    }
    if (fullscreen_exit_needs_drawable_refresh(1523, 825, 1523, 825, 1523, 825, 1.0f)) {
        return fail(37, "a restored drawable was still treated as stale");
    }
    if (!fullscreen_exit_position_is_spurious(0, 0, 198, 127)) {
        return fail(38, "leaving fullscreen stored the screen origin");
    }
    if (fullscreen_exit_position_is_spurious(198, 127, 200, 130)) {
        return fail(38, "the windowed position was discarded");
    }
    const PresentPlace matched = present_place(1523, 825, 1523, 825, true);
    const LayoutPixels entered = layout_pixels_for_window(true, false, 1920, 1080, 1523, 825, 1.0f);
    const LayoutPixels exit_requested = layout_pixels_for_window(true, true, 1920, 1080, 1523, 825, 1.0f);
    if (matched.fitted || entered.w != 1920 || entered.h != 1080 || exit_requested.w != 1523 || exit_requested.h != 825) {
        return fail(39, "fullscreen kept the windowed layout, or a matched frame was moved");
    }
    const double short_card = year_card_panel_height(700, 180, 90, 80, 8);
    const double long_card = year_card_panel_height(700, 180, 160, 220, 8);
    const double capped_card = year_card_panel_height(400, 180, 200, 200, 8);
    if (std::fabs(short_card - 358.0) > 0.5 || std::fabs(long_card - 568.0) > 0.5 || long_card > 700.5 ||
        std::fabs(capped_card - 400.0) > 0.5) {
        return fail(40, "the year card is not the height of its content");
    }
    if (marker_step_ms(500) > kMarkerBlink / 2 || marker_step_ms(10) != 10 || !marker_lit(0) || marker_lit(kMarkerMs) ||
        kMarkerMs < 2000) {
        return fail(39, "the highlight ring does not blink for a couple of seconds");
    }
    if (!hold_stale_fullscreen_present(true, true, 0) || hold_stale_fullscreen_present(true, true, 1) ||
        hold_stale_fullscreen_present(false, true, 0)) {
        return fail(39, "a stale fullscreen frame is presented more than once");
    }
    const YearBoxPlan boxes = plan_year_boxes(200, 0, 8, 90, 140);
    const YearBoxPlan roomy = plan_year_boxes(400, 0, 8, 90, 140);
    if (boxes.low_h + 0.5 < 140.0 || roomy.low_h + 0.5 < 140.0 || boxes.top_h + boxes.low_h + 8.0 > 200.5) {
        return fail(40, "a year-card line is clipped by its box");
    }
    const DialogFocusFill focus = dialog_focus_fill();
    if (std::fabs(focus.r - 0.20) > 0.001 || std::fabs(focus.g - 0.42) > 0.001 || std::fabs(focus.b - 0.72) > 0.001) {
        return fail(41, "a focused dialog button does not match the quit notice");
    }
    for (int look = 0; look < 3; ++look) {
        theme::set_chrome_look(look);
        double ink_r = 0;
        double ink_g = 0;
        double ink_b = 0;
        theme::reading_ink(ink_r, ink_g, ink_b);
        if (ink_r > 0.25 || ink_g > 0.25 || ink_b > 0.35) {
            theme::set_chrome_look(0);
            return fail(42, "dialog ink is too light on the cream page");
        }
    }
    theme::set_chrome_look(0);
    const double thumb_end = slider_thumb_x(10, 200, 12, 1.0);
    const double thumb_start = slider_thumb_x(10, 200, 12, 0.0);
    if (thumb_start < 10.0 || thumb_end + 12.0 > 210.01 || thumb_end < 10.0) {
        return fail(36, "the slider thumb hangs past the track");
    }
    if (zoom_menu_label(16) != "Zoom 100%" || zoom_percent_for_tile(32) != 200 || zoom_percent_for_tile(8) != 50) {
        return fail(30, "the menu zoom label is not the atlas step as a percent");
    }
    const SettingsStack settings_row = settings_stack(0, 18, 20, 12);
    if (!settings_stack_clear(settings_row, 18) || settings_row.bar < 18 || settings_row.next_caption <= settings_row.bar) {
        return fail(31, "settings rows overlap");
    }
    if (!year_card_dismisses(YearCardClick::Close) || !year_card_dismisses(YearCardClick::Escape) ||
        !year_card_dismisses(YearCardClick::Map) || year_card_dismisses(YearCardClick::Budget) ||
        !year_card_jumps(YearCardClick::Map) || year_card_jumps(YearCardClick::Close)) {
        return fail(32, "year card Close and Map do not dismiss the way they should");
    }
    SceneFade fade;
    fade.start();
    fade.advance(140);
    if (!fade.active() || SceneFade::kTotalMs < 400) {
        return fail(33, "the look fade is still the old short dither");
    }
    fade.advance(SceneFade::kTotalMs);
    if (fade.active()) {
        return fail(34, "the look fade did not finish");
    }

    const ChartPlate spooky = chart_plate(2);
    const ChartPlate classic = chart_plate(0);
    if (!spooky.night || spooky.paper_r > 0.5 || classic.night || classic.paper_r < 0.8) {
        return fail(10, "the chart plate should follow the look");
    }
    double lo = 0;
    double hi = 100;
    chart_headroom(lo, hi);
    if (!(hi > 100.0) || lo < 0.0) {
        return fail(11, "a chart peak should sit below the top of the plate");
    }
    if (std::fabs(history_plot_x(0, 40, 10, 100) - 110) > 0.01 ||
        std::fabs(history_plot_x(40, 40, 10, 100) - 10) > 0.01) {
        return fail(12, "history should use the whole plate, newest on the right");
    }
    double room = 0;
    if (!goal_banner_room(800, 8, 220, true, room) || room >= 800 - 16) {
        return fail(13, "the goal banner should leave the hover card alone");
    }

    const int zoo = index_named("Zoo");
    const int prison = index_named("Prison");
    if (zoo < 0 || prison < 0) {
        return fail(14, "zoo or prison is missing from the table");
    }
    const SpecialBuilding &zoo_row = special_building(zoo);
    const int zoo_cell = zoo_row.anchor_y * zoo_row.width + zoo_row.anchor_x;
    const int bare = zoo_row.center_tile | ZONEBIT | BURNBIT | BULLBIT;
    if (!special_center_needs_bolt(bare, zoo, zoo_cell, true) || special_center_needs_bolt(bare, zoo, zoo_cell, false) ||
        special_center_needs_bolt(bare | PWRBIT, zoo, zoo_cell, true) ||
        special_center_needs_bolt(bare, zoo, 0, true)) {
        return fail(15, "the bolt should blink only on an unpowered special centre");
    }

    Micropolis sim;
    sim.generateSomeCity(3);
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            sim.map[x][y] = DIRT;
        }
    }
    sim.setFunds(20000);
    const int click_x = 12;
    const int click_y = 12;
    const int left = click_x - zoo_row.anchor_x;
    const int top = click_y - zoo_row.anchor_y;
    sim.map[left][top] = static_cast<unsigned short>(WOODS2 | BULLBIT | BURNBIT);
    sim.autoBulldoze = false;
    auto word = [&](int x, int y) { return static_cast<int>(sim.map[x][y]); };
    const PlaceForecast blocked_park = forecast_tile(kSpecialEngineId, click_x, click_y, zoo, sim.totalFunds, false, word);
    std::string error;
    const bool placed_off = place_special_building(&sim, zoo, click_x, click_y, error);
    if (blocked_park.mark != PlaceMark::Blocked || blocked_park.tag.find("Bulldoze") == std::string::npos || placed_off ||
        error.find("Bulldoze") == std::string::npos) {
        std::fprintf(stderr, "forecast '%s' place '%s'\n", blocked_park.tag.c_str(), error.c_str());
        return fail(16, "a park with auto-bulldoze off should block the ghost and the click the same way");
    }
    sim.autoBulldoze = true;
    const PlaceForecast clear_park = forecast_tile(kSpecialEngineId, click_x, click_y, zoo, sim.totalFunds, true, word);
    if (clear_park.mark != PlaceMark::Ready || !place_special_building(&sim, zoo, click_x, click_y, error)) {
        std::fprintf(stderr, "clear '%s' place '%s'\n", clear_park.tag.c_str(), error.c_str());
        return fail(17, "a bulldozable park should be accepted by the ghost and the click");
    }
    for (int y = top; y < top + zoo_row.height; ++y) {
        for (int x = left; x < left + zoo_row.width; ++x) {
            sim.map[x][y] = DIRT;
        }
    }
    sim.setFunds(20000);
    sim.map[left + 1][top + 1] = static_cast<unsigned short>(FOUNTAIN | BULLBIT | ANIMBIT);
    const PlaceForecast fountain = forecast_tile(kSpecialEngineId, click_x, click_y, zoo, sim.totalFunds, true, word);
    const bool fountain_placed = place_special_building(&sim, zoo, click_x, click_y, error);
    if (fountain.mark != PlaceMark::Blocked || fountain.tag.find("Bulldoze") == std::string::npos || fountain_placed ||
        error.find("Bulldoze") == std::string::npos) {
        std::fprintf(stderr, "fountain forecast '%s' place '%s'\n", fountain.tag.c_str(), error.c_str());
        return fail(18, "the animated park fountain should block the ghost and the click");
    }
    sim.map[8][8] = FIRE;
    const PlaceForecast fire = forecast_tile(TOOL_BULLDOZER, 8, 8, -1, 1000, true, word);
    if (fire.tag.find("Cannot clear fire") == std::string::npos) {
        return fail(19, "bulldozing a fire should not say it cannot be built: " + fire.tag);
    }

    CitySession session;
    session.new_city("Edge Road", 7);
    const SpecialBuilding &prison_row = special_building(prison);
    int origin_x = -1;
    int origin_y = -1;
    for (int y = 2; y < CitySession::kWorldH - prison_row.height - 2 && origin_x < 0; ++y) {
        for (int x = 1; x < CitySession::kWorldW - prison_row.width - 1; ++x) {
            if (open_patch(session, x, y, prison_row.width, prison_row.height)) {
                origin_x = x;
                origin_y = y;
                break;
            }
        }
    }
    if (origin_x < 0) {
        return fail(20, "the generated city had no place for a prison");
    }
    const int road_x = origin_x + prison_row.width - 1;
    const int road_y = origin_y + prison_row.height;
    const int wire_x = origin_x;
    const int wire_y = origin_y - 1;
    session.use_tool(TOOL_ROAD, road_x, road_y);
    session.use_tool(TOOL_WIRE, wire_x, wire_y);
    const int anchor_x = origin_x + prison_row.anchor_x;
    const int anchor_y = origin_y + prison_row.anchor_y;
    if (!session.place_special(prison, anchor_x, anchor_y)) {
        return fail(21, "the prison did not place beside the edge road: " + session.message());
    }
    const CitySession::TileInspect center = session.inspect_tile(anchor_x, anchor_y);
    if (center.access.find("Road") == std::string::npos) {
        return fail(22, "a road on the prison's edge was reported as " + center.access);
    }
    if (center.power != "Power: off" || center.condition == "Unpowered") {
        return fail(22, "an unpowered prison should say power once: " + center.power + " / " + center.condition);
    }
    const int center_raw = session.map_value(anchor_x, anchor_y);
    const int center_cell = prison_row.anchor_y * prison_row.width + prison_row.anchor_x;
    if ((center_raw & PWRBIT) != 0 || !special_center_needs_bolt(center_raw, prison, center_cell, true)) {
        return fail(23, "a fresh prison should expose the unpowered bolt path");
    }

    if (tile_atlas().ensure_loaded() && tile_atlas().spooky_available()) {
        tile_atlas().set_look(0);
        double cr = 0;
        double cg = 0;
        double cb = 0;
        tile_atlas().average_color(DIRT, cr, cg, cb);
        tile_atlas().set_look(2);
        double sr = 0;
        double sg = 0;
        double sb = 0;
        tile_atlas().average_color(DIRT, sr, sg, sb);
        tile_atlas().set_look(0);
        if (std::fabs(cr - sr) < 0.01 && std::fabs(cg - sg) < 0.01 && std::fabs(cb - sb) < 0.01) {
            return fail(24, "Spooky terrain should not reuse the Classic dirt color");
        }
    }

    std::printf("presentation ok\n");
    return 0;
}

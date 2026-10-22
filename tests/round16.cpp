// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Checks for the review fixes that do not need a window: scenario sidecar,
// floored years, outcome copy, overlay legends, crisp icon scale, tool
// hotkeys, history holes, Fast bursts, evaluation dashes, chrome fit,
// the default tool, the menu count, query wording, file glance, the
// message log, and the sprite cache.

#include "city_glance.hpp"
#include "city_session.hpp"
#include "dialog_copy.hpp"
#include "history_span.hpp"
#include "menu_model.hpp"
#include "messages.hpp"
#include "overlay_map.hpp"
#include "scenario_sidecar.hpp"
#include "sim_pace.hpp"
#include "sprite_art.hpp"
#include "tool_keys.hpp"
#include "tool_palette.hpp"
#include "tools.hpp"
#include "ui_scale.hpp"

#include <cairo.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int fail(int code, const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return code;
}

} // namespace

int main()
{
    if (menu_table::actionable_count() != 43) {
        return fail(1, "the menu does not have 43 actionable rows");
    }
    if (kDefaultToolIndex != 16 || kToolCount != 17) {
        return fail(2, "a new city does not start on Query");
    }
    if (tool_hotkey_clashes() || tool_index_for_key('b') != 0 || tool_index_for_key('Q') != 16 ||
        tool_index_for_key('o') != 8 ||
        menu_table::accel_action(AccelKey::Letter, 0) != Action::None ||
        menu_table::accel_action(AccelKey::O, static_cast<int>(AccelMod::Ctrl)) != Action::Load) {
        return fail(3, "a tool letter clashes with a menu shortcut");
    }
    if (tool_hint_with_key(0, "Bulldoze").find("[B]") == std::string::npos) {
        return fail(4, "the bulldozer hint does not show its key");
    }
    if (std::abs(icon_pixel_scale(40, 16) - 0.5) > 0.01 || std::abs(icon_pixel_scale(40, 40) - 1.0) > 0.01 ||
        std::abs(icon_pixel_scale(41, 16) - 1.0) > 0.01) {
        return fail(5, "toolbar icons are not on a crisp nearest step");
    }
    if (sim_bursts_for_speed(3) != 4 || sim_bursts_for_speed(2) != 1 || sim_bursts_for_speed(0) != 1) {
        return fail(6, "Fast speed does not run four phases per step");
    }
    if (!ui_scale::chrome_fits(1.0f, 1280, 720) || ui_scale::chrome_fits(1.77f, 1280, 800)) {
        return fail(7, "chrome fit is wrong for 1280x720 or for scale 1.77");
    }

    const double hole[] = {5.0, 0.0, 5.0, 5.0};
    const double tail[] = {4.0, 4.0, 0.0, 0.0};
    const double holes[] = {5.0, 0.0, 5.0, 0.0, 5.0};
    const double run[] = {5.0, 0.0, 0.0, 5.0};
    if (!history_point_skipped(hole, 4, 1) || history_point_skipped(hole, 4, 0) ||
        !history_point_skipped(tail, 4, 3) || history_point_skipped(tail, 4, 0) ||
        !history_point_skipped(holes, 5, 1) || !history_point_skipped(holes, 5, 3) ||
        history_point_skipped(run, 4, 1) || history_point_skipped(run, 4, 2)) {
        return fail(8, "a history hole was drawn as a drop to zero");
    }

    const std::string crime = overlay_legend(CitySession::MapLayer::Crime);
    const std::string growth = overlay_legend(CitySession::MapLayer::Growth);
    const std::string police = overlay_legend(CitySession::MapLayer::Police);
    const std::string fire = overlay_legend(CitySession::MapLayer::Fire);
    const std::string people = overlay_legend(CitySession::MapLayer::Population);
    const std::string zones = overlay_legend(CitySession::MapLayer::Zones);
    if (crime.find("Clear means none") == std::string::npos || growth.find("Clear means no change") == std::string::npos ||
        police.find("Clear means no coverage") == std::string::npos ||
        fire.find("Clear means no coverage") == std::string::npos ||
        people.find("Clear means nobody") == std::string::npos || zones.find("Green homes") == std::string::npos) {
        return fail(9, "an overlay legend is missing its plain-language scale");
    }

    const std::string query = zone_status_text(29, 1, 5, 9, 13, 17);
    if (query.find("Clear — Population: Sparse") == std::string::npos ||
        query.find("Growth: Declining") == std::string::npos ||
        zone_status_text(29, 1, 5, 9, 13, 18).find("Growth: Stable") == std::string::npos) {
        return fail(10, "query bands are not title case");
    }

    {
        CitySession fresh;
        fresh.new_city("Dashes", 9);
        const CitySession::Evaluation empty = fresh.evaluation();
        if (empty.deltas_known || !empty.no_residents) {
            return fail(11, "a new city showed a score change before a year passed");
        }
        CitySession dull_eval;
        if (!dull_eval.load_scenario(CitySession::scenario_def(0).id)) {
            return fail(11, "could not load Dullsville for the evaluation");
        }
        const CitySession::Evaluation report = dull_eval.evaluation();
        if (report.deltas_known || report.no_residents || report.migration != 0 || report.score_delta != 0) {
            return fail(11, "a loaded scenario showed a stale score change");
        }
        const EvaluationCopy copy = evaluation_copy(report, "");
        if (copy.score_delta.find("—") == std::string::npos || copy.migration.find("—") == std::string::npos) {
            return fail(12, "the evaluation window did not dash an unknown change");
        }
        if (fresh.plot_cash_flow(CitySession::HistoryScale::Short, 0) != fresh.budget().cash_flow) {
            return fail(13, "the cash-flow chart ignored the budget estimate");
        }
    }

    const auto dir = std::filesystem::temp_directory_path();
    const auto city_path = dir / "lunduke-round16.cty";
    const auto side_path = dir / "lunduke-round16.cty.json";
    std::filesystem::remove(city_path);
    std::filesystem::remove(side_path);
    {
        CitySession dull;
        if (!dull.load_scenario(CitySession::scenario_def(0).id) || dull.scenario_years_left() != 30) {
            return fail(14, "Dullsville did not start at 30 years");
        }
        dull.set_speed(3);
        for (int i = 0; i < 16; ++i) {
            dull.tick();
        }
        const std::string clock = dull.scenario_clock_text();
        if (clock != "29 years, 11 months left" || dull.scenario_years_left() != 29) {
            std::fprintf(stderr, "clock '%s' years %d\n", clock.c_str(), dull.scenario_years_left());
            return fail(15, "a partial year was not shown in months");
        }
        const std::string won = dull.scenario_outcome_text(true);
        if (won.find("You did it! Dullsville met the goal.") == std::string::npos ||
            won.find("Population") == std::string::npos ||
            won.find("previous speed") != std::string::npos) {
            return fail(16, "the win text does not name the city and a stat");
        }
        if (!dull.save_city_as(city_path.string())) {
            return fail(17, "could not save the scenario city");
        }
        CitySession loaded;
        if (!loaded.load_city(city_path.string()) || loaded.scenario_id() != dull.scenario_id() ||
            loaded.scenario_clock_text() != clock) {
            std::fprintf(stderr, "reloaded id %d clock '%s'\n", loaded.scenario_id(),
                         loaded.scenario_clock_text().c_str());
            return fail(18, "loading a save dropped the scenario clock");
        }
        const CityGlance glance = city_glance(city_path.string());
        if (!glance.ok || glance.swatch.size() != 120u * 100u || glance.summary.find("Pop ") == std::string::npos ||
            glance.summary.find("$") == std::string::npos) {
            return fail(19, "the file list could not read a saved city");
        }
    }
    {
        ScenarioClock done;
        done.scenario = CitySession::scenario_def(0).id;
        done.score_wait = 400;
        done.score_type = done.scenario;
        done.outcome = 1;
        done.month = 3;
        done.year = 1930;
        std::ofstream out(side_path.string(), std::ios::trunc);
        out << scenario_sidecar_text(done);
        out.close();
        ScenarioClock parsed;
        const std::string side_text = scenario_sidecar_text(done);
        if (side_text.find('{') == std::string::npos || side_text.find("\"scenario\"") == std::string::npos ||
            !scenario_sidecar_parse(side_text, parsed) || parsed.outcome != 1 || parsed.score_wait != 400) {
            return fail(20, "the scenario sidecar did not round-trip as JSON");
        }
        ScenarioClock legacy;
        const char *old_side = "scenario=1\nscore_wait=400\nscore_type=1\noutcome=1\nmonth=3\nyear=1930\n";
        if (!scenario_sidecar_parse(old_side, legacy) || legacy.outcome != 1 || legacy.year != 1930) {
            return fail(20, "an older key=value sidecar no longer loads");
        }
        CitySession finished;
        if (!finished.load_city(city_path.string()) || finished.scenario_years_left() != -1 ||
            !finished.scenario_progress().empty()) {
            return fail(21, "a finished scenario started its timer again");
        }
    }

    {
        CitySession city;
        city.new_city("Notices", 11);
        city.set_disasters(true);
        for (int i = 0; i < 60; ++i) {
            city.disaster_fire();
        }
        if (city.notices().size() > 50) {
            return fail(22, "the message log kept more than 50 notices");
        }
        city.clear_notices();
        if (!city.notices().empty()) {
            return fail(23, "clearing the message log left a notice");
        }
        std::vector<unsigned short> tiles;
        if (!city.preview_terrain(7, 0, -1, -1, -1, tiles) || tiles.size() != 120u * 100u) {
            return fail(24, "the terrain preview did not build a map");
        }
    }

    {
        CitySession city;
        city.new_city("Overlay", 21);
        OverlayMap map;
        map.set_session(&city);
        map.set_layer(CitySession::MapLayer::Crime);
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 240, 100);
        cairo_t *cr = cairo_create(surface);
        map.sync();
        map.draw(cr, 240, 100);
        cairo_surface_flush(surface);
        const unsigned char *px = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        int painted = 0;
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 240; ++x) {
                if (px[y * stride + x * 4 + 3] != 0) {
                    ++painted;
                }
            }
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        if (painted != 0) {
            return fail(25, "a zero crime sample was painted");
        }
        map.set_layer(CitySession::MapLayer::Water);
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 240, 100);
        cr = cairo_create(surface);
        map.sync();
        map.draw(cr, 240, 100);
        cairo_surface_flush(surface);
        px = cairo_image_surface_get_data(surface);
        const int water_stride = cairo_image_surface_get_stride(surface);
        int margin = 0;
        int water = 0;
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 50; ++x) {
                margin += px[y * water_stride + x * 4 + 3] != 0 ? 1 : 0;
            }
            for (int x = 60; x < 180; ++x) {
                water += px[y * water_stride + x * 4 + 3] != 0 ? 1 : 0;
            }
        }
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        if (margin != 0 || water < 50) {
            std::fprintf(stderr, "margin %d water %d\n", margin, water);
            return fail(26, "the water view stretched or hid the rivers");
        }
    }

    if (sprite_frame(1, 1) == nullptr) {
        return fail(27, "a sprite frame did not load");
    }
    sprite_art_shutdown();
    if (sprite_frame(1, 1) == nullptr) {
        return fail(28, "the sprite cache did not reload after shutdown");
    }
    sprite_art_shutdown();
    sprite_art_shutdown();

    std::filesystem::remove(city_path);
    std::filesystem::remove(side_path);
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Checks for the review fixes that do not need a window: scenario sidecar,
// floored years, outcome copy, overlay legends, crisp icon scale, tool
// hotkeys, history holes, Fast bursts, evaluation dashes, chrome fit,
// the default tool, the menu count, query wording, file glance, the
// message log, and the sprite cache.

#include "city_glance.hpp"
#include "city_seed.hpp"
#include "city_session.hpp"
#include "city_tail.hpp"
#include "controls_page.hpp"
#include "save_path.hpp"
#include "sound_player.hpp"
#include "text_edit.hpp"
#include "version.hpp"
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

#include "micropolis.h"

#include <cmath>
#include <cstdint>
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
    if (menu_table::actionable_count() != 51) {
        return fail(1, "the menu does not have 51 actionable rows");
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
        !history_point_skipped(run, 4, 1) || !history_point_skipped(run, 4, 2)) {
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
        const auto junk_path = dir / "not-a-city.cty";
        {
            std::ofstream junk(junk_path, std::ios::binary | std::ios::trunc);
            const std::vector<char> bytes(27120, '\xff');
            junk.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        const CityGlance junk = city_glance(junk_path.string());
        if (junk.ok || junk.summary != "Not a valid city" || !junk.swatch.empty()) {
            std::fprintf(stderr, "junk ok %d summary '%s'\n", junk.ok ? 1 : 0, junk.summary.c_str());
            return fail(19, "an invalid file still showed city details");
        }
        std::filesystem::remove(junk_path);
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

    not_reached(__LINE__, "round16");

    if (accepted_sidecar_score_type(42, 5) != 5 || accepted_sidecar_score_type(0, 5) != 5 ||
        accepted_sidecar_score_type(99, 99) != 0 || clamp_sidecar_score_wait(70000) != 32767 ||
        clamp_sidecar_score_wait(-4) != 0) {
        return fail(29, "sidecar score values were trusted");
    }
    ScenarioClock overflow;
    if (scenario_sidecar_parse("{\"scenario\":1,\"score_wait\":999999999999,\"score_type\":1,\"outcome\":0}", overflow) &&
        overflow.score_wait != 0) {
        return fail(29, "an overflowing sidecar number was accepted");
    }
    {
        const auto crash_path = dir / "crash.cty";
        std::filesystem::copy_file(city_path, crash_path, std::filesystem::copy_options::overwrite_existing);
        std::ofstream side(std::string(crash_path.string()) + ".json", std::ios::trunc);
        side << "{\"scenario\":5,\"score_wait\":70000,\"score_type\":42,\"outcome\":0,\"month\":1,\"year\":1957}\n";
        side.close();
        CitySession crashed;
        if (!crashed.load_city(crash_path.string())) {
            std::fprintf(stderr, "sidecar load: %s\n", crashed.last_load_error().c_str());
            return fail(30, "a bad sidecar score type rejected the city");
        }
        if (hostile_review_session_probe(crashed, 23) != 5 || hostile_review_session_probe(crashed, 22) != 32767) {
            std::fprintf(stderr, "type %d wait %d\n", hostile_review_session_probe(crashed, 23),
                         hostile_review_session_probe(crashed, 22));
            return fail(30, "a bad sidecar score was copied into the simulator");
        }
        for (int i = 0; i < 80; ++i) {
            crashed.tick();
        }
        if (crashed.city_name().empty()) {
            return fail(30, "ticking a bad sidecar score stopped the city");
        }
        std::filesystem::remove(std::string(crash_path.string()) + ".json");
        std::filesystem::remove(crash_path);
    }
    {
        const auto huge = dir / "huge.cty";
        {
            std::ofstream out(huge, std::ios::binary | std::ios::trunc);
            out.seekp(static_cast<std::streamoff>(kMaxCityBytes + 1));
            out.put('\0');
        }
        const std::string huge_problem = CitySession::load_problem(huge.string());
        if (huge_problem.find("too large") == std::string::npos) {
            std::fprintf(stderr, "huge: %s\n", huge_problem.c_str());
            return fail(31, "an oversized city file was not rejected");
        }
        CitySession huge_session;
        if (huge_session.load_city(huge.string()) || huge_session.last_load_error().find("too large") == std::string::npos) {
            return fail(31, "loading an oversized city did not explain itself");
        }
        const auto tiny = dir / "tiny.cty";
        {
            std::ofstream out(tiny, std::ios::binary | std::ios::trunc);
            const char bytes[] = "not a city";
            out.write(bytes, sizeof(bytes) - 1);
        }
        if (CitySession::load_problem(tiny.string()).find("too short") == std::string::npos) {
            return fail(32, "a truncated city file was not rejected");
        }
        const auto junk = dir / "junk.cty";
        {
            std::ofstream out(junk, std::ios::binary | std::ios::trunc);
            const std::vector<char> bytes(27120, '\x7f');
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        CitySession junk_session;
        if (junk_session.load_city(junk.string()) || junk_session.last_load_error().find("isn't") == std::string::npos ||
            junk_session.last_load_error().find("Details:") == std::string::npos) {
            std::fprintf(stderr, "junk: %s\n", junk_session.last_load_error().c_str());
            return fail(32, "a corrupt city file did not use the friendly wording");
        }
        std::filesystem::remove(huge);
        std::filesystem::remove(tiny);
        std::filesystem::remove(junk);
    }
    {
        const std::string root = std::getenv("LUNDUKE_CITY_ASSETS") != nullptr ? std::getenv("LUNDUKE_CITY_ASSETS") : "";
        const auto rio = root + "/res/snro.888";
        const CityGlance glance = city_glance(rio);
        if (!glance.ok || glance.year != 9999 || glance.month != 0 || glance.summary.find("Jan 9999") == std::string::npos) {
            std::fprintf(stderr, "glance %d %d %s\n", glance.year, glance.month, glance.summary.c_str());
            return fail(33, "the load list did not clamp the scenario date");
        }
        const auto copy = dir / "rio-free.cty";
        std::filesystem::copy_file(rio, copy, std::filesystem::copy_options::overwrite_existing);
        CitySession scenario;
        if (!scenario.load_city(copy.string())) {
            std::fprintf(stderr, "scenario: %s\n", scenario.last_load_error().c_str());
            return fail(34, "a classic scenario file did not open");
        }
        const auto book = scenario.budget();
        if (book.road_percent != 100 || book.police_percent != 100 || book.fire_percent != 100) {
            std::fprintf(stderr, "funding %d %d %d\n", book.road_percent, book.police_percent, book.fire_percent);
            return fail(34, "a classic scenario file kept 0% funding");
        }
        std::filesystem::remove(copy);
    }
    if (!save_name_starts_with_dot(".secret") || save_name_starts_with_dot("secret")) {
        return fail(35, "a save name starting with a dot was allowed");
    }
    const unsigned key = terrain_preview_key(1, -1, -1, -1, -1);
    if (key == 0 || preview_seed_from_field("999999999999999999999") != 2147483647) {
        return fail(36, "the terrain preview key is not defined for a random knob or a huge seed");
    }
    const std::string pasted = sanitize_pasted_text(std::string("a\nb\t") + std::string(80, 'x'), 64);
    if (pasted.find('\n') != std::string::npos || pasted.find('\t') != std::string::npos || pasted.size() > 64 ||
        pasted.compare(0, 4, "a b ") != 0) {
        std::fprintf(stderr, "pasted '%s'\n", pasted.c_str());
        return fail(37, "pasted text kept a control character, ran words together, or was not truncated");
    }
    if (sanitize_pasted_text("a\n\nb\t\tc", 64) != "a b c") {
        return fail(37, "pasted breaks were not collapsed to one space");
    }
    const std::string clipped = sanitize_pasted_text(std::string(63, 'a') + "\xC3\xA9", 64);
    if (clipped.size() != 63 || clipped.find('\xC3') != std::string::npos) {
        return fail(37, "paste truncated in the middle of a UTF-8 character");
    }
    if (kCityNameMaxBytes != 48) {
        return fail(44, "the city name limit is not the engine's 48 bytes");
    }
    {
        const std::string crossed = std::string(47, 'A') + "\xC3\xA9";
        const std::string kept = utf8_prefix(crossed, kCityNameMaxBytes);
        if (kept.size() != 47 || kept.find('\xC3') != std::string::npos) {
            return fail(44, "a city name was cut in the middle of a character");
        }
        const std::string fits = std::string(46, 'A') + "\xC3\xA9";
        if (utf8_prefix(fits, kCityNameMaxBytes) != fits) {
            return fail(44, "a 48-byte city name lost its last character");
        }
        CitySession named;
        named.new_city(crossed, 1);
        if (named.city_name().size() > 48 || named.city_name().find('\xC3') != std::string::npos) {
            std::fprintf(stderr, "name '%s' (%zu)\n", named.city_name().c_str(), named.city_name().size());
            return fail(44, "a new city kept a split character in its name");
        }
        named.rename_city(fits);
        if (named.city_name() != fits) {
            std::fprintf(stderr, "renamed '%s'\n", named.city_name().c_str());
            return fail(44, "rename dropped a character that fits in 48 bytes");
        }
    }
    if (kCitySeedFieldMax != 10 || new_city_field_error(true, true) != nullptr ||
        std::string(new_city_field_error(false, true)) != "Check the name." ||
        std::string(new_city_field_error(true, false)) != "Check the seed." ||
        std::string(new_city_field_error(false, false)) != "Check the name and the seed.") {
        return fail(40, "the New City error does not name the bad field");
    }
    {
        CitySeedParse parsed;
        if (parse_city_seed("12345678901234567890", parsed) || !parse_city_seed("+999999999", parsed) ||
            parsed.value != 999999999) {
            return fail(40, "the seed field accepted a 20-digit value");
        }
    }
    if (legend_chip_px(CitySession::MapLayer::Commercial) != 28 ||
        legend_chip_px(CitySession::MapLayer::Industrial) != 28 ||
        legend_chip_px(CitySession::MapLayer::Residential) != 42 ||
        legend_chip_px(CitySession::MapLayer::Transportation) != 62) {
        return fail(41, "the Commercial or Industrial swatch outline is wider than the fill");
    }
    {
        CitySession quiet;
        quiet.new_city("Quiet", 2);
        quiet.set_sound_enabled(true);
        const auto sound_path = dir / "lunduke-round16-sound.cty";
        if (!quiet.save_city_as(sound_path.string())) {
            return fail(42, "could not save a city with sound on");
        }
        quiet.set_sound_enabled(false);
        if (!quiet.load_city(sound_path.string()) || quiet.sound_enabled()) {
            std::filesystem::remove(sound_path);
            return fail(42, "loading a city turned sound on");
        }
        quiet.set_sound_enabled(false);
        if (!quiet.save_city_as(sound_path.string())) {
            std::filesystem::remove(sound_path);
            return fail(42, "could not save a city with sound off");
        }
        quiet.set_sound_enabled(true);
        if (!quiet.load_city(sound_path.string()) || !quiet.sound_enabled()) {
            std::filesystem::remove(sound_path);
            return fail(42, "loading a city turned sound off");
        }
        std::filesystem::remove(sound_path);
    }
    {
        CitySession tokyo;
        if (!tokyo.load_scenario(CitySession::scenario_def(4).id) || hostile_review_session_probe(tokyo, 24) != 1) {
            return fail(43, "Tokyo's clock did not reach Time is up");
        }
        const std::string won = tokyo.scenario_outcome_text(true);
        const std::string lost = tokyo.scenario_outcome_text(false);
        auto count = [](const std::string &text, const std::string &needle) {
            int n = 0;
            for (std::size_t at = 0; (at = text.find(needle, at)) != std::string::npos; at += needle.size()) {
                ++n;
            }
            return n;
        };
        if (won.find("You did it! Tokyo met the goal.") == std::string::npos || won.find("Score 683.") == std::string::npos ||
            won.find("keep it above") != std::string::npos || count(won, "until 1962") != 1 || count(won, "Time is up") != 1 ||
            lost.find("Time is up. Tokyo missed the goal.") == std::string::npos || lost.find("Time's up") != std::string::npos ||
            count(lost, "until 1962") != 1 || count(lost, "Time is up") != 1) {
            std::fprintf(stderr, "won '%s'\nlost '%s'\n", won.c_str(), lost.c_str());
            return fail(43, "the win dialog repeats the goal or disagrees with Time is up");
        }
    }
    if (about_text().find("Version 2.0.0") == std::string::npos || about_text().find("Lunduke City 2 2") != std::string::npos ||
        about_text().find("Will Wright") == std::string::npos || about_text().find("Don Hopkins") == std::string::npos ||
        how_to_play_text().find("budget") == std::string::npos || how_to_play_text().find("power") == std::string::npos) {
        return fail(38, "about or how to play copy is missing");
    }
    if (kMaxVoices != 8 || kSoundCooldownMs != 80 || kAudioIdleCloseMs < 1000 || kMaxCityBytes < 27120) {
        return fail(39, "audio or city size limits moved");
    }

    std::filesystem::remove(city_path);
    std::filesystem::remove(side_path);
    return 0;
}

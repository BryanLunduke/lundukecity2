// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "assets.hpp"
#include "atomic_write.hpp"
#include "city_tail.hpp"
#include "dialog_layout.hpp"
#include "file_list.hpp"
#include "text_edit.hpp"
#include "list_scroll.hpp"
#include "map_fit.hpp"
#include "redraw.hpp"
#include "sound_player.hpp"
#include "ui_scale.hpp"
#include "window_fit.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

} // namespace

int main()
{
    // C1: a selection past the first screen scrolls into view, and the
    // offset never walks off the end of a short list.
    int scroll = 0;
    list_reveal(scroll, 30, 10, 40);
    if (scroll != 21) {
        return fail("selecting row 30 of 40 with 10 visible should scroll to 21, got " + std::to_string(scroll));
    }
    list_reveal(scroll, 0, 10, 40);
    if (scroll != 0) {
        return fail("selecting the first row should scroll back to 0");
    }
    list_reveal(scroll, 3, 10, 40);
    if (scroll != 0) {
        return fail("a row already on screen should not scroll");
    }
    scroll = 100;
    list_reveal(scroll, 2, 15, 8);
    if (scroll != 0) {
        return fail("a list shorter than the window should pin scroll at 0, got " + std::to_string(scroll));
    }
    if (list_max_scroll(40, 10) != 30 || list_max_scroll(4, 10) != 0) {
        return fail("max scroll");
    }
    if (list_index_from_track(0, 0, 100, 40) != 0 || list_index_from_track(100, 0, 100, 40) != 39) {
        return fail("track ends should map to the first and last row");
    }
    if (list_index_from_track(50, 0, 100, 11) != 5) {
        return fail("mid track should map to the middle row");
    }
    double ty = 0;
    double th = 0;
    list_thumb(21, 40, 10, 10, 200, ty, th);
    if (th >= 200.0 || ty <= 10.0) {
        return fail("thumb should sit below the top of a scrolled track");
    }
    list_thumb(0, 4, 10, 10, 200, ty, th);
    if (ty != 10.0 || th != 200.0) {
        return fail("a short list fills the track");
    }
    // H4: packaged search paths are absolute install locations, never the
    // process's current directory. A relative third_party/ fallback used to
    // load whatever tree happened to be in the launch folder.
    const std::vector<std::string> candidates = asset_candidates();
    bool saw_source_tree = false;
    const std::string source_rel = "third_party/micropolis-assets";
    const std::string source_win = "third_party\\micropolis-assets";
    for (const std::string &candidate : candidates) {
        if (candidate == source_rel || candidate == source_win) {
            return fail("asset search still probes the current directory");
        }
        const bool source = (candidate.size() > source_rel.size() &&
                             candidate.compare(candidate.size() - source_rel.size(), source_rel.size(), source_rel) == 0) ||
                            (candidate.size() > source_win.size() &&
                             candidate.compare(candidate.size() - source_win.size(), source_win.size(), source_win) == 0);
        if (source) {
            saw_source_tree = true;
        }
    }
    if (!saw_source_tree) {
        return fail("asset search does not walk up to the source tree");
    }
    if (asset_root().empty()) {
        return fail("asset root was not found beside the executable");
    }
    const std::string missing = asset_missing_text(candidates.empty() ? std::vector<std::string>{"/nowhere/tiles"} : candidates);
    if (missing.find("tiles.png") == std::string::npos || missing.find("Looked in:") == std::string::npos) {
        return fail("missing-asset text does not name the file and the paths");
    }
    if (missing.find("/nowhere/tiles") == std::string::npos && candidates.empty()) {
        return fail("missing-asset text dropped the searched path");
    }
    // A Deck session must put the desktop rectangle back, and must not invent
    // one from the fullscreen window when the file had none.
    {
        const ui_scale::StoredSettings loaded = ui_scale::settings_parse(
            "ui_scale=1.25\nwindow_x=40\nwindow_y=50\nwindow_w=1100\nwindow_h=700\nmaximized=1\n");
        if (!loaded.has_geometry || loaded.w != 1100 || loaded.h != 700) {
            return fail("settings did not keep the desktop window");
        }
        ui_scale::StoredSettings deck_save;
        deck_save.speed = 2;
        ui_scale::keep_desktop_geometry(deck_save, loaded, true, true, 0, 0, 1920, 1080, false);
        const std::string deck_text = ui_scale::settings_text(deck_save);
        if (deck_text.find("window_w=1100") == std::string::npos || deck_text.find("window_h=700") == std::string::npos ||
            deck_text.find("window_x=40") == std::string::npos || deck_text.find("window_y=50") == std::string::npos ||
            deck_text.find("maximized=1") == std::string::npos || deck_text.find("window_w=1920") != std::string::npos) {
            return fail("a Deck session replaced the desktop window");
        }
        ui_scale::StoredSettings empty;
        ui_scale::StoredSettings deck_empty;
        ui_scale::keep_desktop_geometry(deck_empty, empty, true, true, 0, 0, 1920, 1080, false);
        if (ui_scale::settings_text(deck_empty).find("window_") != std::string::npos) {
            return fail("a Deck session stored a window it did not load");
        }
        ui_scale::StoredSettings desktop;
        ui_scale::keep_desktop_geometry(desktop, loaded, false, true, 8, 9, 1280, 800, false);
        const std::string desktop_text = ui_scale::settings_text(desktop);
        if (desktop_text.find("window_w=1280") == std::string::npos || desktop_text.find("window_h=800") == std::string::npos) {
            return fail("a desktop session did not store the live window");
        }
    }
    // H5: scale follows the display unless the user set one, and the default
    // window fits the work area.
    if (std::abs(ui_scale::resolve_ui_scale(2.0f, 0.0f) - 2.0f) > 0.001f) {
        return fail("display scale was ignored");
    }
    if (std::abs(ui_scale::resolve_ui_scale(2.0f, 1.5f) - 1.5f) > 0.001f) {
        return fail("user scale did not override the display");
    }
    if (std::abs(ui_scale::resolve_ui_scale(0.0f, 0.0f) - 1.0f) > 0.001f) {
        return fail("a missing display scale should fall back to 1");
    }
    if (ui_scale::scaled_font("Sans", 13, 1.0f) != "Sans 13" || ui_scale::scaled_font("Sans Bold", 18, 2.0f) != "Sans Bold 36") {
        return fail("scaled font string");
    }
    const ui_scale::Placement laptop = ui_scale::fit_default_window(1280, 800, 0, 0, 1366, 768);
    if (laptop.w + 2 * window_fit::kFrameSide > 1366 || laptop.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768) {
        return fail("1366x768 window is taller than the work area");
    }
    if (laptop.y < 0 || laptop.x < 0) {
        return fail("fitted window origin is negative");
    }
    const ui_scale::Placement wide = ui_scale::fit_default_window(1280, 800, 0, 0, 1920, 1080);
    if (wide.w <= 1280 || wide.h < 800 || wide.w + 2 * window_fit::kFrameSide > 1920) {
        return fail("1080p window was not sized from the display");
    }
    const ui_scale::Placement pulled = ui_scale::clamp_saved_window(-400, 9000, 2000, 2000, 0, 0, 1366, 768);
    if (pulled.x < 0 || pulled.y < 0 || pulled.w > 1366 || pulled.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768) {
        return fail("saved window was not brought back on screen");
    }
    const std::string text = ui_scale::settings_text([] {
        ui_scale::StoredSettings settings;
        settings.has_geometry = true;
        settings.x = 40;
        settings.y = 50;
        settings.w = 1280;
        settings.h = 800;
        settings.maximized = true;
        settings.user_scale = 1.5f;
        return settings;
    }());
    const ui_scale::StoredSettings parsed = ui_scale::settings_parse(text + "future_key=yes\n");
    if (!parsed.has_geometry || parsed.x != 40 || parsed.y != 50 || parsed.w != 1280 || parsed.h != 800 || !parsed.maximized ||
        std::abs(parsed.user_scale - 1.5f) > 0.001f) {
        return fail("settings did not round-trip");
    }
    ui_scale::StoredSettings game;
    game.speed = 2;
    game.sound = 0;
    game.volume = 40;
    game.auto_budget = 1;
    game.pause_unfocused = 1;
    const ui_scale::StoredSettings game_parsed = ui_scale::settings_parse(ui_scale::settings_text(game));
    const std::string atomic_path = "review-atomic.txt";
    if (!write_text_atomic(atomic_path, "one\n") || !write_text_atomic(atomic_path, "two\n")) {
        return fail("atomic write failed");
    }
    std::string atomic_body;
    {
        // Close the file before deleting it. Windows refuses to remove a
        // file that still has a reader open.
        std::ifstream atomic_in(atomic_path);
        std::getline(atomic_in, atomic_body);
    }
    std::error_code remove_ec;
    std::filesystem::remove(atomic_path, remove_ec);
    if (atomic_body != "two") {
        return fail("atomic write did not replace the file");
    }
    if (write_text_atomic("", "x")) {
        return fail("atomic write accepted an empty path");
    }
    if (game_parsed.speed != 2 || game_parsed.sound != 0 || game_parsed.volume != 40 || game_parsed.auto_budget != 1 ||
        game_parsed.pause_unfocused != 1 || game_parsed.auto_bulldoze != -1) {
        return fail("game settings did not round-trip");
    }
    const DialogRect budget = place_dialog(1280, 800, 640, 470, 16);
    if (budget.w > 640.5 || budget.h > 470.5 || budget.x < 0 || budget.y < 0 || budget.x + budget.w > 1280 ||
        budget.y + budget.h > 800) {
        return fail("budget dialog does not fit 1280x800");
    }
    const DialogRect deck = place_dialog(1024, 640, 700, 420, 16);
    if (deck.w > 1024 - 32 || deck.h > 640 - 32 || deck.y + deck.h > 640) {
        return fail("evaluation dialog does not fit 1024x640");
    }
    if (std::abs(message_width(1920, 16, 420, 560) - 560) > 0.1 || std::abs(message_width(800, 16, 420, 560) - 560) > 0.1 ||
        std::abs(message_width(400, 16, 420, 560) - 368) > 0.1) {
        return fail("message width was not clamped");
    }
    if (std::abs(button_width(40, 32, 120) - 120) > 0.1 || std::abs(button_width(200, 32, 120) - 232) > 0.1) {
        return fail("button width");
    }
    std::vector<unsigned char> classic(kClassicCityBytes, 1);
    std::vector<unsigned char> zeros(kClassicCityBytes, 0);
    std::vector<unsigned char> complete = classic;
    const char *name = "Town";
    complete.push_back('L');
    complete.push_back('C');
    complete.push_back('N');
    complete.push_back('1');
    complete.push_back(4);
    complete.push_back(0);
    complete.insert(complete.end(), name, name + 4);
    complete.push_back('L');
    complete.push_back('C');
    complete.push_back('F');
    complete.push_back('1');
    complete.resize(complete.size() + 240 * 8 + 240, 0);
    complete.push_back('L');
    complete.push_back('C');
    complete.push_back('W');
    complete.push_back('1');
    complete.push_back(1);
    complete.push_back(0);
    complete.push_back(3);
    complete.push_back(0);
    complete.resize(complete.size() + 8 + 24, 0);
    std::vector<unsigned char> short_tail = complete;
    short_tail.resize(short_tail.size() - 10);
    if (classify_city_bytes(classic.data(), classic.size()) != CityFileKind::Classic ||
        classify_city_bytes(zeros.data(), zeros.size()) != CityFileKind::AllZero ||
        classify_city_bytes(complete.data(), complete.size()) != CityFileKind::Complete ||
        classify_city_bytes(short_tail.data(), short_tail.size()) != CityFileKind::Incomplete ||
        classify_city_bytes(nullptr, 0) != CityFileKind::TooShort) {
        return fail("city file tail was classified wrong");
    }
    const DialogRect fly = place_submenu(8, 320, 180, 32, 3, 480, 1280, 800, 180);
    const DialogRect low = place_submenu(8, 320, 760, 32, 4, 200, 1280, 800, 180);
    const DialogRect cramped = place_submenu(900, 320, 40, 32, 1, 500, 1280, 800, 180);
    if (std::abs(fly.y - 180) > 0.1 || fly.w < 480 || fly.x + fly.w > 1276 || low.y + low.h > 796 || cramped.x < 4 ||
        cramped.x + cramped.w > 1276 || std::abs(cramped.y - 40) > 0.1) {
        return fail("recent submenu did not follow the row or stay on screen");
    }
    std::vector<std::string> rows = {"city10.cty", "Notes/", "city2.cty", "a.cty", "..", "Zoo/"};
    std::sort(rows.begin(), rows.end(), file_row_less);
    if (rows.size() != 6 || rows[0] != ".." || rows[1] != "Notes/" || rows[2] != "Zoo/" || rows[3] != "a.cty" ||
        rows[4] != "city2.cty" || rows[5] != "city10.cty") {
        return fail("file list did not put .. first, then folders, then natural names");
    }
    if (!file_name_hidden(".git") || file_name_hidden("city.cty")) {
        return fail("dotfile filter");
    }
    std::string word = "caf\xc3\xa9";
    int caret = static_cast<int>(word.size());
    int anchor = caret;
    field_backspace(word, caret, anchor);
    if (word != "caf" || caret != 3) {
        return fail("backspace removed a byte of a UTF-8 character");
    }
    field_insert(word, caret, anchor, "\xc3\xa9s", 64);
    field_select_all(word, caret, anchor);
    if (field_insert(word, caret, anchor, std::string(80, 'x'), 64) || word != "caf\xc3\xa9s") {
        return fail("an over-long insert erased the selection");
    }
    field_insert(word, caret, anchor, "Z", 64);
    if (word != "Z" || caret != 1) {
        return fail("typing did not replace the selection");
    }
    int min_w = 0;
    int min_h = 0;
    ui_scale::window_minimum(1920, 1080, min_w, min_h);
    int tiny_w = 0;
    int tiny_h = 0;
    ui_scale::window_minimum(800, 500, tiny_w, tiny_h);
    if (min_w != 960 || min_h != 600 || tiny_w != 800 || tiny_h != 500) {
        return fail("window minimum");
    }
    if (tile_size_to_cover(1800, 900, 120, 100, 12, 6, 64) < 15 || tile_size_to_cover(1800, 900, 120, 100, 20, 6, 64) != 20 ||
        tile_size_to_cover(8000, 4000, 120, 100, 12, 6, 64) != 64 || map_letterbox(1800, 1440) != 180 ||
        map_letterbox(1000, 1440) != 0) {
        return fail("large windows do not cover the map");
    }
    double origin = 0;
    double span = 1.4;
    clamp_viewport_span(origin, span);
    if (origin != 0 || span != 1) {
        return fail("minimap viewport was not clamped to the land");
    }
    if (audio_queue_limit_bytes(22050, 1, 2, 50) != 2205 || audio_bytes_to_write(0, 2205, 80000) != 2205 ||
        audio_bytes_to_write(2205, 2205, 800) != 0 || audio_bytes_to_write(2000, 2205, 800) != 205) {
        return fail("audio streams are not paced to 50 ms");
    }
    if (!text_has_path_separator("a/b") || text_has_path_separator("ab")) {
        return fail("path separator check");
    }
    if (default_cities_dir().find("Lunduke City 2") == std::string::npos) {
        return fail("default city folder is not under Documents");
    }
    const DialogRect popped = place_near(1280, 800, 480, 160, 1200, 700, 16);
    if (popped.x < 16 || popped.y < 16 || popped.x + popped.w > 1280 - 16 || popped.y + popped.h > 800 - 16) {
        return fail("query popup escaped the window");
    }
    redraw::Reasons idle;
    if (redraw::needed(idle)) {
        return fail("idle frame should not redraw");
    }
    redraw::Reasons running;
    running.sim_running = true;
    redraw::Reasons blink;
    blink.blink = true;
    if (!redraw::needed(running) || !redraw::needed(blink)) {
        return fail("simulation and blink frames should redraw");
    }
    const ui_scale::StoredSettings unknown = ui_scale::settings_parse("not_a_window=1\nui_scale=0\n");
    if (unknown.has_geometry || unknown.user_scale != 0.0f) {
        return fail("unknown settings keys were not ignored");
    }
    std::cout << "review ok\n";
    return 0;
}

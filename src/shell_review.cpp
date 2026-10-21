// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "city_tail.hpp"
#include "dialog_layout.hpp"
#include "file_list.hpp"
#include "text_edit.hpp"
#include "list_scroll.hpp"
#include "redraw.hpp"
#include "text_paint.hpp"
#include "tile_atlas.hpp"
#include "version.hpp"
#include "ui_scale.hpp"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

void Shell::run_review_checks()
{
    auto fail = [&](const std::string &message) {
        ran_ok_ = false;
        smoke_error_ = message;
    };

    // C1: rows past the first screen are selectable, and the scroll offset
    // follows the selection. D-pad, wheel and Page Down share this path.
    files_.clear();
    for (int i = 0; i < 40; ++i) {
        files_.push_back("city-" + std::to_string(i) + ".cty");
    }
    files_save_ = false;
    dialog_ = Dialog::Files;
    list_index_ = 0;
    list_scroll_ = 0;
    files_visible_ = 10;
    focus_id_ = kListItem;
    if (!nudge_file_list(1, true, true) || list_index_ != 9) {
        fail("page down should move a full screen, index " + std::to_string(list_index_));
        return;
    }
    if (!nudge_file_list(30, false, true)) {
        fail("forced nudge did not move the file list");
        return;
    }
    reveal_file_list();
    if (list_index_ < 20 || list_scroll_ > list_index_ || list_index_ >= list_scroll_ + files_visible_) {
        fail("row " + std::to_string(list_index_) + " is not inside the scrolled window starting at " +
             std::to_string(list_scroll_));
        return;
    }
    const int before = list_index_;
    focus_id_ = kListItem + (list_index_ - list_scroll_);
    if (!nudge_file_list(-1, false, false) || list_index_ != before - 1) {
        fail("d-pad up on the list should move the selection");
        return;
    }
    // A click on the last visible row selects that file, not the row number.
    list_scroll_ = 21;
    list_index_ = 21;
    on_hotspot(kListItem + 4, 0, 0);
    if (list_index_ != 25) {
        fail("clicking visible row 4 while scrolled to 21 should select file 25, got " + std::to_string(list_index_));
        return;
    }
    dialog_ = Dialog::None;

    // H1: Budget Cancel restores the rates from when the window opened,
    // including outside a tax year where the sliders apply live.
    apply_action(Action::Budget);
    if (dialog_ != Dialog::Budget || budget_is_tax_) {
        fail("budget did not open for an ordinary year");
        return;
    }
    const int opened_tax = open_tax_;
    tax_ = opened_tax + 5 > 20 ? 20 : opened_tax + 5;
    session_.set_tax(tax_);
    on_dialog_button(kCancel);
    if (dialog_ != Dialog::None || session_.tax() != opened_tax) {
        fail("budget cancel kept tax " + std::to_string(session_.tax()) + " opened at " + std::to_string(opened_tax));
        return;
    }

    // H2: Save from the unsaved prompt on a city with no file must keep the
    // action that was waiting (Quit, New City, Load, ...).
    after_save_ = AfterSave::Quit;
    apply_action(Action::Save);
    if (after_save_ != AfterSave::Quit || (dialog_ != Dialog::Files && dialog_ != Dialog::FileWait)) {
        fail("save-as from the quit prompt dropped the pending quit");
        return;
    }
    after_save_ = AfterSave::Nothing;
    close_dialog();

    // H3: a failed save from the quit prompt must not make the next save quit.
    after_save_ = AfterSave::Quit;
    save_generation_ = city_generation_;
    finish_save("/no/such/lunduke-city-dir/city.cty");
    if (after_save_ != AfterSave::Nothing || dialog_ != Dialog::SaveError || quitting_) {
        fail("failed quit-save left the game armed to quit");
        return;
    }
    on_dialog_button(kOk);
    if (quitting_ || after_save_ != AfterSave::Nothing) {
        fail("dismissing the save error quit the game");
        return;
    }

    // H4: a missing tile sheet is reported once, not every frame.
    const int attempts = tile_atlas().load_attempts();
    tile_atlas().ensure_loaded();
    tile_atlas().ensure_loaded();
    if (tile_atlas().load_attempts() != attempts) {
        fail("map tiles were opened more than once");
        return;
    }
    if (!tile_atlas().loaded() && tile_atlas().load_error().find("tiles.png") == std::string::npos) {
        fail("missing tiles did not name tiles.png");
        return;
    }

    // H5: headless play stays at the classic size and a 1x scale. A saved
    // window must not be written from the test, and a short laptop work area
    // must shrink the default window.
    if (std::abs(ui_scale_ - 1.0f) > 0.01f || font_ui_ != "Sans 13") {
        fail("headless UI scale should stay 1, got " + font_ui_);
        return;
    }
    if (win_w_ != 1280 || win_h_ != 800) {
        fail("headless window should stay 1280x800");
        return;
    }
    if (allow_saved_settings()) {
        fail("review run would persist window settings");
        return;
    }
    const ui_scale::Placement laptop = ui_scale::fit_default_window(1280, 800, 0, 0, 1366, 768);
    if (laptop.h + window_fit::kFrameTop + window_fit::kFrameBottom > 768 || laptop.w + 2 * window_fit::kFrameSide > 1366) {
        fail("default window does not fit a 1366x768 work area");
        return;
    }
    const ui_scale::Placement desktop = ui_scale::fit_default_window(1280, 800, 0, 0, 1920, 1080);
    if (desktop.w < 1280 || desktop.h < 800) {
        fail("a 1080p display should not shrink the classic window");
        return;
    }

    // H6: a paused city reuses its frame and does not repaint when nothing changed.
    if (skipped_redraws_ < 1) {
        fail("paused city repainted every frame (" + std::to_string(redraws_) + " draws)");
        return;
    }
    if (surface_builds_ != 1) {
        fail("cairo surface was rebuilt for an unchanged window");
        return;
    }
    if (text_cache_entries() < 1) {
        fail("text layouts were not cached");
        return;
    }
    redraw::Reasons idle;
    if (redraw::needed(idle)) {
        fail("an idle frame asked for a redraw");
        return;
    }
    redraw::Reasons running;
    running.sim_running = true;
    if (!redraw::needed(running)) {
        fail("a running simulation did not ask for a redraw");
        return;
    }

    // M1: content-sized dialogs stay inside a Steam Deck and a short laptop window.
    dialog_ = Dialog::NewCity;
    new_page_ = 0;
    focus_id_ = kFieldName;
    field_a_ = "caf\xc3\xa9";
    field_caret_ = static_cast<int>(field_a_.size());
    field_anchor_ = field_caret_;
    backspace_field();
    if (field_a_ != "caf") {
        fail("field backspace split a UTF-8 character");
        return;
    }
    type_text("/");
    if (field_a_ != "caf" || dialog_error_.find("/") == std::string::npos) {
        fail("a slash was accepted in a city name");
        return;
    }
    dialog_error_.clear();
    close_dialog();
    files_.clear();
    files_.push_back("..");
    files_.push_back(".git/");
    files_.push_back("city10.cty");
    std::sort(files_.begin(), files_.end(), file_row_less);
    if (files_.empty() || files_[0] != ".." || file_name_hidden("city.cty")) {
        fail("file list lost the parent row");
        return;
    }
    // M4: Replace-file Cancel keeps the typed name and the folder being browsed.
    field_a_ = "Other";
    files_dir_ = "/tmp";
    after_save_ = AfterSave::Quit;
    dialog_ = Dialog::Overwrite;
    on_dialog_button(kCancel);
    if (field_a_ != "Other" || dialog_ != Dialog::Files || files_dir_ != "/tmp" || after_save_ != AfterSave::Quit) {
        fail("replace-file cancel dropped the typed name");
        return;
    }
    close_dialog();
    after_save_ = AfterSave::Nothing;

    // M5: unplugging clears stick drift, adopts another pad, then pauses.
    if (!attach_test_pad()) {
        return;
    }
    SDL_VirtualJoystickDesc spare;
    SDL_INIT_INTERFACE(&spare);
    spare.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    spare.naxes = SDL_GAMEPAD_AXIS_COUNT;
    spare.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    spare.name = "Lunduke City 2 Spare";
    const SDL_JoystickID spare_id = SDL_AttachVirtualJoystick(&spare);
    const SDL_JoystickID first_id = SDL_GetGamepadID(pad_);
    stick_x_ = 0.9f;
    pan_y_ = -0.6f;
    trig_r_ = 0.5f;
    pad_dragging_ = true;
    speed_ = 2;
    forget_gamepad(first_id);
    if (spare_id == 0 || pad_ == nullptr || SDL_GetGamepadID(pad_) != spare_id || stick_x_ != 0.0f || pan_y_ != 0.0f ||
        trig_r_ != 0.0f || pad_dragging_ || speed_ != 2) {
        fail("unplug did not adopt the spare controller");
        return;
    }
    SDL_DetachVirtualJoystick(first_id);
    forget_gamepad(spare_id);
    if (pad_ != nullptr || speed_ != 0 || stick_x_ != 0.0f || shown_message_ != "Controller disconnected") {
        fail("the last controller unplug did not pause");
        return;
    }
    SDL_DetachVirtualJoystick(spare_id);

    // M7: a closer zoom is kept, a too-small zoom grows to cover, gauges stay short.
    map_.set_tile_size(kMaxMapTile);
    layout();
    if (map_.tile_size() != kMaxMapTile) {
        fail("fitting the map shrank a closer zoom");
        return;
    }
    map_.set_tile_size(kMinMapTile);
    layout();
    if (map_.pixel_width() < view_w_ && map_.tile_size() < kMaxMapTile) {
        fail("the map left an empty band beside the city");
        return;
    }
    if (demand_h_ > dp(kMaxDemandPx)) {
        fail("demand gauges stretched with the window");
        return;
    }

    // M8: scenarios past the first screen are reachable.
    dialog_ = Dialog::Scenario;
    scenario_index_ = 0;
    list_index_ = 0;
    list_scroll_ = 0;
    files_visible_ = 3;
    focus_id_ = kListItem;
    if (!nudge_file_list(1, true, true) || scenario_index_ != 2) {
        fail("scenario list did not page");
        return;
    }
    nudge_file_list(20, false, true);
    if (scenario_index_ != CitySession::kScenarioCount - 1 || scenario_index_ < list_scroll_ ||
        scenario_index_ >= list_scroll_ + files_visible_) {
        fail("the last scenario is not on screen");
        return;
    }
    close_dialog();

    // M9: settings cancel restores volume, and a key clears gamepad glyphs.
    volume_ = 40;
    open_volume_ = 80;
    scale_percent_ = 0;
    open_scale_percent_ = 0;
    dialog_ = Dialog::Settings;
    on_dialog_button(kCancel);
    if (volume_ != 80 || dialog_ != Dialog::None) {
        fail("settings cancel did not restore the volume");
        return;
    }
    const int tax_before = session_.tax();
    session_.set_tax(tax_before == 20 ? 19 : 20);
    recovery_.path = "/no/such/lunduke-recovery.cty";
    dialog_ = Dialog::Recovery;
    on_dialog_button(kRecover);
    if (!session_.needs_save_prompt() || dialog_ != Dialog::Unsaved || after_save_ != AfterSave::Recover) {
        fail("recover replaced an unsaved city without asking");
        return;
    }
    close_dialog();
    after_save_ = AfterSave::Nothing;
    session_.set_tax(tax_before);
    session_.accept_preferences();

    const int zoom_before = map_.tile_size();
    dialog_ = Dialog::About;
    SDL_SetModState(SDL_KMOD_CTRL);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1;
    handle_event(wheel);
    SDL_SetModState(SDL_KMOD_NONE);
    if (map_.tile_size() != zoom_before) {
        fail("ctrl+wheel zoomed behind a dialog");
        return;
    }
    close_dialog();

    dialog_ = Dialog::LoadError;
    sync_title();
    const char *title = SDL_GetWindowTitle(window_);
    if (title == nullptr || std::string(title).find(kProductName) == std::string::npos) {
        fail("the load error dropped the product name from the title");
        return;
    }
    dialog_ = Dialog::None;
    sync_title();

    gamepad_nav_ = true;
    note_keyboard_input();
    if (gamepad_nav_) {
        fail("keyboard input left gamepad hints up");
        return;
    }

    dialog_ = Dialog::None;
    menu_.open_menu = 0;
    menu_.cursor = 2;
    menu_.recent_open = true;
    recent_.clear();
    recent_.push_back(std::string("/home/player/Cities/") + std::string(48, 'W') + ".cty");
    menu_.recent_count = static_cast<int>(recent_.size());
    render();
    const Hotspot *recent_row = nullptr;
    const Hotspot *flyout = nullptr;
    for (const Hotspot &spot : hot_) {
        if (spot.id == kMenuItem + 2) {
            recent_row = &spot;
        }
        if (spot.id == kRecentItem) {
            flyout = &spot;
        }
    }
    // The row hotspot is the menu line. The flyout's first city sits on that
    // line, inset by the panel's 4px padding, and grows past the old 340px cap.
    if (recent_row == nullptr || flyout == nullptr || std::abs(flyout->y - (recent_row->y + 4.0)) > 1.0 ||
        flyout->w < 360 || flyout->w > 1100 || flyout->x < 40) {
        fail("the recent submenu did not follow its row or widen for a long name");
        return;
    }
    menu_.close();
    recent_.clear();

    const auto saved_city = std::filesystem::temp_directory_path() / "lunduke-city2-l6.cty";
    std::filesystem::remove(saved_city);
    if (!session_.save_city_as(saved_city.string()) ||
        !session_.load_city(saved_city.string()) || session_.message() != "Loaded a saved city.") {
        fail("a complete save did not load cleanly");
        return;
    }
    std::ifstream saved_in(saved_city, std::ios::binary);
    std::vector<char> saved_bytes((std::istreambuf_iterator<char>(saved_in)), std::istreambuf_iterator<char>());
    saved_in.close();
    if (saved_bytes.size() <= 27120 || classify_city_path(saved_city.string()) != CityFileKind::Complete) {
        fail("a City 2 save was not recognised as complete");
        return;
    }
    {
        std::ofstream trimmed(saved_city, std::ios::binary | std::ios::trunc);
        trimmed.write(saved_bytes.data(), static_cast<std::streamsize>(saved_bytes.size() - 10));
    }
    if (!session_.load_city(saved_city.string()) || session_.message() == "Loaded a saved city." ||
        session_.message().find("missing") == std::string::npos) {
        fail("a short City 2 tail loaded without a warning");
        return;
    }
    {
        std::ofstream classic(saved_city, std::ios::binary | std::ios::trunc);
        classic.write(saved_bytes.data(), 27120);
    }
    if (!session_.load_city(saved_city.string()) || session_.message() != "Loaded a saved city.") {
        fail("a classic 27120-byte city warned about a missing tail");
        return;
    }
    {
        std::ofstream blank(saved_city, std::ios::binary | std::ios::trunc);
        const std::vector<char> zeros(27120, 0);
        blank.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    if (session_.load_city(saved_city.string()) || session_.message().find("Loaded a saved city.") != std::string::npos) {
        fail("an all-zero file loaded as a city");
        return;
    }
    std::filesystem::remove(saved_city);

    // M1: wrapped lines keep their measured height, so headings, fund labels,
    // and the Close button do not share pixels. Checked at 1x and at a scale
    // that wraps the evaluation sentences.
    {
        auto boxes_overlap = [](const TextSpan &a, double bx, double by, double bw, double bh) {
            return a.x < bx + bw - 0.5 && bx < a.x + a.w - 0.5 && a.y < by + bh - 0.5 && by < a.y + a.h - 0.5;
        };
        auto spans_clash = [&]() {
            for (std::size_t i = 0; i < text_spans_.size(); ++i) {
                for (std::size_t j = i + 1; j < text_spans_.size(); ++j) {
                    const TextSpan &b = text_spans_[j];
                    if (boxes_overlap(text_spans_[i], b.x, b.y, b.w, b.h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto button_clash = [&]() {
            for (const Hotspot &hot : hot_) {
                const bool button = hot.id == kClose || hot.id == kOk || hot.id == kCancel || hot.id == kCollect;
                if (!button) {
                    continue;
                }
                for (const TextSpan &span : text_spans_) {
                    if (boxes_overlap(span, hot.x, hot.y, hot.w, hot.h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto bar_clash = [&]() {
            for (const SliderGeom &slider : sliders_) {
                const double bar_y = slider.y + dp(6);
                const double bar_h = static_cast<double>(dp(16));
                for (const TextSpan &span : text_spans_) {
                    if (boxes_overlap(span, slider.x, bar_y, slider.w, bar_h)) {
                        return true;
                    }
                }
            }
            return false;
        };
        auto apply_scale = [&](float scale) {
            ui_scale_ = scale;
            font_ui_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
            font_body_ = ui_scale::scaled_font("Sans", 14, ui_scale_);
            font_title_ = ui_scale::scaled_font("Sans Bold", 18, ui_scale_);
            font_menu_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
            font_bold_ = ui_scale::scaled_font("Sans Bold", 13, ui_scale_);
        };
        const Dialog checks[] = {Dialog::Evaluation, Dialog::Budget, Dialog::About};
        const char *names[] = {"evaluation", "budget", "about"};
        const float scales[] = {1.0f, 1.77f};
        for (float scale : scales) {
            apply_scale(scale);
            for (int i = 0; i < 3; ++i) {
                dialog_ = checks[i];
                presented_ = false;
                render();
                if (text_spans_.empty()) {
                    fail(std::string(names[i]) + " recorded no text");
                    return;
                }
                if (spans_clash()) {
                    fail(std::string(names[i]) + " text overlaps at scale " + std::to_string(scale));
                    return;
                }
                if (button_clash()) {
                    fail(std::string(names[i]) + " text overlaps a button at scale " + std::to_string(scale));
                    return;
                }
                if (bar_clash()) {
                    fail(std::string(names[i]) + " text overlaps a slider at scale " + std::to_string(scale));
                    return;
                }
            }
        }
        apply_scale(1.0f);
        dialog_ = Dialog::None;
        presented_ = false;
    }

    // M9: a loaded city's speed and disasters do not become the global preferences.
    pref_speed_ = 1;
    pref_disasters_ = 0;
    session_.set_speed(3);
    session_.set_disasters(true);
    restore_play_preferences();
    if (session_.speed() != 1 || session_.disasters() || pref_speed_ != 1 || pref_disasters_ != 0) {
        fail("loading a city replaced the global speed or disasters preference");
        return;
    }
    session_.set_speed(0);

#ifndef _WIN32
    const auto previous_sigint = std::signal(SIGINT, SIG_DFL);
    std::signal(SIGINT, previous_sigint);
    if (previous_sigint == SIG_ERR || previous_sigint == SIG_DFL || previous_sigint == SIG_IGN) {
        fail("SIGINT is not hooked for autosave");
        return;
    }
#endif

    const auto shot = std::filesystem::temp_directory_path() / "lunduke-city2-l10.png";
    std::filesystem::remove(shot);
    shot_path_ = shot.string();
    render();
    render();
    if (std::filesystem::exists(shot)) {
        std::filesystem::remove(shot);
        fail("a screenshot was written before the last frame");
        return;
    }
    quitting_ = true;
    write_final_shot();
    const bool wrote_shot = std::filesystem::exists(shot);
    std::filesystem::remove(shot);
    quitting_ = false;
    if (!wrote_shot) {
        fail("the last frame was not written to the screenshot");
        return;
    }

    const DialogRect eval_deck = place_dialog(1280, 800, 700, 420, 16);
    const DialogRect budget_short = place_dialog(1024, 640, 640, 470, 16);
    if (eval_deck.y + eval_deck.h > 800 || budget_short.y + budget_short.h > 640 || budget_short.w > 1024 - 32) {
        fail("a content-sized dialog does not fit the small window");
        return;
    }
}

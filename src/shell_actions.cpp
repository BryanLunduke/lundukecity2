// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "atomic_write.hpp"
#include "city_seed.hpp"
#include "file_list.hpp"
#include "list_scroll.hpp"
#include "osk_layout.hpp"
#include "text_edit.hpp"
#include "platform.hpp"
#include "save_path.hpp"
#include "special_buildings.hpp"
#include "tools.hpp"
#include "version.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::string path_join(const std::string &dir, const std::string &name)
{
    if (dir.empty()) {
        return name;
    }
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (dir.back() == '/' || dir.back() == '\\') {
        return dir + name;
    }
    return dir + sep + name;
}

std::string parent_dir(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

bool ends_with_ci(const std::string &text, const char *suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (text.size() < n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[text.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool Shell::dialog_pauses() const
{
    switch (dialog_) {
    case Dialog::None:
    case Dialog::Graphs:
    case Dialog::Evaluation:
    case Dialog::Overlay:
    case Dialog::Query:
    case Dialog::Messages:
        return false;
    case Dialog::Budget:
        return budget_is_tax_;
    default:
        return true;
    }
}

void Shell::set_paused_for_dialog()
{
    session_.set_simulation_paused(dialog_pauses());
}

void Shell::close_dialog()
{
    const Dialog closing = dialog_;
    dialog_ = Dialog::None;
    dialog_error_.clear();
    osk_shift_ = false;
    if (text_input_on_ && window_ != nullptr) {
        SDL_StopTextInput(window_);
        text_input_on_ = false;
    }
    if (closing == Dialog::LoadError) {
        sync_title();
    }
    set_paused_for_dialog();
}

void Shell::dismiss_dialog()
{
    close_dialog();
    if (welcome_gate_ && dialog_ == Dialog::None) {
        open_dialog(Dialog::Welcome);
    }
}

void Shell::open_dialog(Dialog dialog)
{
    menu_.close();
    special_open_ = false;
    dialog_ = dialog;
    dialog_error_.clear();
    // A hover left over from the map must not light a dialog button.
    hover_id_ = 0;
    focus_id_ = kOk;
    if (dialog == Dialog::NewCity) {
        field_a_ = "New City";
        field_b_ = "auto";
        new_page_ = 0;
        new_level_ = CitySession::kLevelEasy;
        new_island_ = CitySession::kTerrainDefault;
        new_rivers_ = CitySession::kTerrainDefault;
        new_lakes_ = CitySession::kTerrainDefault;
        new_trees_ = CitySession::kTerrainDefault;
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
    } else if (dialog == Dialog::Rename) {
        field_a_ = session_.city_name();
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
    } else if (dialog == Dialog::Files && files_save_) {
        field_a_ = proposed_city_filename(session_.city_name(), session_.save_path());
        focus_id_ = kFieldName;
        field_select_all(field_a_, field_caret_, field_anchor_);
    } else if (dialog == Dialog::Unsaved) {
        focus_id_ = kSave;
    } else if (dialog == Dialog::Disaster || dialog == Dialog::RecoveryDelete || dialog == Dialog::Overwrite) {
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Recovery) {
        focus_id_ = kRecover;
    } else if (dialog == Dialog::Scenario) {
        focus_id_ = kListItem;
        list_index_ = scenario_index_;
    } else if (dialog == Dialog::Files && !files_save_) {
        focus_id_ = files_.empty() ? kUp : kListItem;
    } else if (dialog == Dialog::Welcome) {
        focus_id_ = kWelcomeNew;
    } else if (dialog == Dialog::QuitConfirm) {
        // Cancel is the focused button, so a stray A does not leave the game.
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Outcome) {
        focus_id_ = kKeep;
    } else if (dialog == Dialog::Budget) {
        focus_id_ = kSlider + 1;
    } else if (dialog == Dialog::Messages) {
        focus_id_ = session_.notices().empty() ? kClose : kListItem;
        list_index_ = 0;
        list_scroll_ = 0;
    } else if (dialog == Dialog::Controls) {
        controls_page_ = 0;
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Settings) {
        open_volume_ = volume_;
        open_wheel_zooms_ = wheel_zooms_;
        open_fullscreen_ = window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
        open_scale_percent_ = user_scale_ <= 0.05f ? 0 : static_cast<int>(std::lround(user_scale_ * 100.0f));
        if (open_scale_percent_ < 0) {
            open_scale_percent_ = 0;
        }
        scale_percent_ = open_scale_percent_;
        open_pause_unfocused_ = pause_unfocused_;
        focus_id_ = kSlider + 1;
    }
    set_paused_for_dialog();
}

void Shell::sync_title()
{
    if (window_ == nullptr) {
        return;
    }
    if (dialog_ == Dialog::LoadError) {
        const std::string failed = std::string(kProductName) + " - Could not load city";
        SDL_SetWindowTitle(window_, failed.c_str());
        return;
    }
    const std::string title = std::string(kProductName) + " - " + session_.city_name();
    SDL_SetWindowTitle(window_, title.c_str());
}

void Shell::note_message()
{
    sync_title();
}

void Shell::set_speed(int speed)
{
    speed_ = speed;
    session_.set_speed(speed);
    if (session_.outcome_pause_pending() && speed > 0) {
        session_.resume_after_outcome();
        speed_ = session_.speed();
    }
    pref_speed_ = speed_;
}

void Shell::toggle_pause()
{
    if (speed_ == 0) {
        set_speed(paused_from_ > 0 ? paused_from_ : 2);
    } else {
        paused_from_ = speed_;
        set_speed(0);
    }
}

int Shell::special_tool_index() const
{
    for (int i = 0; i < kToolCount; ++i) {
        if (tool_by_index(i)->engine_id == kSpecialEngineId) {
            return i;
        }
    }
    return kToolCount - 1;
}

void Shell::select_tool(int index)
{
    if (index < 0) {
        index = kToolCount - 1;
    }
    if (index >= kToolCount) {
        index = 0;
    }
    const int special = special_tool_index();
    const bool entering = index == special && tool_index_ != index;
    const bool leaving = tool_index_ == special && index != special;
    tool_index_ = index;
    palette_.set_selected(index);
    map_.set_tool(tool_by_index(index)->engine_id);
    if (entering) {
        menu_.close();
        special_open_ = true;
        if (special_cursor_ < 0 || special_cursor_ >= special_building_count()) {
            special_cursor_ = special_choice_ >= 0 ? special_choice_ : 0;
        }
    }
    if (leaving) {
        special_open_ = false;
    }
}

void Shell::cycle_tool(int delta)
{
    if (special_open_) {
        special_open_ = false;
    }
    select_tool(tool_index_ + delta);
}

void Shell::press_palette(double x, double y)
{
    const int index = palette_.index_at(x, y);
    if (index >= 0 && index == tool_index_ && tool_by_index(index)->engine_id == kSpecialEngineId) {
        special_open_ = !special_open_;
        if (special_open_) {
            menu_.close();
            if (special_cursor_ < 0 || special_cursor_ >= special_building_count()) {
                special_cursor_ = special_choice_ >= 0 ? special_choice_ : 0;
            }
        }
        return;
    }
    palette_.press(x, y);
}

void Shell::move_special_cursor(int delta)
{
    const int count = special_building_count();
    if (count < 1) {
        return;
    }
    int next = special_cursor_ + delta;
    if (next < 0) {
        next = count - 1;
    }
    if (next >= count) {
        next = 0;
    }
    special_cursor_ = next;
}

void Shell::choose_special(int index)
{
    if (index < 0 || index >= special_building_count()) {
        return;
    }
    special_choice_ = index;
    special_cursor_ = index;
    const int slot = special_tool_index();
    if (tool_index_ != slot) {
        select_tool(slot);
    }
    special_open_ = false;
    map_.set_tool(kSpecialEngineId);
}

void Shell::clamp_scroll()
{
    const double max_x = std::max(0, map_.pixel_width() - view_w_);
    const double max_y = std::max(0, map_.pixel_height() - view_h_);
    scroll_x_ = std::max(0.0, std::min(max_x, scroll_x_));
    scroll_y_ = std::max(0.0, std::min(max_y, scroll_y_));
}

void Shell::fit_map_to_view()
{
    // 8px is the overview. It is allowed to letterbox so the whole city stays
    // in view. Every larger zoom still grows until the window is covered, and
    // a closer zoom is never shrunk.
    const int cover = map_.tile_size() <= kMapTileOverview
                          ? map_.tile_size()
                          : tile_size_to_cover(view_w_, view_h_, CitySession::kWorldW, CitySession::kWorldH,
                                               map_.tile_size(), kMapTileSource, kMaxMapTile);
    if (cover != map_.tile_size()) {
        const double ax = view_x_ + view_w_ * 0.5;
        const double ay = view_y_ + view_h_ * 0.5;
        const double fx = map_point_fraction(ax, view_x_, map_ox_, scroll_x_, map_.pixel_width());
        const double fy = map_point_fraction(ay, view_y_, map_oy_, scroll_y_, map_.pixel_height());
        map_.set_tile_size(cover);
        scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox(view_w_, map_.pixel_width()), map_.pixel_width());
        scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox(view_h_, map_.pixel_height()), map_.pixel_height());
        clamp_scroll();
    }
    map_ox_ = map_letterbox(view_w_, map_.pixel_width());
    map_oy_ = map_letterbox(view_h_, map_.pixel_height());
}

double Shell::map_local_x(double x) const
{
    // Same device pixel the map was drawn on. A fractional scroll is floored
    // at paint time; hit testing has to follow that or an 8px tile boundary
    // lands on the neighbour.
    return x - snapped_map_origin(view_x_, map_ox_, scroll_x_);
}

double Shell::map_local_y(double y) const
{
    return y - snapped_map_origin(view_y_, map_oy_, scroll_y_);
}

void Shell::center_on_fraction(double fx, double fy)
{
    const double ax = view_x_ + view_w_ * 0.5;
    const double ay = view_y_ + view_h_ * 0.5;
    scroll_x_ = scroll_for_map_fraction(fx, ax, view_x_, map_letterbox(view_w_, map_.pixel_width()), map_.pixel_width());
    scroll_y_ = scroll_for_map_fraction(fy, ay, view_y_, map_letterbox(view_h_, map_.pixel_height()), map_.pixel_height());
    clamp_scroll();
}

void Shell::zoom_by(int delta)
{
    zoom_by(delta, view_x_ + view_w_ * 0.5, view_y_ + view_h_ * 0.5);
}

void Shell::zoom_by(int delta, double anchor_x, double anchor_y)
{
    const int before = map_.tile_size();
    // Read the anchor before the tile size changes. The grey band is not
    // part of the city; a centred 8px map would otherwise zoom in about
    // 13 tiles to the right of the tile under the view centre.
    const double fx = map_point_fraction(anchor_x, view_x_, map_ox_, scroll_x_, map_.pixel_width());
    const double fy = map_point_fraction(anchor_y, view_y_, map_oy_, scroll_y_, map_.pixel_height());
    map_.set_tile_size(step_map_tile(before, delta));
    if (map_.tile_size() == before) {
        return;
    }
    const int ox = map_letterbox(view_w_, map_.pixel_width());
    const int oy = map_letterbox(view_h_, map_.pixel_height());
    scroll_x_ = scroll_for_map_fraction(fx, anchor_x, view_x_, ox, map_.pixel_width());
    scroll_y_ = scroll_for_map_fraction(fy, anchor_y, view_y_, oy, map_.pixel_height());
    clamp_scroll();
    map_ox_ = ox;
    map_oy_ = oy;
}

void Shell::clamp_gamepad_cursor(float push_x, float push_y)
{
    if (!gamepad_nav_ || view_w_ < 1 || view_h_ < 1) {
        return;
    }
    const double view_r = static_cast<double>(view_x_) + view_w_;
    const double view_b = static_cast<double>(view_y_) + view_h_;
    const double x = cursor_x_;
    const double y = cursor_y_;
    if (x < view_x_ || x >= view_r || y < view_y_ || y >= view_b) {
        return;
    }
    const double map_left = snapped_map_origin(view_x_, map_ox_, scroll_x_);
    const double map_top = snapped_map_origin(view_y_, map_oy_, scroll_y_);
    const double vis_left = std::max(static_cast<double>(view_x_), map_left);
    const double vis_top = std::max(static_cast<double>(view_y_), map_top);
    const double vis_right = std::min(view_r, map_left + map_.pixel_width());
    const double vis_bottom = std::min(view_b, map_top + map_.pixel_height());
    cursor_x_ = static_cast<float>(clamp_cursor_off_letterbox(x, push_x, view_x_, view_r, vis_left, vis_right));
    cursor_y_ = static_cast<float>(clamp_cursor_off_letterbox(y, push_y, view_y_, view_b, vis_top, vis_bottom));
}

void Shell::toggle_fullscreen()
{
    if (window_ == nullptr) {
        return;
    }
    const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window_, !full);
}

void Shell::toggle_overlay(CitySession::MapLayer layer)
{
    if (overlay_on_ && overlay_.layer() == layer) {
        overlay_on_ = false;
        return;
    }
    overlay_.set_layer(layer);
    overlay_on_ = true;
    overlay_.sync();
}

void Shell::reroll_terrain()
{
    const unsigned salt = static_cast<unsigned>(SDL_GetTicks()) * 1103515245u + 12345u;
    const int seed = 1000 + static_cast<int>(salt % 900000);
    field_b_ = std::to_string(seed);
    terrain_key_ = 0;
}

void Shell::jump_notice(int index)
{
    const auto &notes = session_.notices();
    if (index < 0 || index >= static_cast<int>(notes.size())) {
        return;
    }
    const CitySession::CityNotice &note = notes[static_cast<std::size_t>(index)];
    if (!note.has_place) {
        return;
    }
    center_on_fraction((note.tile_x + 0.5) / CitySession::kWorldW, (note.tile_y + 0.5) / CitySession::kWorldH);
    if (dialog_ == Dialog::Messages) {
        close_dialog();
    }
}

void Shell::arm_touch_tool()
{
    if (!touch_pending_ || touch_armed_) {
        return;
    }
    touch_armed_ = true;
    touch_pending_ = false;
    pointer_on_map_ = true;
    session_.begin_provisional_stroke();
    map_.press(map_local_x(touch_down_x_), map_local_y(touch_down_y_));
    map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), true);
}

void Shell::load_recent()
{
    recent_.clear();
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::ifstream in(dir + "/recent.txt");
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (path_is_absolute(line)) {
            recent_.push_back(line);
        }
    }
    menu_.recent_count = static_cast<int>(recent_.size());
}

void Shell::remember_recent(const std::string &path)
{
    if (!path_is_absolute(path)) {
        return;
    }
    recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > 10) {
        recent_.resize(10);
    }
    menu_.recent_count = static_cast<int>(recent_.size());
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string body;
    for (const std::string &item : recent_) {
        body += item;
        body += '\n';
    }
    if (!write_text_atomic(dir + "/recent.txt", body)) {
        note_background_write_failed("Could not update the recent-cities list.");
    }
}

void Shell::drop_recovery()
{
    city_recovery::remove(own_recovery_);
    own_recovery_.clear();
    autosave_fingerprint_.clear();
}

void Shell::autosave(bool force)
{
    if (!session_.needs_save_prompt()) {
        return;
    }
    const std::string fingerprint = session_.change_fingerprint();
    if (!force && fingerprint == autosave_fingerprint_ && !own_recovery_.empty()) {
        return;
    }
    if (own_recovery_.empty()) {
        own_recovery_ = city_recovery::new_recovery_path();
        if (own_recovery_.empty()) {
            note_background_write_failed("Autosave is off. This city will not be recovered if the game closes.");
            return;
        }
    }
    if (!session_.write_recovery(own_recovery_)) {
        note_background_write_failed("Autosave failed. This city will not be recovered if the game closes.");
        return;
    }
    city_recovery::write_info(own_recovery_, session_.city_name(), session_.save_path(), session_.date_text());
    autosave_fingerprint_ = fingerprint;
}

void Shell::remember_saved(const std::string &path)
{
    remember_city_folder(path);
    remember_recent(path);
    drop_recovery();
    sync_title();
}

void Shell::finish_save(const std::string &path)
{
    if (path.empty()) {
        return;
    }
    if (save_generation_ != city_generation_) {
        info_title_ = "Save City";
        info_body_ = "The city changed while the Save dialog was open. Nothing was saved.";
        open_dialog(Dialog::Info);
        return;
    }
    if (!session_.save_city_as(path)) {
        info_title_ = "Save City";
        info_body_ = session_.message().empty() ? "Could not save the city." : session_.message();
        // A failed save must not leave Quit (or New/Load) armed for the next Ctrl+S.
        after_save_ = AfterSave::Nothing;
        open_dialog(Dialog::SaveError);
        return;
    }
    remember_saved(path);
    const AfterSave next = after_save_;
    after_save_ = AfterSave::Nothing;
    if (next != AfterSave::Nothing) {
        proceed(next);
    }
}

void Shell::finish_open(const std::string &path)
{
    if (path.empty() || !session_.load_city(path)) {
        std::string reason = session_.last_load_error();
        if (reason.empty()) {
            reason = path.empty() ? "That file has no local path." : CitySession::load_problem(path);
        }
        if (reason.empty()) {
            reason = "The file could not be read as a city. It may be damaged.";
        }
        info_title_ = "Could not load city";
        info_body_ = reason + "\n\n" + path;
        open_dialog(Dialog::LoadError);
        sync_title();
        return;
    }
    welcome_gate_ = false;
    ++city_generation_;
    restore_play_preferences();
    center_on_fraction(0.5, 0.5);
    const std::string folder = parent_dir(path);
    if (!folder.empty()) {
        remember_city_folder(folder);
    }
    remember_recent(path);
    sync_title();
}

void Shell::refresh_files()
{
    files_.clear();
    std::error_code ec;
    if (files_dir_.empty() || !std::filesystem::is_directory(files_dir_, ec)) {
        const std::string remembered = remembered_city_folder();
        if (!remembered.empty()) {
            files_dir_ = remembered;
        } else {
            files_dir_ = default_cities_dir();
        }
    }
    if (!files_dir_.empty() && !std::filesystem::is_directory(files_dir_, ec) && !headless_ && !smoke_ && !ui_test_ &&
        !review_) {
        std::filesystem::create_directories(files_dir_, ec);
    }
    if (std::filesystem::is_directory(files_dir_, ec)) {
        for (const auto &entry : std::filesystem::directory_iterator(files_dir_, ec)) {
            if (ec) {
                break;
            }
            const std::string name = entry.path().filename().string();
            if (name.empty() || name == "." || name == ".." || file_name_hidden(name)) {
                continue;
            }
            if (entry.is_directory(ec)) {
                files_.push_back(name + "/");
            } else if (ends_with_ci(name, ".cty")) {
                files_.push_back(name);
            }
        }
    }
    std::sort(files_.begin(), files_.end(), file_row_less);
    if (list_index_ >= static_cast<int>(files_.size())) {
        list_index_ = 0;
    }
    if (list_index_ < 0) {
        list_index_ = 0;
    }
    reveal_file_list();
}

void Shell::begin_files(bool save)
{
    files_save_ = save;
    save_generation_ = city_generation_;
    const std::string current = session_.save_path();
    if (!current.empty()) {
        files_dir_ = parent_dir(current);
    } else {
        files_dir_ = remembered_city_folder();
    }
    list_index_ = 0;
    list_scroll_ = 0;
    refresh_files();
    open_dialog(Dialog::Files);
}

void Shell::begin_load()
{
    if (deck_ || headless_) {
        begin_files(false);
        return;
    }
    begin_system_files(false);
}

void Shell::begin_save_as()
{
    if (deck_ || headless_) {
        begin_files(true);
        return;
    }
    begin_system_files(true);
}

void Shell::begin_system_files(bool save)
{
    files_save_ = save;
    save_generation_ = city_generation_;
    file_ready_ = false;
    file_state_ = 0;
    file_path_.clear();
    std::string folder = session_.save_path().empty() ? remembered_city_folder() : parent_dir(session_.save_path());
    static const SDL_DialogFileFilter filters[] = {{"City files", "cty"}, {"All files", "*"}};
    open_dialog(Dialog::FileWait);
    if (save) {
        SDL_ShowSaveFileDialog(&Shell::file_callback, this, window_, filters, 2,
                               folder.empty() ? nullptr : folder.c_str());
    } else {
        SDL_ShowOpenFileDialog(&Shell::file_callback, this, window_, filters, 2,
                               folder.empty() ? nullptr : folder.c_str(), false);
    }
}

void Shell::poll_file_dialog()
{
    if (dialog_ != Dialog::FileWait) {
        return;
    }
    std::string path;
    int state = 0;
    {
        std::lock_guard<std::mutex> lock(file_mu_);
        if (!file_ready_) {
            return;
        }
        state = file_state_;
        path = file_path_;
        file_ready_ = false;
    }
    close_dialog();
    if (state == 1) {
        if (files_save_) {
            finish_save(with_cty_suffix(path));
        } else {
            finish_open(path);
        }
        return;
    }
    if (state == 3) {
        begin_files(files_save_);
        return;
    }
    // The player dismissed the system dialog. Do not quit on a later save.
    after_save_ = AfterSave::Nothing;
    if (welcome_gate_ && !files_save_ && dialog_ == Dialog::None) {
        open_dialog(Dialog::Welcome);
    }
}

void Shell::proceed(AfterSave next)
{
    switch (next) {
    case AfterSave::NewCity:
        open_dialog(Dialog::NewCity);
        break;
    case AfterSave::Load:
        begin_load();
        break;
    case AfterSave::FilesLoad:
        begin_files(false);
        break;
    case AfterSave::Scenario:
        scenario_index_ = 0;
        list_index_ = 0;
        list_scroll_ = 0;
        open_dialog(Dialog::Scenario);
        break;
    case AfterSave::Quit:
        drop_recovery();
        quitting_ = true;
        break;
    case AfterSave::OpenPath:
        finish_open(queued_path_);
        queued_path_.clear();
        break;
    case AfterSave::Recover:
        if (!session_.load_recovery(recovery_.path, recovery_.origin)) {
            info_title_ = "Recover city";
            info_body_ = session_.last_load_error().empty() ? "The recovery file could not be read."
                                                            : session_.last_load_error();
            open_dialog(Dialog::Info);
            break;
        }
        own_recovery_ = recovery_.path;
        ++city_generation_;
        autosave_fingerprint_ = session_.change_fingerprint();
        set_speed(0);
        center_on_fraction(0.5, 0.5);
        sync_title();
        break;
    case AfterSave::Nothing:
        break;
    }
}

void Shell::request_with_save(AfterSave next)
{
    if (!session_.needs_save_prompt()) {
        if (next == AfterSave::Quit || next == AfterSave::NewCity || next == AfterSave::Load ||
            next == AfterSave::Scenario || next == AfterSave::OpenPath) {
            drop_recovery();
        }
        proceed(next);
        return;
    }
    after_save_ = next;
    open_dialog(Dialog::Unsaved);
}

void Shell::offer_recovery()
{
    const std::vector<city_recovery::Entry> entries = city_recovery::list(own_recovery_);
    if (entries.empty()) {
        return;
    }
    recovery_ = entries.front();
    recovery_left_ = static_cast<int>(entries.size()) - 1;
    open_dialog(Dialog::Recovery);
}

void Shell::apply_action(Action action)
{
    switch (action) {
    case Action::NewCity:
        request_with_save(AfterSave::NewCity);
        break;
    case Action::Load:
        request_with_save(AfterSave::Load);
        break;
    case Action::Recent:
        request_with_save(AfterSave::FilesLoad);
        break;
    case Action::Save:
        if (session_.save_path().empty()) {
            // A Save from "Save changes?" has no file yet. Keep the pending
            // Quit / New / Load so it still runs after Save As succeeds.
            begin_save_as();
        } else if (!session_.save_city_as(session_.save_path())) {
            info_title_ = "Save City";
            info_body_ = session_.message().empty() ? "Could not save the city." : session_.message();
            after_save_ = AfterSave::Nothing;
            open_dialog(Dialog::SaveError);
        } else {
            remember_saved(session_.save_path());
            const AfterSave next = after_save_;
            after_save_ = AfterSave::Nothing;
            if (next != AfterSave::Nothing) {
                proceed(next);
            }
        }
        break;
    case Action::SaveAs:
        after_save_ = AfterSave::Nothing;
        begin_save_as();
        break;
    case Action::Scenario:
        request_with_save(AfterSave::Scenario);
        break;
    case Action::Rename:
        open_dialog(Dialog::Rename);
        break;
    case Action::Quit:
        request_with_save(AfterSave::Quit);
        break;
    case Action::AutoBudget:
        session_.set_auto_budget(!session_.auto_budget());
        save_window_settings();
        break;
    case Action::AutoBulldoze:
        session_.set_auto_bulldoze(!session_.auto_bulldoze());
        save_window_settings();
        break;
    case Action::DisastersEnabled:
        session_.set_disasters(!session_.disasters());
        pref_disasters_ = session_.disasters() ? 1 : 0;
        save_window_settings();
        break;
    case Action::AutoGoto:
        session_.set_auto_goto(!session_.auto_goto());
        save_window_settings();
        break;
    case Action::Mute:
        session_.set_sound_enabled(!session_.sound_enabled());
        sound_.set_muted(!session_.sound_enabled());
        save_window_settings();
        break;
    case Action::SetPause:
        paused_from_ = speed_ > 0 ? speed_ : paused_from_;
        set_speed(0);
        save_window_settings();
        break;
    case Action::Slow:
        set_speed(1);
        save_window_settings();
        break;
    case Action::Medium:
        set_speed(2);
        save_window_settings();
        break;
    case Action::Fast:
        set_speed(3);
        save_window_settings();
        break;
    case Action::TogglePause:
        toggle_pause();
        save_window_settings();
        break;
    case Action::ZoomIn:
        zoom_by(2);
        break;
    case Action::ZoomOut:
        zoom_by(-2);
        break;
    case Action::Fire:
    case Action::Flood:
    case Action::Tornado:
    case Action::Earthquake:
    case Action::Monster:
    case Action::Meltdown:
        disaster_ = action;
        open_dialog(Dialog::Disaster);
        break;
    case Action::Budget: {
        const auto book = session_.budget();
        tax_ = book.tax_percent;
        road_ = book.road_percent;
        police_ = book.police_percent;
        fire_ = book.fire_percent;
        open_tax_ = tax_;
        open_road_ = road_;
        open_police_ = police_;
        open_fire_ = fire_;
        budget_is_tax_ = session_.budget_pending();
        open_dialog(Dialog::Budget);
        break;
    }
    case Action::Graphs:
        graphs_long_ = false;
        open_dialog(Dialog::Graphs);
        break;
    case Action::Evaluation:
        session_.update_evaluation();
        open_dialog(Dialog::Evaluation);
        break;
    case Action::Power:
        toggle_overlay(CitySession::MapLayer::Power);
        break;
    case Action::Water:
        toggle_overlay(CitySession::MapLayer::Zones);
        break;
    case Action::Zones:
        toggle_overlay(CitySession::MapLayer::Zones);
        break;
    case Action::Pollution:
        toggle_overlay(CitySession::MapLayer::Pollution);
        break;
    case Action::Crime:
        toggle_overlay(CitySession::MapLayer::Crime);
        break;
    case Action::LandValue:
        toggle_overlay(CitySession::MapLayer::LandValue);
        break;
    case Action::Traffic:
        toggle_overlay(CitySession::MapLayer::Traffic);
        break;
    case Action::PoliceMap:
        toggle_overlay(CitySession::MapLayer::Police);
        break;
    case Action::FireMap:
        toggle_overlay(CitySession::MapLayer::Fire);
        break;
    case Action::Population:
        toggle_overlay(CitySession::MapLayer::Population);
        break;
    case Action::Growth:
        toggle_overlay(CitySession::MapLayer::Growth);
        break;
    case Action::Messages:
        list_index_ = 0;
        list_scroll_ = 0;
        open_dialog(Dialog::Messages);
        break;
    case Action::Fullscreen:
        toggle_fullscreen();
        break;
    case Action::About:
        open_dialog(Dialog::About);
        break;
    case Action::Settings:
        open_dialog(Dialog::Settings);
        break;
    case Action::Controls:
        open_dialog(Dialog::Controls);
        break;
    case Action::RecentIndex:
    case Action::None:
        break;
    }
}

void Shell::on_dialog_button(int id)
{
    if (dialog_ == Dialog::Settings) {
        if (id == kReset) {
            pause_unfocused_ = !pause_unfocused_;
            return;
        }
        if (id == kScaleAuto) {
            scale_percent_ = scale_percent_ <= 0 ? 100 : 0;
            return;
        }
        if (id == kWheelZoom) {
            wheel_zooms_ = !wheel_zooms_;
            return;
        }
        if (id == kFullscreenBox) {
            toggle_fullscreen();
            return;
        }
        if (id == kCancel) {
            volume_ = open_volume_;
            scale_percent_ = open_scale_percent_;
            pause_unfocused_ = open_pause_unfocused_;
            wheel_zooms_ = open_wheel_zooms_;
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            if (window_ != nullptr) {
                const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                if (full != open_fullscreen_) {
                    toggle_fullscreen();
                }
            }
            refresh_ui_scale();
            close_dialog();
            return;
        }
        if (id == kOk || id == kSave) {
            user_scale_ = scale_percent_ <= 0 ? 0.0f : static_cast<float>(scale_percent_) / 100.0f;
            sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            refresh_ui_scale();
            save_window_settings();
            close_dialog();
            return;
        }
    }
    if (dialog_ == Dialog::Controls) {
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Welcome) {
        if (id == kWelcomeNew) {
            open_dialog(Dialog::NewCity);
            return;
        }
        if (id == kWelcomeLoad) {
            begin_load();
            return;
        }
        if (id == kWelcomeScenario) {
            scenario_index_ = 0;
            list_index_ = 0;
            list_scroll_ = 0;
            open_dialog(Dialog::Scenario);
            return;
        }
        // Quit, B, and Escape ask first. A second B from New City or the
        // scenario list must not leave the game.
        open_dialog(Dialog::QuitConfirm);
        return;
    }
    if (dialog_ == Dialog::QuitConfirm) {
        if (id == kOk) {
            welcome_gate_ = false;
            close_dialog();
            request_with_save(AfterSave::Quit);
            return;
        }
        dismiss_dialog();
        return;
    }
    if (id == kSystemFiles) {
        const bool save = files_save_;
        close_dialog();
        begin_system_files(save);
        return;
    }
    if (id == kUp) {
        files_dir_ = parent_dir(files_dir_);
        list_index_ = 0;
        list_scroll_ = 0;
        refresh_files();
        return;
    }
    if (id == kShortScale) {
        graphs_long_ = false;
        return;
    }
    if (id == kLongScale) {
        graphs_long_ = true;
        return;
    }
    if (dialog_ == Dialog::Unsaved) {
        if (id == kSave) {
            close_dialog();
            apply_action(Action::Save);
            return;
        }
        if (id == kDiscard) {
            session_.discard_pending_budget();
            drop_recovery();
            const AfterSave next = after_save_;
            after_save_ = AfterSave::Nothing;
            close_dialog();
            proceed(next);
            return;
        }
        after_save_ = AfterSave::Nothing;
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Disaster) {
        if (id == kOk) {
            const Action disaster = disaster_;
            close_dialog();
            switch (disaster) {
            case Action::Fire:
                session_.disaster_fire();
                break;
            case Action::Flood:
                session_.disaster_flood();
                break;
            case Action::Tornado:
                session_.disaster_tornado();
                break;
            case Action::Earthquake:
                session_.disaster_earthquake();
                break;
            case Action::Monster:
                session_.disaster_monster();
                break;
            case Action::Meltdown:
                session_.disaster_meltdown();
                break;
            default:
                break;
            }
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        if (id == kCancel) {
            dismiss_dialog();
            return;
        }
        if (id == kBack) {
            if (new_page_ > 0) {
                --new_page_;
            }
            return;
        }
        if (id == kNext && new_page_ == 0) {
            if (!CitySession::name_is_usable(field_a_)) {
                dialog_error_ = "Enter a name that is not only spaces.";
                return;
            }
            dialog_error_.clear();
            new_page_ = 1;
            focus_id_ = kFieldSeed;
            return;
        }
        if (id == kNext && new_page_ == 1) {
            int seed = 0;
            bool set = false;
            if (!take_city_seed(field_b_, seed, set)) {
                dialog_error_ = "Enter a whole number, or leave the field blank.";
                return;
            }
            dialog_error_.clear();
            new_page_ = 2;
            focus_id_ = kGenerate;
            return;
        }
        if (id == kGenerate) {
            int seed = 0;
            bool set = false;
            if (!take_city_seed(field_b_, seed, set) || !CitySession::name_is_usable(field_a_)) {
                dialog_error_ = "Check the name and the seed.";
                return;
            }
            CitySession::NewCitySpec spec;
            spec.name = field_a_;
            spec.seed = seed;
            spec.seed_was_set = set;
            spec.difficulty = new_level_;
            spec.island = new_island_;
            spec.rivers = new_rivers_;
            spec.lakes = new_lakes_;
            spec.trees = new_trees_;
            welcome_gate_ = false;
            close_dialog();
            const int serial = session_.message_serial();
            ++city_generation_;
            session_.new_city(spec);
            if (session_.message_serial() == serial) {
                restore_play_preferences();
            }
            center_on_fraction(0.5, 0.5);
            sync_title();
        }
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        if (id == kPlay) {
            const int index = scenario_index_;
            close_dialog();
            if (index >= 0 && index < CitySession::kScenarioCount) {
                const int id_num = CitySession::scenario_def(index).id;
                ++city_generation_;
                if (!session_.load_scenario(id_num)) {
                    info_title_ = "Play Scenario";
                    info_body_ = "Could not start that scenario.";
                    open_dialog(Dialog::Info);
                } else {
                    welcome_gate_ = false;
                    speed_ = session_.speed();
                    center_on_fraction(0.5, 0.5);
                    sync_title();
                }
            }
            return;
        }
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Rename) {
        if (id == kRename || id == kOk) {
            if (!CitySession::name_is_usable(field_a_)) {
                dialog_error_ = "Enter a name that is not only spaces.";
                return;
            }
            session_.rename_city(field_a_);
            close_dialog();
            sync_title();
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Recovery) {
        if (id == kRecover) {
            const city_recovery::Entry entry = recovery_;
            close_dialog();
            recovery_ = entry;
            request_with_save(AfterSave::Recover);
            return;
        }
        if (id == kDelete) {
            open_dialog(Dialog::RecoveryDelete);
            return;
        }
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::RecoveryDelete) {
        if (id == kOk || id == kDelete) {
            city_recovery::remove(recovery_.path);
            close_dialog();
            offer_recovery();
            return;
        }
        offer_recovery();
        return;
    }
    if (dialog_ == Dialog::Files) {
        if (id == kOk || id == kSave || id == kPlay) {
            if (files_save_) {
                if (text_has_path_separator(field_a_)) {
                    dialog_error_ = "That name cannot contain / or \\.";
                    return;
                }
                const std::string path = with_cty_suffix(path_join(files_dir_, field_a_));
                std::error_code ec;
                if (std::filesystem::exists(path, ec) && path != session_.save_path()) {
                    queued_path_ = path;
                    open_dialog(Dialog::Overwrite);
                    return;
                }
                close_dialog();
                finish_save(path);
                return;
            }
            if (list_index_ >= 0 && list_index_ < static_cast<int>(files_.size())) {
                const std::string name = files_[static_cast<std::size_t>(list_index_)];
                if (name == "..") {
                    files_dir_ = parent_dir(files_dir_);
                    list_index_ = 0;
                    list_scroll_ = 0;
                    refresh_files();
                    return;
                }
                if (!name.empty() && name.back() == '/') {
                    files_dir_ = path_join(files_dir_, name.substr(0, name.size() - 1));
                    list_index_ = 0;
                    list_scroll_ = 0;
                    refresh_files();
                    return;
                }
                const std::string path = path_join(files_dir_, name);
                close_dialog();
                finish_open(path);
            }
            return;
        }
        dismiss_dialog();
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        if (id == kOk || id == kSave) {
            const std::string path = queued_path_;
            close_dialog();
            finish_save(path);
            return;
        }
        // Cancel returns to Save As with the name and folder the player typed.
        const std::string typed = field_a_;
        const std::string dir = files_dir_;
        const AfterSave pending = after_save_;
        begin_files(true);
        after_save_ = pending;
        if (!dir.empty()) {
            files_dir_ = dir;
            list_index_ = 0;
            list_scroll_ = 0;
            refresh_files();
        }
        field_a_ = typed;
        field_select_all(field_a_, field_caret_, field_anchor_);
        focus_id_ = kFieldName;
        return;
    }
    if (dialog_ == Dialog::Budget) {
        if (id == kReset) {
            tax_ = open_tax_;
            road_ = open_road_;
            police_ = open_police_;
            fire_ = open_fire_;
            session_.restore_budget_rates(tax_, road_, police_, fire_);
            return;
        }
        if (id == kCollect || id == kOk) {
            session_.set_tax(tax_);
            session_.set_road_funding(road_);
            session_.set_police_funding(police_);
            session_.set_fire_funding(fire_);
            session_.finish_budget_edit();
            budget_is_tax_ = false;
            close_dialog();
            return;
        }
        // Cancel outside a tax year used to keep the live slider edits.
        // Both paths restore the rates the window opened with.
        session_.restore_budget_rates(open_tax_, open_road_, open_police_, open_fire_);
        tax_ = open_tax_;
        road_ = open_road_;
        police_ = open_police_;
        fire_ = open_fire_;
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Outcome) {
        close_dialog();
        if (id == kKeep) {
            if (session_.outcome_pause_pending()) {
                session_.resume_after_outcome();
                speed_ = session_.speed();
            }
        } else if (id == kScenarioList) {
            if (session_.outcome_pause_pending()) {
                session_.stay_paused_after_outcome();
                speed_ = session_.speed();
            }
            request_with_save(AfterSave::Scenario);
        } else if (session_.outcome_pause_pending()) {
            session_.stay_paused_after_outcome();
            speed_ = session_.speed();
        }
        return;
    }
    if (dialog_ == Dialog::Query) {
        query_pinned_ = false;
        hint_until_ = std::chrono::steady_clock::now();
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Files || dialog_ == Dialog::FileWait || dialog_ == Dialog::SaveError) {
        after_save_ = AfterSave::Nothing;
    }
    dismiss_dialog();
}

void Shell::on_hotspot(int id, double x, double y)
{
    if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
        if (id >= kSpecialItem && id < kSpecialItem + special_building_count()) {
            choose_special(id - kSpecialItem);
            return;
        }
        if (id == kHotNone) {
            special_open_ = false;
            return;
        }
    }
    if (id >= kMenuTitle && id < kMenuTitle + 5 && dialog_ == Dialog::None) {
        special_open_ = false;
        const int column = id - kMenuTitle;
        if (menu_.open_menu == column) {
            menu_.close();
        } else {
            menu_.open_menu = column;
            menu_.cursor = 0;
            menu_.alt_shown = true;
            menu_.recent_open = false;
        }
        return;
    }
    if (id >= kMenuItem && id < kRecentItem) {
        const int packed = id - kMenuItem;
        menu_.open_menu = packed / 40;
        menu_.cursor = packed % 40;
        menu_.activate_current();
        if (menu_.activated == Action::RecentIndex && menu_.activated_recent >= 0 &&
            menu_.activated_recent < static_cast<int>(recent_.size())) {
            queued_path_ = recent_[static_cast<std::size_t>(menu_.activated_recent)];
            request_with_save(AfterSave::OpenPath);
        } else if (menu_.activated != Action::None) {
            apply_action(menu_.activated);
        }
        return;
    }
    if (id >= kRecentItem && id < kRecentItem + 20) {
        const int index = id - kRecentItem;
        if (index >= 0 && index < static_cast<int>(recent_.size())) {
            menu_.close();
            queued_path_ = recent_[static_cast<std::size_t>(index)];
            request_with_save(AfterSave::OpenPath);
        }
        return;
    }
    if (id >= kListItem && id < kListItem + 64) {
        const int row = id - kListItem;
        if (dialog_ == Dialog::Files) {
            const int picked = list_scroll_ + row;
            if (picked == list_index_ && picked >= 0 && picked < static_cast<int>(files_.size())) {
                focus_id_ = id;
                activate_file_row();
                return;
            }
            list_index_ = picked;
            if (list_index_ >= static_cast<int>(files_.size())) {
                list_index_ = static_cast<int>(files_.size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
        } else if (dialog_ == Dialog::Scenario) {
            const int picked = list_scroll_ + row;
            if (picked == scenario_index_) {
                focus_id_ = id;
                on_dialog_button(kPlay);
                return;
            }
            scenario_index_ = picked;
            if (scenario_index_ >= CitySession::kScenarioCount) {
                scenario_index_ = CitySession::kScenarioCount - 1;
            }
            if (scenario_index_ < 0) {
                scenario_index_ = 0;
            }
            list_index_ = scenario_index_;
            reveal_file_list();
        } else if (dialog_ == Dialog::Messages) {
            const int picked = list_scroll_ + row;
            list_index_ = picked;
            if (list_index_ >= static_cast<int>(session_.notices().size())) {
                list_index_ = static_cast<int>(session_.notices().size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
            jump_notice(list_index_);
        } else {
            list_index_ = row;
        }
        focus_id_ = id;
        return;
    }
    if (id == kListScroll && (dialog_ == Dialog::Files || dialog_ == Dialog::Scenario)) {
        list_dragging_ = true;
        const int count =
            dialog_ == Dialog::Scenario ? CitySession::kScenarioCount : static_cast<int>(files_.size());
        const int index = list_index_from_track(y, list_track_y_, list_track_h_, count);
        if (dialog_ == Dialog::Scenario) {
            scenario_index_ = index;
        }
        list_index_ = dialog_ == Dialog::Scenario ? scenario_index_ : index;
        reveal_file_list();
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
        return;
    }
    if (id == kFieldName || id == kFieldSeed) {
        focus_id_ = id;
        return;
    }
    if (id >= kOsk && id < kOsk + 64) {
        int count = 0;
        const OskKey *keys = osk_keys(count);
        const int index = id - kOsk;
        if (index < 0 || index >= count) {
            return;
        }
        const OskKey &key = keys[index];
        focus_id_ = id;
        if (key.kind == OskKind::Backspace) {
            backspace_field();
        } else if (key.kind == OskKind::Space) {
            type_text(" ");
        } else if (key.kind == OskKind::Auto) {
            if (active_field() != nullptr) {
                static const char *names[] = {
                    "Pine Harbor", "Red Mesa", "Oak Crossing", "Cedar Bluff", "Silver Ford",
                    "Little Falls", "West Ridge", "Copper Bend", "Green Hollow", "Stone Ferry",
                };
                const unsigned salt = static_cast<unsigned>(SDL_GetTicks());
                int pick = static_cast<int>(salt % 10);
                if (pick == osk_last_name_) {
                    pick = (pick + 1) % 10;
                }
                osk_last_name_ = pick;
                *active_field() = names[pick];
                field_caret_ = static_cast<int>(active_field()->size());
                field_anchor_ = field_caret_;
            }
        } else if (key.kind == OskKind::Shift) {
            osk_shift_ = !osk_shift_;
        } else if (key.text != nullptr) {
            const char *insert = (osk_shift_ && key.text_shift != nullptr) ? key.text_shift : key.text;
            type_text(insert);
        }
        return;
    }
    if (id >= kGraphSeries && id < kGraphSeries + 16 && dialog_ == Dialog::Graphs) {
        graph_mask_ ^= 1 << (id - kGraphSeries);
        focus_id_ = id;
        return;
    }
    if (id == kNewTerrain && dialog_ == Dialog::NewCity) {
        reroll_terrain();
        focus_id_ = id;
        return;
    }
    if (id >= kSlider && id < kSlider + 8) {
        slider_drag_ = id;
        for (const SliderGeom &slider : sliders_) {
            if (slider.id != id || slider.w <= 1) {
                continue;
            }
            const double t = std::max(0.0, std::min(1.0, (x - slider.x) / slider.w));
            int value = slider.min + static_cast<int>(std::lround(t * (slider.max - slider.min)));
            if (dialog_ == Dialog::Settings) {
                if (id == kSlider + 1) {
                    volume_ = value;
                    sound_.set_volume(static_cast<float>(volume_) / 100.0f);
                } else if (id == kSlider + 2 && scale_percent_ > 0) {
                    int snapped = 50 + static_cast<int>(std::lround((value - 50) / 25.0) * 25);
                    if (snapped < 50) {
                        snapped = 50;
                    }
                    if (snapped > 200) {
                        snapped = 200;
                    }
                    scale_percent_ = snapped;
                }
            } else if (id == kSlider + 1) {
                tax_ = value;
                session_.set_tax(tax_);
            } else if (id == kSlider + 2) {
                road_ = value;
                session_.set_road_funding(road_);
            } else if (id == kSlider + 3) {
                police_ = value;
                session_.set_police_funding(police_);
            } else if (id == kSlider + 4) {
                fire_ = value;
                session_.set_fire_funding(fire_);
            }
        }
        focus_id_ = id;
        return;
    }
    if (id >= kChoice && id < kChoice + 50) {
        const int packed = id - kChoice;
        const int group = packed / 10;
        const int option = packed % 10;
        if (group == 0) {
            new_level_ = option;
        } else if (group == 1) {
            new_island_ = option == 0 ? CitySession::kTerrainDefault
                                      : option == 1 ? CitySession::kTerrainOff : CitySession::kIslandAlways;
        } else if (group == 2) {
            new_rivers_ = option == 0 ? CitySession::kTerrainDefault
                                      : option == 1 ? CitySession::kTerrainOff : CitySession::kRiversCurvy;
        } else if (group == 3) {
            new_lakes_ = option == 0 ? CitySession::kTerrainDefault
                                     : option == 1 ? CitySession::kTerrainOff : CitySession::kLakesMany;
        } else if (group == 4) {
            new_trees_ = option == 0 ? CitySession::kTerrainDefault
                                     : option == 1 ? CitySession::kTerrainOff : CitySession::kTreesWooded;
        }
        focus_id_ = id;
        return;
    }
    if (dialog_ != Dialog::None && is_dialog_button(id)) {
        on_dialog_button(id);
        return;
    }
    if (menu_.open() && dialog_ == Dialog::None &&
        (id == kHotNone || id == kMapId || id == kPaletteId || id == kMinimapId)) {
        menu_.close();
        return;
    }
    if (dialog_ != Dialog::None && id == 0) {
        if (dialog_ == Dialog::Graphs || dialog_ == Dialog::Evaluation || dialog_ == Dialog::Overlay ||
            dialog_ == Dialog::Query) {
            close_dialog();
        }
    } else if (menu_.open() && id == 0) {
        menu_.close();
    }
}

bool Shell::show_osk() const
{
    return gamepad_nav_ || deck_ || touch_used_;
}

std::string *Shell::active_field()
{
    if (dialog_ == Dialog::NewCity && (focus_id_ == kFieldName || focus_id_ == kFieldSeed || new_page_ == 0)) {
        if (focus_id_ == kFieldSeed || (new_page_ == 1 && focus_id_ != kFieldName)) {
            if (focus_id_ == kFieldSeed) {
                return &field_b_;
            }
        }
        if (new_page_ == 0 || focus_id_ == kFieldName) {
            return &field_a_;
        }
    }
    if (dialog_ == Dialog::Rename && (focus_id_ == kFieldName || focus_id_ == 0)) {
        return &field_a_;
    }
    if (dialog_ == Dialog::Files && files_save_ && focus_id_ == kFieldName) {
        return &field_a_;
    }
    if ((dialog_ == Dialog::NewCity && new_page_ == 1 && focus_id_ == kFieldSeed) ||
        (dialog_ == Dialog::Rename) || (dialog_ == Dialog::Files && files_save_ && focus_id_ == kFieldName) ||
        (dialog_ == Dialog::NewCity && new_page_ == 0)) {
        return focus_id_ == kFieldSeed ? &field_b_ : &field_a_;
    }
    return nullptr;
}

void Shell::select_field()
{
    std::string *field = active_field();
    if (field == nullptr) {
        return;
    }
    field_select_all(*field, field_caret_, field_anchor_);
}

void Shell::type_text(const std::string &text)
{
    std::string *field = active_field();
    if (field == nullptr || text.empty()) {
        return;
    }
    if (field == &field_a_ && text_has_path_separator(text)) {
        dialog_error_ = "That name cannot contain / or \\.";
        return;
    }
    if (!field_insert(*field, field_caret_, field_anchor_, text, 64)) {
        dialog_error_ = "That name is too long.";
    } else if (dialog_error_ == "That name cannot contain / or \\." || dialog_error_ == "That name is too long.") {
        dialog_error_.clear();
    }
}

void Shell::backspace_field()
{
    std::string *field = active_field();
    if (field == nullptr || field->empty()) {
        return;
    }
    field_backspace(*field, field_caret_, field_anchor_);
}

void Shell::move_field_caret(int direction, bool extend, bool home, bool end)
{
    std::string *field = active_field();
    if (field == nullptr) {
        return;
    }
    if (home) {
        field_home(*field, field_caret_, field_anchor_, extend);
        return;
    }
    if (end) {
        field_end(*field, field_caret_, field_anchor_, extend);
        return;
    }
    field_move(*field, field_caret_, field_anchor_, direction, extend);
}

void Shell::paste_field()
{
    char *clip = SDL_GetClipboardText();
    if (clip == nullptr) {
        return;
    }
    type_text(clip);
    SDL_free(clip);
}

int Shell::hit(double x, double y) const
{
    for (int i = static_cast<int>(hot_.size()) - 1; i >= 0; --i) {
        const Hotspot &hot = hot_[static_cast<std::size_t>(i)];
        if (x >= hot.x && y >= hot.y && x < hot.x + hot.w && y < hot.y + hot.h) {
            return hot.id;
        }
    }
    return 0;
}

void Shell::push_hot(double x, double y, double w, double h, int id)
{
    next_hot_.push_back(Hotspot{x, y, w, h, id});
}

void Shell::push_focus(int id)
{
    // The menu and the map stay on screen under a modal. They must not join
    // the D-pad cycle.
    if (dialog_ != Dialog::None && !is_modal_control(id)) {
        return;
    }
    next_focus_.push_back(id);
}

bool Shell::move_osk_vertically(int direction)
{
    if (direction == 0 || focus_id_ < kOsk || focus_id_ >= kOsk + 64) {
        return false;
    }
    int count = 0;
    const OskKey *keys = osk_keys(count);
    const int index = focus_id_ - kOsk;
    if (index < 0 || index >= count || count > 64) {
        return false;
    }
    double center[64] = {};
    char present[64] = {};
    for (const Hotspot &hot : hot_) {
        if (hot.id < kOsk || hot.id >= kOsk + count) {
            continue;
        }
        const int key = hot.id - kOsk;
        center[key] = hot.x + hot.w * 0.5;
        present[key] = 1;
    }
    if (present[index] == 0) {
        return false;
    }
    const int target = osk_vertical_target(keys, count, index, direction, center, present);
    if (target < 0) {
        return false;
    }
    focus_id_ = kOsk + target;
    return true;
}

void Shell::move_focus(int delta)
{
    if (focus_order_.empty()) {
        return;
    }
    int index = 0;
    for (int i = 0; i < static_cast<int>(focus_order_.size()); ++i) {
        if (focus_order_[static_cast<std::size_t>(i)] == focus_id_) {
            index = i;
            break;
        }
    }
    index = (index + delta + static_cast<int>(focus_order_.size())) % static_cast<int>(focus_order_.size());
    focus_id_ = focus_order_[static_cast<std::size_t>(index)];
    if (focus_id_ >= kListItem && focus_id_ < kListItem + 64) {
        if (dialog_ == Dialog::Files) {
            list_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (list_index_ >= static_cast<int>(files_.size())) {
                list_index_ = static_cast<int>(files_.size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
        } else if (dialog_ == Dialog::Messages) {
            list_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (list_index_ >= static_cast<int>(session_.notices().size())) {
                list_index_ = static_cast<int>(session_.notices().size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
        } else if (dialog_ == Dialog::Scenario) {
            scenario_index_ = list_scroll_ + (focus_id_ - kListItem);
            if (scenario_index_ >= CitySession::kScenarioCount) {
                scenario_index_ = CitySession::kScenarioCount - 1;
            }
            if (scenario_index_ < 0) {
                scenario_index_ = 0;
            }
            list_index_ = scenario_index_;
            reveal_file_list();
        } else {
            list_index_ = focus_id_ - kListItem;
        }
    }
}

void Shell::reveal_file_list()
{
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages;
    const int count = scenarios ? CitySession::kScenarioCount
                                : messages ? static_cast<int>(session_.notices().size())
                                           : static_cast<int>(files_.size());
    const int index = scenarios ? scenario_index_ : list_index_;
    list_reveal(list_scroll_, index, std::max(1, files_visible_), count);
    if (scenarios) {
        list_index_ = scenario_index_;
    }
}

bool Shell::nudge_file_list(int delta, bool page, bool force)
{
    const bool files = dialog_ == Dialog::Files && !files_.empty();
    const bool scenarios = dialog_ == Dialog::Scenario;
    const bool messages = dialog_ == Dialog::Messages && !session_.notices().empty();
    if ((!files && !scenarios && !messages) || delta == 0) {
        return false;
    }
    const bool on_list = focus_id_ >= kListItem && focus_id_ < kListItem + 64;
    if (!on_list && !force && !page) {
        return false;
    }
    const int count = scenarios ? CitySession::kScenarioCount
                               : messages ? static_cast<int>(session_.notices().size())
                                          : static_cast<int>(files_.size());
    int index = scenarios ? scenario_index_ : list_index_;
    const int step = page ? std::max(1, files_visible_ - 1) : std::max(1, std::abs(delta));
    const int sign = delta < 0 ? -1 : 1;
    int next = index + sign * step;
    if (!page && !force && on_list && (next < 0 || next >= count)) {
        return false;
    }
    if (next < 0) {
        next = 0;
    }
    if (next >= count) {
        next = count - 1;
    }
    if (scenarios) {
        scenario_index_ = next;
    }
    list_index_ = next;
    reveal_file_list();
    const bool keep_field = focus_id_ == kFieldName || focus_id_ == kFieldSeed;
    if (!keep_field) {
        const int row = list_index_ - list_scroll_;
        if (row >= 0 && row < 64) {
            focus_id_ = kListItem + row;
        }
    }
    return true;
}

void Shell::activate_file_row()
{
    if (dialog_ != Dialog::Files) {
        return;
    }
    if (list_index_ >= 0 && list_index_ < static_cast<int>(files_.size()) &&
        file_row_is_dir(files_[static_cast<std::size_t>(list_index_)])) {
        const std::string name = files_[static_cast<std::size_t>(list_index_)];
        if (name == "..") {
            files_dir_ = parent_dir(files_dir_);
        } else {
            files_dir_ = path_join(files_dir_, name.substr(0, name.size() - 1));
        }
        list_index_ = 0;
        list_scroll_ = 0;
        refresh_files();
        return;
    }
    on_dialog_button(files_save_ ? kSave : kOk);
}

void Shell::activate_focus()
{
    if (focus_id_ >= kOsk && focus_id_ < kOsk + 64) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kGraphSeries && focus_id_ < kGraphSeries + 16) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kChoice && focus_id_ < kChoice + 50) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kSlider && focus_id_ < kSlider + 8) {
        return;
    }
    if (dialog_ != Dialog::None && is_dialog_button(focus_id_)) {
        on_dialog_button(focus_id_);
        return;
    }
    if (dialog_ == Dialog::Scenario) {
        on_dialog_button(kPlay);
        return;
    }
    if (dialog_ == Dialog::Messages) {
        jump_notice(list_index_);
        return;
    }
    if (dialog_ == Dialog::Files) {
        if (files_save_ && focus_id_ == kFieldName) {
            on_dialog_button(kSave);
            return;
        }
        activate_file_row();
        return;
    }
    if (dialog_ == Dialog::NewCity) {
        on_dialog_button(new_page_ < 2 ? kNext : kGenerate);
        return;
    }
    if (dialog_ == Dialog::Rename) {
        on_dialog_button(kRename);
    }
}

bool Shell::adjust_focused_slider(int direction)
{
    for (const SliderGeom &slider : sliders_) {
        if (slider.id != focus_id_) {
            continue;
        }
        int step = slider.id == kSlider + 1 ? direction : direction * 5;
        int current = tax_;
        if (dialog_ == Dialog::Settings) {
            if (slider.id == kSlider + 1) {
                current = volume_;
                step = direction * 5;
            } else if (slider.id == kSlider + 2) {
                if (scale_percent_ <= 0) {
                    return false;
                }
                current = scale_percent_;
                step = direction * 25;
            }
        } else if (slider.id == kSlider + 2) {
            current = road_;
        } else if (slider.id == kSlider + 3) {
            current = police_;
        } else if (slider.id == kSlider + 4) {
            current = fire_;
        }
        const int value = std::max(slider.min, std::min(slider.max, current + step));
        if (dialog_ == Dialog::Settings) {
            if (slider.id == kSlider + 1) {
                volume_ = value;
                sound_.set_volume(static_cast<float>(volume_) / 100.0f);
            } else if (slider.id == kSlider + 2) {
                scale_percent_ = value;
            }
        } else if (slider.id == kSlider + 1) {
            tax_ = value;
            session_.set_tax(tax_);
        } else if (slider.id == kSlider + 2) {
            road_ = value;
            session_.set_road_funding(road_);
        } else if (slider.id == kSlider + 3) {
            police_ = value;
            session_.set_police_funding(police_);
        } else if (slider.id == kSlider + 4) {
            fire_ = value;
            session_.set_fire_funding(fire_);
        }
        return true;
    }
    return false;
}

void Shell::arm_smoke_pad()
{
    if (!attach_test_pad()) {
        return;
    }
    const int before = tool_index_;
    if (virtual_joy_ != nullptr) {
        SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    }
    SDL_UpdateJoysticks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handle_event(event);
    }
    if (virtual_joy_ != nullptr) {
        SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
        SDL_UpdateJoysticks();
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }
    }
    if (tool_index_ == before) {
        SDL_Event pushed{};
        pushed.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        pushed.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        pushed.gbutton.down = true;
        handle_event(pushed);
    }
    if (tool_index_ == before) {
        ran_ok_ = false;
        smoke_error_ = "gamepad shoulder did not cycle tools";
    }
    const int touch_before = tool_index_;
    SDL_Event finger{};
    finger.type = SDL_EVENT_FINGER_DOWN;
    finger.tfinger.fingerID = 1;
    finger.tfinger.x = 0.5f;
    finger.tfinger.y = 0.55f;
    handle_event(finger);
    finger.type = SDL_EVENT_FINGER_UP;
    handle_event(finger);
    (void)touch_before;
    if (!touch_used_) {
        ran_ok_ = false;
        smoke_error_ = "touch path did not run";
    }
}

void SDLCALL Shell::file_callback(void *userdata, const char *const *filelist, int)
{
    if (userdata == nullptr) {
        return;
    }
    static_cast<Shell *>(userdata)->on_file_result(filelist);
}

const char *Shell::glyph(SDL_GamepadButton button) const
{
    SDL_GamepadButtonLabel label = SDL_GAMEPAD_BUTTON_LABEL_A;
    if (pad_ != nullptr) {
        label = SDL_GetGamepadButtonLabel(pad_, button);
    } else if (button == SDL_GAMEPAD_BUTTON_EAST) {
        label = SDL_GAMEPAD_BUTTON_LABEL_B;
    } else if (button == SDL_GAMEPAD_BUTTON_WEST) {
        label = SDL_GAMEPAD_BUTTON_LABEL_X;
    } else if (button == SDL_GAMEPAD_BUTTON_NORTH) {
        label = SDL_GAMEPAD_BUTTON_LABEL_Y;
    }
    switch (label) {
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
        return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
        return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
        return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        return "Triangle";
    default:
        return "A";
    }
}

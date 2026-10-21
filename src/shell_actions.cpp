// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "city_seed.hpp"
#include "file_list.hpp"
#include "list_scroll.hpp"
#include "osk_layout.hpp"
#include "platform.hpp"
#include "save_path.hpp"
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

void Shell::open_dialog(Dialog dialog)
{
    menu_.close();
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
    } else if (dialog == Dialog::Rename) {
        field_a_ = session_.city_name();
        focus_id_ = kFieldName;
    } else if (dialog == Dialog::Files && files_save_) {
        field_a_ = proposed_city_filename(session_.city_name(), session_.save_path());
        focus_id_ = kFieldName;
    } else if (dialog == Dialog::Unsaved) {
        focus_id_ = kSave;
    } else if (dialog == Dialog::Disaster || dialog == Dialog::RecoveryDelete || dialog == Dialog::Overwrite) {
        focus_id_ = kCancel;
    } else if (dialog == Dialog::Recovery) {
        focus_id_ = kRecover;
    } else if (dialog == Dialog::Outcome) {
        focus_id_ = kKeep;
    } else if (dialog == Dialog::Budget) {
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
        SDL_SetWindowTitle(window_, "Could not load city");
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

void Shell::select_tool(int index)
{
    if (index < 0) {
        index = kToolCount - 1;
    }
    if (index >= kToolCount) {
        index = 0;
    }
    tool_index_ = index;
    palette_.set_selected(index);
    map_.set_tool(tool_by_index(index)->engine_id);
}

void Shell::cycle_tool(int delta)
{
    select_tool(tool_index_ + delta);
}

void Shell::clamp_scroll()
{
    const double max_x = std::max(0, map_.pixel_width() - view_w_);
    const double max_y = std::max(0, map_.pixel_height() - view_h_);
    scroll_x_ = std::max(0.0, std::min(max_x, scroll_x_));
    scroll_y_ = std::max(0.0, std::min(max_y, scroll_y_));
}

void Shell::center_on_fraction(double fx, double fy)
{
    const double mw = std::max(1, map_.pixel_width());
    const double mh = std::max(1, map_.pixel_height());
    scroll_x_ = fx * mw - view_w_ / 2.0;
    scroll_y_ = fy * mh - view_h_ / 2.0;
    clamp_scroll();
}

void Shell::zoom_by(int delta)
{
    const double mw = std::max(1, map_.pixel_width());
    const double mh = std::max(1, map_.pixel_height());
    const double cx = (scroll_x_ + view_w_ * 0.5) / mw;
    const double cy = (scroll_y_ + view_h_ * 0.5) / mh;
    const int before = map_.tile_size();
    map_.set_tile_size(before + delta);
    if (map_.tile_size() == before) {
        return;
    }
    center_on_fraction(cx, cy);
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
    std::ofstream out(dir + "/recent.txt", std::ios::trunc);
    for (const std::string &item : recent_) {
        out << item << '\n';
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
            return;
        }
    }
    if (!session_.write_recovery(own_recovery_)) {
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
    ++city_generation_;
    speed_ = session_.speed();
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
    files_.push_back("..");
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
        break;
    case Action::AutoBulldoze:
        session_.set_auto_bulldoze(!session_.auto_bulldoze());
        break;
    case Action::DisastersEnabled:
        session_.set_disasters(!session_.disasters());
        break;
    case Action::AutoGoto:
        session_.set_auto_goto(!session_.auto_goto());
        break;
    case Action::Mute:
        session_.set_sound_enabled(!session_.sound_enabled());
        sound_.set_muted(!session_.sound_enabled());
        break;
    case Action::SetPause:
        paused_from_ = speed_ > 0 ? speed_ : paused_from_;
        set_speed(0);
        break;
    case Action::Slow:
        set_speed(1);
        break;
    case Action::Medium:
        set_speed(2);
        break;
    case Action::Fast:
        set_speed(3);
        break;
    case Action::TogglePause:
        toggle_pause();
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
        overlay_.set_layer(CitySession::MapLayer::Power);
        open_dialog(Dialog::Overlay);
        break;
    case Action::Water:
        overlay_.set_layer(CitySession::MapLayer::Water);
        open_dialog(Dialog::Overlay);
        break;
    case Action::Pollution:
        overlay_.set_layer(CitySession::MapLayer::Pollution);
        open_dialog(Dialog::Overlay);
        break;
    case Action::Crime:
        overlay_.set_layer(CitySession::MapLayer::Crime);
        open_dialog(Dialog::Overlay);
        break;
    case Action::LandValue:
        overlay_.set_layer(CitySession::MapLayer::LandValue);
        open_dialog(Dialog::Overlay);
        break;
    case Action::Traffic:
        overlay_.set_layer(CitySession::MapLayer::Traffic);
        open_dialog(Dialog::Overlay);
        break;
    case Action::About:
        open_dialog(Dialog::About);
        break;
    case Action::RecentIndex:
    case Action::None:
        break;
    }
}

void Shell::on_dialog_button(int id)
{
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
        close_dialog();
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
            close_dialog();
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
            close_dialog();
            const int serial = session_.message_serial();
            ++city_generation_;
            session_.new_city(spec);
            if (session_.message_serial() == serial) {
                session_.set_speed(speed_);
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
                    speed_ = session_.speed();
                    center_on_fraction(0.5, 0.5);
                    sync_title();
                }
            }
            return;
        }
        close_dialog();
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
            if (!session_.needs_save_prompt() || true) {
                if (session_.needs_save_prompt()) {
                    queued_path_.clear();
                }
            }
            if (!session_.load_recovery(entry.path, entry.origin)) {
                info_title_ = "Recover city";
                info_body_ = session_.last_load_error().empty() ? "The recovery file could not be read."
                                                                : session_.last_load_error();
                open_dialog(Dialog::Info);
                return;
            }
            own_recovery_ = entry.path;
            ++city_generation_;
            autosave_fingerprint_ = session_.change_fingerprint();
            set_speed(0);
            center_on_fraction(0.5, 0.5);
            sync_title();
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
        close_dialog();
        return;
    }
    if (dialog_ == Dialog::Overwrite) {
        if (id == kOk || id == kSave) {
            const std::string path = queued_path_;
            close_dialog();
            finish_save(path);
            return;
        }
        begin_files(true);
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
    close_dialog();
}

void Shell::on_hotspot(int id, double x, double y)
{
    if (id == kOverlayMap && dialog_ == Dialog::Overlay) {
        double fx = 0;
        double fy = 0;
        if (overlay_.click(x - overlay_x_, y - overlay_y_, static_cast<int>(overlay_w_), static_cast<int>(overlay_h_), fx,
                           fy)) {
            center_on_fraction(fx, fy);
        }
        return;
    }
    if (id == kWelcomeId) {
        welcome_ = false;
        const std::string dir = city_config_dir();
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::ofstream out(dir + "/welcome-dismissed", std::ios::trunc);
            out << "dismissed\n";
        }
        return;
    }
    if (id >= kMenuTitle && id < kMenuTitle + 5 && dialog_ == Dialog::None) {
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
            list_index_ = list_scroll_ + row;
            if (list_index_ >= static_cast<int>(files_.size())) {
                list_index_ = static_cast<int>(files_.size()) - 1;
            }
            if (list_index_ < 0) {
                list_index_ = 0;
            }
            reveal_file_list();
        } else {
            list_index_ = row;
            if (dialog_ == Dialog::Scenario) {
                scenario_index_ = list_index_;
            }
        }
        focus_id_ = id;
        return;
    }
    if (id == kListScroll && dialog_ == Dialog::Files) {
        list_dragging_ = true;
        const int count = static_cast<int>(files_.size());
        list_index_ = list_index_from_track(y, list_track_y_, list_track_h_, count);
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
                *active_field() = "auto";
            }
        } else if (key.kind == OskKind::Shift) {
            osk_shift_ = !osk_shift_;
        } else if (key.text != nullptr) {
            const char *insert = (osk_shift_ && key.text_shift != nullptr) ? key.text_shift : key.text;
            type_text(insert);
        }
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
            if (id == kSlider + 1) {
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

void Shell::type_text(const std::string &text)
{
    std::string *field = active_field();
    if (field == nullptr) {
        return;
    }
    if (field->size() + text.size() > 64) {
        return;
    }
    *field += text;
}

void Shell::backspace_field()
{
    std::string *field = active_field();
    if (field == nullptr || field->empty()) {
        return;
    }
    field->pop_back();
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
    // The welcome bar, menu, and map stay on screen under a modal. They must
    // not join the D-pad cycle.
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
        } else {
            list_index_ = focus_id_ - kListItem;
            if (dialog_ == Dialog::Scenario) {
                scenario_index_ = list_index_;
            }
        }
    }
}

void Shell::reveal_file_list()
{
    const int count = static_cast<int>(files_.size());
    list_reveal(list_scroll_, list_index_, std::max(1, files_visible_), count);
}

bool Shell::nudge_file_list(int delta, bool page, bool force)
{
    if (dialog_ != Dialog::Files || files_.empty() || delta == 0) {
        return false;
    }
    const bool on_list = focus_id_ >= kListItem && focus_id_ < kListItem + 64;
    if (!on_list && !force && !page) {
        return false;
    }
    const int count = static_cast<int>(files_.size());
    const int step = page ? std::max(1, files_visible_ - 1) : std::max(1, std::abs(delta));
    const int sign = delta < 0 ? -1 : 1;
    int next = list_index_ + sign * step;
    if (!page && !force && on_list && (next < 0 || next >= count)) {
        return false;
    }
    if (next < 0) {
        next = 0;
    }
    if (next >= count) {
        next = count - 1;
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

void Shell::activate_focus()
{
    if (focus_id_ >= kOsk) {
        on_hotspot(focus_id_, 0, 0);
        return;
    }
    if (focus_id_ >= kChoice) {
        on_hotspot(focus_id_, 0, 0);
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
    if (dialog_ == Dialog::Files) {
        on_dialog_button(files_save_ ? kSave : kOk);
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
        const int step = slider.id == kSlider + 1 ? direction : direction * 5;
        int current = tax_;
        if (slider.id == kSlider + 2) {
            current = road_;
        } else if (slider.id == kSlider + 3) {
            current = police_;
        } else if (slider.id == kSlider + 4) {
            current = fire_;
        }
        const int value = std::max(slider.min, std::min(slider.max, current + step));
        if (slider.id == kSlider + 1) {
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

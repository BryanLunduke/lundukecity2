// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "assets.hpp"
#include "city_seed.hpp"
#include "dialog_copy.hpp"
#include "notice_bar.hpp"
#include "platform.hpp"
#include "save_path.hpp"
#include "sim_pace.hpp"
#include "special_buildings.hpp"
#include "sprite_art.hpp"
#include "text_paint.hpp"
#include "tile_atlas.hpp"
#include "tool_keys.hpp"
#include "tools.hpp"
#include "version.hpp"
#include "zoom_keys.hpp"

#include <SDL3/SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
volatile std::sig_atomic_t g_terminate = 0;

void on_terminate(int)
{
    g_terminate = 1;
}
#endif

AccelKey accel_from_key(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z') {
        switch (key) {
        case 'n':
            return AccelKey::N;
        case 'o':
            return AccelKey::O;
        case 's':
            return AccelKey::S;
        case 'q':
            return AccelKey::Q;
        case 'p':
            return AccelKey::P;
        default:
            return AccelKey::Letter;
        }
    }
    switch (key) {
    case SDLK_1:
        return AccelKey::D1;
    case SDLK_2:
        return AccelKey::D2;
    case SDLK_3:
        return AccelKey::D3;
    case SDLK_EQUALS:
        return AccelKey::Equal;
    case SDLK_PLUS:
        return AccelKey::Plus;
    case SDLK_MINUS:
        return AccelKey::Minus;
    case SDLK_KP_PLUS:
        return AccelKey::KpAdd;
    case SDLK_KP_MINUS:
        return AccelKey::KpSub;
    case SDLK_F10:
        return AccelKey::F10;
    case SDLK_UP:
        return AccelKey::Up;
    case SDLK_DOWN:
        return AccelKey::Down;
    case SDLK_LEFT:
        return AccelKey::Left;
    case SDLK_RIGHT:
        return AccelKey::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return AccelKey::Return;
    case SDLK_ESCAPE:
        return AccelKey::Escape;
    case SDLK_SPACE:
        return AccelKey::Space;
    default:
        return AccelKey::None;
    }
}

int mods_from_sdl(SDL_Keymod mod)
{
    int mods = 0;
    if ((mod & SDL_KMOD_CTRL) != 0) {
        mods |= static_cast<int>(AccelMod::Ctrl);
    }
    if ((mod & SDL_KMOD_SHIFT) != 0) {
        mods |= static_cast<int>(AccelMod::Shift);
    }
    if ((mod & SDL_KMOD_ALT) != 0) {
        mods |= static_cast<int>(AccelMod::Alt);
    }
    return mods;
}

char letter_from_key(SDL_Keycode key)
{
    if (key >= 'a' && key <= 'z') {
        return static_cast<char>(key);
    }
    if (key >= 'A' && key <= 'Z') {
        return static_cast<char>(key - 'A' + 'a');
    }
    return 0;
}

unsigned gdk_mods(SDL_Keymod mod)
{
    unsigned out = 0;
    if ((mod & SDL_KMOD_CTRL) != 0) {
        out |= 1u << 2;
    }
    if ((mod & SDL_KMOD_ALT) != 0) {
        out |= 1u << 3;
    }
    return out;
}

unsigned gdk_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_EQUALS:
        return 0x03d;
    case SDLK_PLUS:
        return 0x02b;
    case SDLK_MINUS:
        return 0x02d;
    case SDLK_KP_PLUS:
        return 0xffab;
    case SDLK_KP_MINUS:
        return 0xffad;
    default:
        return 0;
    }
}

} // namespace

void Shell::on_file_result(const char *const *filelist)
{
    std::lock_guard<std::mutex> lock(file_mu_);
    file_ready_ = true;
    if (filelist == nullptr) {
        file_state_ = 3;
        file_path_.clear();
        return;
    }
    if (filelist[0] == nullptr) {
        file_state_ = 2;
        file_path_.clear();
        return;
    }
    file_state_ = 1;
    file_path_ = filelist[0];
}

int run_shell(int argc, char **argv)
{
    Shell shell;
    return shell.run(argc, argv);
}

int Shell::run(int argc, char **argv)
{
    if (const char *smoke = std::getenv("LUNDUKE_CITY2_SMOKE")) {
        smoke_ = smoke[0] != '\0' && smoke[0] != '0';
    }
    if (const char *ui = std::getenv("LUNDUKE_CITY2_UI_TEST")) {
        ui_test_ = ui[0] != '\0' && ui[0] != '0';
    }
#ifdef CITY_REVIEW_CHECKS
    if (const char *review = std::getenv("LUNDUKE_CITY2_REVIEW")) {
        review_ = review[0] != '\0' && review[0] != '0';
    }
    // The self-test locks a 1280x800 window. A Deck flag or a size override
    // inherited from the environment must not change that.
    if (review_) {
#ifdef _WIN32
        _putenv_s("SteamDeck", "");
        _putenv_s("LUNDUKE_CITY2_SIZE", "");
#else
        unsetenv("SteamDeck");
        unsetenv("LUNDUKE_CITY2_SIZE");
#endif
    }
#endif
    if (const char *demo = std::getenv("LUNDUKE_CITY2_DEMO")) {
        demo_ = demo;
    }
    if (const char *shot = std::getenv("LUNDUKE_CITY2_SHOT")) {
        shot_path_ = shot;
    }
    if (const char *save = std::getenv("LUNDUKE_CITY2_SAVE")) {
        save_path_env_ = save;
    }
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && argv[i][0] != '\0' && argv[i][0] != '-') {
            pending_opens_.emplace_back(argv[i]);
        }
    }
    if (!init_video()) {
        return 1;
    }
    // Pause before the first city exists, matching the gtkmm shell. The
    // generated city starts in January 1900. Starting the clock afterwards
    // is a preference, not an edit, same as New City.
    session_.set_speed(0);
    speed_ = 0;
    session_.new_city("New City");
    session_.set_speed(0);
    apply_loaded_settings();
    bind_session();
    if (!tile_atlas().loaded() && dialog_ == Dialog::None) {
        info_title_ = "Missing game data";
        info_body_ = tile_atlas().load_error().empty() ? asset_missing_text(asset_candidates()) : tile_atlas().load_error();
        open_dialog(Dialog::Info);
    }
    load_recent();
    started_ = std::chrono::steady_clock::now();
    next_autosave_ = started_ + std::chrono::seconds(60);
    layout();
    center_on_fraction(0.5, 0.5);
    // Command-line opens happen before the first tick, so a brand-new city
    // has not yet become unsaved work and the file replaces it directly.
    const bool launched_with_file = !pending_opens_.empty();
    while (dialog_ == Dialog::None && !pending_opens_.empty()) {
        queued_path_ = pending_opens_.front();
        pending_opens_.erase(pending_opens_.begin());
        request_with_save(AfterSave::OpenPath);
    }
    if (smoke_ && !ui_test_ && !launched_with_file && session_.city_name() == "New City") {
        session_.load_scenario(CitySession::scenario_def(0).id);
        speed_ = session_.speed();
        center_on_fraction(0.5, 0.5);
    }
    if (ui_test_ || review_) {
        recovery_checked_ = true;
        speed_ = 0;
        session_.set_speed(0);
    }
    // The launch screen is for a player who has not already named a city
    // on the command line, and not for the scripted demo / review runs.
    const bool scripted = smoke_ || ui_test_ || review_ || !demo_.empty();
    if (!scripted && !launched_with_file) {
        welcome_gate_ = true;
        if (dialog_ == Dialog::None) {
            open_dialog(Dialog::Welcome);
        }
    } else if (launched_with_file && dialog_ == Dialog::LoadError) {
        welcome_gate_ = true;
    }
    if (!demo_.empty() && demo_ != "budget" && demo_ != "menu" && demo_ != "welcome" && demo_ != "scenario" &&
        demo_ != "quit" && demo_ != "special" && demo_ != "gifts" && demo_ != "giftquery" && demo_ != "giftbudget" &&
        demo_ != "crime" && demo_ != "zones" && demo_ != "police" && demo_ != "graphs" && demo_ != "terrain" &&
        demo_ != "controls" && demo_ != "settings" && demo_ != "won" && demo_ != "lost" && demo_ != "fire") {
        int ox = 0;
        int oy = 0;
        if (session_.stamp_neighborhood(ox, oy)) {
            center_on_fraction((ox + 8.0) / CitySession::kWorldW, (oy + 8.0) / CitySession::kWorldH);
        }
    }
#ifndef _WIN32
    std::signal(SIGTERM, on_terminate);
    std::signal(SIGHUP, on_terminate);
    // A terminal Ctrl+C should autosave, the same as SIGTERM.
    std::signal(SIGINT, on_terminate);
#endif
    loop();
    shutdown();
    if ((smoke_ || ui_test_ || review_) && !ran_ok_) {
        std::cerr << smoke_error_ << "\n";
        return 2;
    }
    return 0;
}

int Shell::dp(int px) const
{
    return ui_scale::scaled_px(px, ui_scale_);
}

const char *Shell::ui_font() const
{
    return font_ui_.c_str();
}

const char *Shell::body_font() const
{
    return font_body_.c_str();
}

const char *Shell::title_font() const
{
    return font_title_.c_str();
}

const char *Shell::menu_font() const
{
    return font_menu_.c_str();
}

const char *Shell::bold_font() const
{
    return font_bold_.c_str();
}

void Shell::refresh_ui_scale()
{
    float display = 1.0f;
    if (window_ != nullptr) {
        const float reported = SDL_GetWindowDisplayScale(window_);
        if (reported > 0.0f) {
            display = reported;
        }
    }
    const float user = scale_env_ > 0.05f ? scale_env_ : user_scale_;
    ui_scale_ = ui_scale::resolve_ui_scale(display, user);
    font_ui_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
    font_body_ = ui_scale::scaled_font("Sans", 14, ui_scale_);
    font_title_ = ui_scale::scaled_font("Sans Bold", 18, ui_scale_);
    font_menu_ = ui_scale::scaled_font("Sans", 13, ui_scale_);
    font_bold_ = ui_scale::scaled_font("Sans Bold", 13, ui_scale_);
}

bool Shell::allow_saved_settings() const
{
    return !headless_ && !size_override_ && !smoke_ && !ui_test_ && !review_ && demo_.empty() && shot_path_.empty();
}

void Shell::load_window_settings()
{
    if (smoke_ || ui_test_ || review_ || !demo_.empty() || !shot_path_.empty() || headless_) {
        return;
    }
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::ifstream in(dir + "/settings.txt");
    if (!in) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const ui_scale::StoredSettings stored = ui_scale::settings_parse(buffer.str());
    loaded_ = stored;
    user_scale_ = stored.user_scale;
    // Remember the desktop rectangle even on Deck. The window itself stays
    // fullscreen; the rectangle is written back when the session ends.
    if (stored.has_geometry && !size_override_) {
        saved_x_ = stored.x;
        saved_y_ = stored.y;
        saved_w_ = stored.w;
        saved_h_ = stored.h;
        if (!deck_) {
            want_maximized_ = stored.maximized;
        }
        has_saved_window_ = true;
    }
}

void Shell::save_window_settings()
{
    if (!allow_saved_settings() || window_ == nullptr) {
        return;
    }
    note_window_geometry();
    ui_scale::StoredSettings stored;
    stored.user_scale = user_scale_;
    stored.speed = std::max(0, std::min(3, pref_speed_ >= 0 ? pref_speed_ : speed_));
    stored.sound = session_.sound_enabled() ? 1 : 0;
    stored.volume = std::max(0, std::min(100, volume_));
    stored.auto_budget = session_.auto_budget() ? 1 : 0;
    stored.auto_bulldoze = session_.auto_bulldoze() ? 1 : 0;
    stored.disasters = pref_disasters_ >= 0 ? (pref_disasters_ != 0 ? 1 : 0) : (session_.disasters() ? 1 : 0);
    stored.auto_goto = session_.auto_goto() ? 1 : 0;
    stored.pause_unfocused = pause_unfocused_ ? 1 : 0;
    stored.wheel_zoom = wheel_zooms_ ? 1 : 0;
    {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
        const bool maximized = (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) != 0;
        ui_scale::keep_desktop_geometry(stored, loaded_, deck_, normal_valid_, normal_x_, normal_y_, normal_w_, normal_h_,
                                        deck_ ? loaded_.maximized : maximized);
    }
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir + "/settings.txt", std::ios::trunc);
    if (!out) {
        return;
    }
    out << ui_scale::settings_text(stored);
}

void Shell::apply_loaded_settings()
{
    const ui_scale::StoredSettings &stored = loaded_;
    if (stored.speed >= 0) {
        speed_ = std::max(0, std::min(3, stored.speed));
        session_.set_speed(speed_);
    }
    if (stored.sound >= 0) {
        session_.set_sound_enabled(stored.sound != 0);
        sound_.set_muted(!session_.sound_enabled());
    }
    if (stored.volume >= 0) {
        volume_ = std::max(0, std::min(100, stored.volume));
        sound_.set_volume(static_cast<float>(volume_) / 100.0f);
    }
    if (stored.auto_budget >= 0) {
        session_.set_auto_budget(stored.auto_budget != 0);
    }
    if (stored.auto_bulldoze >= 0) {
        session_.set_auto_bulldoze(stored.auto_bulldoze != 0);
    }
    if (stored.disasters >= 0) {
        session_.set_disasters(stored.disasters != 0);
    }
    if (stored.auto_goto >= 0) {
        session_.set_auto_goto(stored.auto_goto != 0);
    }
    if (stored.pause_unfocused >= 0) {
        pause_unfocused_ = stored.pause_unfocused != 0;
    }
    if (stored.wheel_zoom >= 0) {
        wheel_zooms_ = stored.wheel_zoom != 0;
    }
    // These are the global preferences. A city file must not replace them.
    pref_speed_ = speed_;
    pref_disasters_ = session_.disasters() ? 1 : 0;
    session_.accept_preferences();
}

void Shell::restore_play_preferences()
{
    if (pref_speed_ >= 0) {
        session_.set_speed(pref_speed_);
        speed_ = session_.speed();
    }
    if (pref_disasters_ >= 0) {
        session_.set_disasters(pref_disasters_ != 0);
    }
    session_.accept_preferences();
}

void Shell::note_keyboard_input()
{
    gamepad_nav_ = false;
    touch_used_ = false;
}

void Shell::note_background_write_failed(const char *text)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    // Stay up until the player clicks the message bar. A failure after that
    // click is shown again; the same text does not restart the notice.
    if (notice_sticky_ && shown_message_ == text) {
        return;
    }
    notice_sticky_ = true;
    autosave_warned_ = true;
    shown_message_ = text;
    shown_message_serial_ = session_.message_serial();
    hint_until_ = std::chrono::steady_clock::now() + std::chrono::hours(24 * 365);
}

void Shell::dismiss_background_notice()
{
    if (!notice_sticky_) {
        return;
    }
    notice_sticky_ = false;
    autosave_warned_ = false;
    shown_message_.clear();
    hint_until_ = std::chrono::steady_clock::now();
}

void Shell::note_window_geometry()
{
    if (window_ == nullptr) {
        return;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED)) != 0) {
        return;
    }
    int ww = 0;
    int hh = 0;
    int x = 0;
    int y = 0;
    SDL_GetWindowSize(window_, &ww, &hh);
    SDL_GetWindowPosition(window_, &x, &y);
    if (ww > 0 && hh > 0) {
        normal_w_ = ww;
        normal_h_ = hh;
        normal_x_ = x;
        normal_y_ = y;
        normal_valid_ = true;
    }
}

bool Shell::init_video()
{
#ifdef _WIN32
    SetCurrentProcessExplicitAppUserModelID(L"Lunduke.LundukeCity2");
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    // Audio is optional. SDL_Init fails the whole call when any requested
    // subsystem fails, and a machine with no playback device (the argv_open
    // test, and a sniper session without a dummy driver) must still open.
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_ClearError();
    }
    deck_ = steam_deck_environment();
    if (const char *hint = SDL_GetHint("SteamDeck")) {
        if (hint[0] == '1') {
            deck_ = true;
        }
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    headless_ = driver != nullptr && (std::strcmp(driver, "dummy") == 0 || std::strcmp(driver, "offscreen") == 0);
    if (const char *scale = std::getenv("LUNDUKE_CITY2_SCALE")) {
        char *end = nullptr;
        const float parsed = std::strtof(scale, &end);
        if (end != scale && parsed > 0.0f) {
            scale_env_ = parsed;
        }
    }
    int size_w = 0;
    int size_h = 0;
    if (const char *size = std::getenv("LUNDUKE_CITY2_SIZE")) {
        int parsed_w = 0;
        int parsed_h = 0;
        char sep = 0;
        if (std::sscanf(size, "%d%c%d", &parsed_w, &sep, &parsed_h) == 3 && (sep == 'x' || sep == 'X') && parsed_w >= 320 &&
            parsed_h >= 240) {
            size_w = parsed_w;
            size_h = parsed_h;
            size_override_ = true;
        }
    }
    load_window_settings();
    int req_w = 1280;
    int req_h = 800;
    int req_x = 0;
    int req_y = 0;
    bool place = false;
    SDL_Rect usable{};
    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const bool have_bounds = display != 0 && SDL_GetDisplayUsableBounds(display, &usable);
    if (size_override_) {
        req_w = size_w;
        req_h = size_h;
    } else if (headless_) {
        req_w = 1280;
        req_h = 800;
    } else if (have_bounds && has_saved_window_ && !deck_) {
        const ui_scale::Placement saved =
            ui_scale::clamp_saved_window(saved_x_, saved_y_, saved_w_, saved_h_, usable.x, usable.y, usable.w, usable.h);
        req_w = saved.w;
        req_h = saved.h;
        req_x = saved.x;
        req_y = saved.y;
        place = true;
    } else if (have_bounds && !deck_) {
        const ui_scale::Placement fitted = ui_scale::fit_default_window(1280, 800, usable.x, usable.y, usable.w, usable.h);
        req_w = fitted.w;
        req_h = fitted.h;
        req_x = fitted.x;
        req_y = fitted.y;
        place = true;
    } else if (have_bounds && deck_) {
        req_w = std::max(640, usable.w);
        req_h = std::max(480, usable.h);
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (!headless_) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    window_ = SDL_CreateWindow(kProductName, req_w, req_h, flags);
    if (window_ == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }
    int min_w = 960;
    int min_h = 600;
    ui_scale::window_minimum(have_bounds ? usable.w : 0, have_bounds ? usable.h : 0, min_w, min_h);
    SDL_SetWindowMinimumSize(window_, min_w, min_h);
    if (place) {
        SDL_SetWindowPosition(window_, req_x, req_y);
    }
    normal_x_ = req_x;
    normal_y_ = req_y;
    normal_w_ = req_w;
    normal_h_ = req_h;
    normal_valid_ = true;
    // A requested size (the screenshot and test harness) stays that size.
    // A Deck session otherwise fills the display.
    if (deck_ && !size_override_) {
        SDL_SetWindowFullscreen(window_, true);
    } else if (want_maximized_) {
        SDL_MaximizeWindow(window_);
    }
    refresh_ui_scale();
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    sync_title();
    return true;
}

void Shell::demo_place_gifts(bool query_one)
{
    session_.set_disasters(false);
    session_.set_auto_bulldoze(true);
    set_speed(0);
    const int want = query_one ? 1 : special_building_count();
    int placed_x[8] = {};
    int placed_y[8] = {};
    int placed = 0;
    // Prefer one horizontal row so a single frame shows every building.
    // Centers five tiles apart keep the 3x3 footprints from touching.
    const int gap = 5;
    const int bulldozer = tool_by_index(0)->engine_id;
    for (int y = 3; y < CitySession::kWorldH - 3 && placed < want; ++y) {
        const int x_limit = CitySession::kWorldW - 3 - gap * (want - 1);
        for (int x = 3; x < x_limit && placed < want; ++x) {
            placed = 0;
            for (int i = 0; i < want; ++i) {
                if (!session_.place_special(i, x + i * gap, y)) {
                    break;
                }
                placed_x[i] = x + i * gap;
                placed_y[i] = y;
                placed = i + 1;
            }
            if (placed == want) {
                break;
            }
            for (int i = 0; i < placed; ++i) {
                session_.use_tool(bulldozer, placed_x[i], placed_y[i]);
            }
            placed = 0;
        }
    }
    if (placed < want) {
        placed = 0;
        for (int y = 2; y < CitySession::kWorldH - 2 && placed < want; ++y) {
            for (int x = 2; x < CitySession::kWorldW - 2 && placed < want; ++x) {
                bool too_close = false;
                for (int i = 0; i < placed; ++i) {
                    int dx = x - placed_x[i];
                    int dy = y - placed_y[i];
                    if (dx < 0) {
                        dx = -dx;
                    }
                    if (dy < 0) {
                        dy = -dy;
                    }
                    if ((dx > dy ? dx : dy) < 4) {
                        too_close = true;
                    }
                }
                if (too_close || !session_.place_special(placed, x, y)) {
                    continue;
                }
                placed_x[placed] = x;
                placed_y[placed] = y;
                ++placed;
            }
        }
    }
    if (placed < 1) {
        return;
    }
    map_.set_tile_size(48);
    const int mid = placed / 2;
    center_on_fraction((placed_x[mid] + 0.5) / CitySession::kWorldW, (placed_y[mid] + 0.5) / CitySession::kWorldH);
    if (query_one) {
        pointer_x_ = view_x_ + view_w_ / 2.0;
        pointer_y_ = view_y_ + view_h_ / 2.0;
        session_.use_tool(tool_by_index(kToolCount - 1)->engine_id, placed_x[0], placed_y[0]);
    }
}

void Shell::bind_session()
{
    map_.set_session(&session_);
    minimap_.set_session(&session_);
    demand_.set_session(&session_);
    overlay_.set_session(&session_);
    select_tool(kDefaultToolIndex);
    map_.on_tool_down = [this](int x, int y) {
        goal_dismissed_ = true;
        const ToolDef *tool = tool_by_index(tool_index_);
        if (tool->engine_id == kSpecialEngineId) {
            if (special_open_ || special_choice_ < 0) {
                if (special_choice_ < 0) {
                    special_open_ = true;
                    special_cursor_ = 0;
                } else {
                    special_open_ = false;
                }
                return;
            }
            session_.place_special(special_choice_, x, y);
            return;
        }
        session_.use_tool(tool->engine_id, x, y);
    };
    map_.on_tool_drag = [this](int x0, int y0, int x1, int y1) {
        const ToolDef *tool = tool_by_index(tool_index_);
        if (tool->engine_id == kSpecialEngineId) {
            return;
        }
        session_.drag_tool(tool->engine_id, x0, y0, x1, y1);
    };
    palette_.on_selected = [this](int index) { select_tool(index); };
    minimap_.set_viewport_provider([this](double &fx, double &fy, double &fw, double &fh) {
        map_viewport_fraction(scroll_x_, scroll_y_, map_.pixel_width(), map_.pixel_height(), view_w_, view_h_, fx, fy, fw,
                              fh);
    });
    minimap_.on_jump = [this](double fx, double fy) { center_on_fraction(fx, fy); };
    session_.set_listener([this] { note_message(); });
}

void Shell::shutdown()
{
    // The mixer thread uses SDL audio. Join it before SDL_Quit.
    sound_.shutdown();
    sprite_art_shutdown();
    if (terrain_preview_ != nullptr) {
        cairo_surface_destroy(terrain_preview_);
        terrain_preview_ = nullptr;
    }
    save_window_settings();
    release_text_cache();
    release_fontconfig();
    if (frame_surface_ != nullptr) {
        cairo_surface_destroy(frame_surface_);
        frame_surface_ = nullptr;
    }
    if (pad_ != nullptr) {
        SDL_CloseGamepad(pad_);
        pad_ = nullptr;
    }
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

std::string Shell::visual_stamp() const
{
    const auto now = std::chrono::steady_clock::now();
    const bool hint_visible = !shown_message_.empty() && hint_until_ > now;
    std::ostringstream out;
    out << win_w_ << 'x' << win_h_ << ' ' << ui_scale_ << ' ' << static_cast<int>(dialog_) << ' ' << new_page_ << ' '
        << list_index_ << ' ' << list_scroll_ << ' ' << scenario_index_ << ' ' << focus_id_ << ' ' << hover_id_ << ' '
        << menu_.open_menu << ' ' << menu_.cursor << ' ' << menu_.recent_open << ' ' << menu_.recent_cursor << ' '
        << menu_.alt_shown << ' ' << scroll_x_ << ' ' << scroll_y_ << ' ' << map_.tile_size() << ' ' << tool_index_ << ' '
        << special_open_ << ' ' << special_cursor_ << ' ' << special_choice_ << ' '
        << speed_ << ' ' << volume_ << ' ' << scale_percent_ << ' ' << pause_unfocused_ << ' ' << session_.map_serial()
        << ' ' << session_.funds() << ' ' << session_.city_name() << ' '
        << session_.date_text() << ' ' << session_.scenario_years_left() << ' ' << session_.scenario_progress() << ' '
        << session_.message_serial() << ' ' << session_.message() << ' ' << welcome_gate_ << ' ' << shown_message_ << ' '
        << hint_visible << ' ' << query_pinned_ << ' ' << gamepad_nav_ << ' ' << cursor_x_ << ' ' << cursor_y_ << ' '
        << pointer_x_ << ' ' << pointer_y_ << ' ' << field_a_ << ' ' << field_b_ << ' ' << field_caret_ << ' ' << field_anchor_
        << ' ' << dialog_error_ << ' '
        << info_title_ << ' ' << info_body_ << ' ' << tax_ << ' ' << road_ << ' ' << police_ << ' ' << fire_ << ' '
        << graphs_long_ << ' ' << budget_is_tax_ << ' ' << files_dir_ << ' ' << osk_shift_ << ' ' << quake_strength_ << ' '
        << static_cast<int>(files_.size()) << ' ' << overlay_on_ << ' ' << controls_page_ << ' ' << controls_scroll_
        << ' ' << new_level_ << ' ' << new_island_ << ' ' << new_rivers_ << ' ' << new_lakes_ << ' ' << new_trees_;
    return out.str();
}

redraw::Reasons Shell::redraw_reasons() const
{
    redraw::Reasons reasons;
    reasons.forced = !presented_ || ui_test_ || smoke_ || !demo_.empty() || !shot_path_.empty();
    reasons.sim_running = speed_ > 0 && !dialog_pauses();
    reasons.quake = quake_strength_ > 0;
    reasons.navigating = pointer_down_ || pad_dragging_ || std::fabs(stick_x_) > 0.05f || std::fabs(stick_y_) > 0.05f ||
                         std::fabs(pan_x_) > 0.05f || std::fabs(pan_y_) > 0.05f;
    reasons.blink = blink_drew_;
    reasons.stamp_changed = visual_stamp() != visual_stamp_;
    reasons.exposed = window_exposed_;
    return reasons;
}

void Shell::loop()
{
    bool running = true;
    Uint64 last = SDL_GetTicks();
    int sim_acc = 0;
    while (running && !quitting_) {
#ifndef _WIN32
        if (g_terminate) {
            autosave(true);
            std::_Exit(0);
        }
#endif
        const bool spin = ui_test_ || smoke_ || review_ || !demo_.empty() || !shot_path_.empty();
        const bool animate = (speed_ > 0 && !dialog_pauses()) || quake_strength_ > 0 || pad_dragging_ || pointer_down_ ||
                             std::fabs(stick_x_) > 0.05f || std::fabs(stick_y_) > 0.05f || std::fabs(pan_x_) > 0.05f ||
                             std::fabs(pan_y_) > 0.05f;
        SDL_Event event;
        // A paused city waits for input. WaitEventTimeout sleeps the thread;
        // a NULL event would drop the input, so the event is handled here.
        if (!spin && presented_ && !animate) {
            int timeout = 500;
            const auto now_wait = std::chrono::steady_clock::now();
            if (hint_until_ > now_wait) {
                const int ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(hint_until_ - now_wait).count());
                timeout = std::max(1, std::min(timeout, ms));
            }
            if (next_autosave_ <= now_wait) {
                timeout = 1;
            } else {
                const int ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(next_autosave_ - now_wait).count());
                timeout = std::max(1, std::min(timeout, ms));
            }
            if (dialog_ == Dialog::FileWait) {
                timeout = std::min(timeout, 30);
            }
            if (SDL_WaitEventTimeout(&event, timeout)) {
                if (event.type == SDL_EVENT_QUIT) {
                    request_with_save(AfterSave::Quit);
                } else {
                    handle_event(event);
                }
            }
        }
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                request_with_save(AfterSave::Quit);
            } else {
                handle_event(event);
            }
        }
        const Uint64 now = SDL_GetTicks();
        int elapsed = static_cast<int>(now - last);
        if (elapsed < 0) {
            elapsed = 0;
        }
        if (elapsed > 2000) {
            elapsed = 2000;
        }
        last = now;
        // Simulation does not fast-forward through a long idle sleep. The
        // blink clock does, or a paused city would flash late.
        // Keep up to half a second. Fast runs several phases per slice so a
        // year does not take minutes. See sim_pace.hpp.
        sim_acc += std::min(elapsed, 500);
        blink_drew_ = map_.tick(elapsed);
        while (sim_acc >= 100) {
            sim_acc -= 100;
            const int bursts = sim_bursts_for_speed(speed_);
            for (int burst = 0; burst < bursts; ++burst) {
                tick_sim(100);
            }
        }
        const auto wall = std::chrono::steady_clock::now();
        if (wall >= next_autosave_) {
            autosave(false);
            next_autosave_ = wall + std::chrono::seconds(60);
        }
        if (!recovery_checked_ && pending_opens_.empty() && demo_.empty() && !smoke_ &&
            wall >= started_ + std::chrono::milliseconds(450) && dialog_ == Dialog::None) {
            recovery_checked_ = true;
            offer_recovery();
        }
        if (dialog_ == Dialog::None && !pending_opens_.empty()) {
            queued_path_ = pending_opens_.front();
            pending_opens_.erase(pending_opens_.begin());
            request_with_save(AfterSave::OpenPath);
        }
        poll_file_dialog();
        const bool skip_pause_note = smoke_ || ui_test_ || review_ || (!demo_.empty() && demo_ != "paused");
        if (!pause_noted_ && !skip_pause_note && dialog_ == Dialog::None && !welcome_gate_ && speed_ == 0 && frame_ > 1) {
            pause_noted_ = true;
            if (deck_ || gamepad_nav_) {
                shown_message_ = "Game paused. Press Start, then Options, to run the city.";
            } else {
                shown_message_ = "Game paused. Press Space, or Start then Options, to run the city.";
            }
            hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        }
        if (gamepad_nav_ && dialog_ == Dialog::None && !menu_.open()) {
            // The UI test holds the stick for a fixed number of frames. Give
            // each of those frames a slice of time so the drag does not
            // depend on how fast the dummy driver spins.
            const int move_ms = (ui_test_ && elapsed < 16) ? 16 : elapsed;
            const double step = 700.0 * (move_ms / 1000.0);
            cursor_x_ = static_cast<float>(std::max(0.0, std::min<double>(win_w_ - 1, cursor_x_ + stick_x_ * step)));
            cursor_y_ = static_cast<float>(std::max(0.0, std::min<double>(win_h_ - 1, cursor_y_ + stick_y_ * step)));
            scroll_x_ += pan_x_ * step;
            scroll_y_ += pan_y_ * step;
            clamp_scroll();
            clamp_gamepad_cursor(stick_x_, stick_y_);
            if (trigger_zoom_edge(trig_r_, zoom_r_latched_)) {
                zoom_by(2);
            }
            if (trigger_zoom_edge(trig_l_, zoom_l_latched_)) {
                zoom_by(-2);
            }
        }
        if (pad_dragging_ && pad_ != nullptr && dialog_ == Dialog::None && !menu_.open()) {
            int dx = 0;
            int dy = 0;
            if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
                dx -= 1;
            }
            if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                dx += 1;
            }
            if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
                dy -= 1;
            }
            if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
                dy += 1;
            }
            if (dx != 0 || dy != 0) {
                const int tick = std::max(1, (ui_test_ && elapsed < 16) ? 16 : elapsed);
                if (pad_repeat_ms_ <= 0) {
                    const float tile = static_cast<float>(std::max(map_.tile_size(), 8));
                    cursor_x_ = static_cast<float>(std::max(0.0, std::min<double>(win_w_ - 1, cursor_x_ + dx * tile)));
                    cursor_y_ = static_cast<float>(std::max(0.0, std::min<double>(win_h_ - 1, cursor_y_ + dy * tile)));
                    clamp_gamepad_cursor(static_cast<float>(dx), static_cast<float>(dy));
                    pad_repeat_ms_ = 70;
                }
                pad_repeat_ms_ -= tick;
            } else {
                pad_repeat_ms_ = 0;
            }
        }
        if (pad_dragging_) {
            pointer_x_ = cursor_x_;
            pointer_y_ = cursor_y_;
            map_.motion(map_local_x(cursor_x_), map_local_y(cursor_y_), true);
        }
        if (pointer_down_ && pointer_on_map_ && map_.dragging()) {
            double dx = 0;
            double dy = 0;
            const double local_x = pointer_x_ - view_x_;
            const double local_y = pointer_y_ - view_y_;
            if (map_.edge_scroll(local_x, local_y, view_w_, view_h_, dx, dy)) {
                scroll_x_ += dx;
                scroll_y_ += dy;
                clamp_scroll();
                map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), true);
            }
        }
        if (quake_strength_ > 0) {
            const auto now_wall = std::chrono::steady_clock::now();
            if (now_wall < quake_until_) {
                const int gone = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now_wall - quake_started_).count());
                const int amp = std::max(2, quake_strength_ / 80);
                const int step = gone / 50;
                map_.set_shake((step % 2 == 0) ? amp : -amp, ((step / 2) % 2 == 0) ? (amp / 2) : -(amp / 2));
            } else {
                quake_strength_ = 0;
                map_.set_shake(0, 0);
            }
        }
        for (const std::string &name : session_.take_sounds()) {
            if (!sound_.muted()) {
                sound_.play(name);
            }
        }
        if (session_.take_budget_request()) {
            if (dialog_pauses() && dialog_ != Dialog::Budget) {
                session_.keep_budget_request();
            } else if (dialog_ == Dialog::None || dialog_ == Dialog::Budget) {
                budget_is_tax_ = true;
                const auto book = session_.budget();
                tax_ = book.tax_percent;
                road_ = book.road_percent;
                police_ = book.police_percent;
                fire_ = book.fire_percent;
                open_tax_ = tax_;
                open_road_ = road_;
                open_police_ = police_;
                open_fire_ = fire_;
                open_dialog(Dialog::Budget);
            }
        }
        if (const int outcome = session_.take_scenario_outcome()) {
            if (dialog_ == Dialog::None) {
                info_title_ = outcome > 0 ? "Scenario won" : "Scenario lost";
                info_body_ = session_.scenario_outcome_text(outcome > 0);
                const int resume = session_.outcome_resume_speed();
                if (resume > 0) {
                    paused_from_ = resume;
                }
                set_speed(0);
                open_dialog(Dialog::Outcome);
            }
        }
        int gx = 0;
        int gy = 0;
        if (session_.take_view_target(gx, gy) && session_.auto_goto()) {
            center_on_fraction((gx + 0.5) / CitySession::kWorldW, (gy + 0.5) / CitySession::kWorldH);
        }
        if (const int strength = session_.take_earthquake()) {
            quake_strength_ = strength;
            quake_started_ = std::chrono::steady_clock::now();
            const int milliseconds = std::max(400, std::min(strength, 1200));
            quake_until_ = quake_started_ + std::chrono::milliseconds(milliseconds);
        }
        note_message();
        if (session_.query_serial() != shown_query_serial_) {
            shown_query_serial_ = session_.query_serial();
            const std::string text = session_.message();
            if (!text.empty()) {
                shown_message_ = text;
                hint_until_ = std::chrono::steady_clock::now() + std::chrono::hours(1);
                query_pinned_ = true;
                info_body_ = query_body(text);
                if (dialog_ == Dialog::None || dialog_ == Dialog::Query) {
                    query_anchored_ = pointer_x_ >= view_x_ && pointer_x_ < view_x_ + view_w_ && pointer_y_ >= view_y_ &&
                                      pointer_y_ < view_y_ + view_h_;
                    query_anchor_x_ = pointer_x_;
                    query_anchor_y_ = pointer_y_;
                    open_dialog(Dialog::Query);
                }
            }
        }
        set_paused_for_dialog();
        render();
        ++frame_;
        if (smoke_ && frame_ == 2) {
            arm_smoke_pad();
        }
#ifdef CITY_REVIEW_CHECKS
        if (review_ && frame_ == 2) {
            run_review_checks();
            quitting_ = true;
        }
#endif
        if (smoke_ && frame_ > 8 && smoke_phase_ == 0) {
            smoke_phase_ = 1;
            session_.set_speed(0);
            speed_ = 0;
            apply_action(Action::Slow);
            if (speed_ != 1) {
                ran_ok_ = false;
                smoke_error_ = "Ctrl+1 did not set the slow speed";
            }
            const int before = tool_index_;
            cycle_tool(1);
            if (tool_index_ == before) {
                ran_ok_ = false;
                smoke_error_ = "cycling tools did not change the tool";
            }
            if (session_.city_name().empty()) {
                ran_ok_ = false;
                smoke_error_ = "smoke city has no name";
            }
            touch_used_ = true;
            if (!save_path_env_.empty() && !session_.save_city_as(save_path_env_)) {
                ran_ok_ = false;
                smoke_error_ = "smoke save failed";
            }
            quitting_ = true;
        }
        if (!demo_.empty() && frame_ == 1) {
            if (demo_ == "menu" || demo_ == "gamepad") {
                menu_.open_first();
                gamepad_nav_ = demo_ == "gamepad";
                if (gamepad_nav_) {
                    cursor_x_ = static_cast<float>(view_x_ + view_w_ / 2);
                    cursor_y_ = static_cast<float>(view_y_ + view_h_ / 2);
                }
            } else if (demo_ == "budget") {
                const auto book = session_.budget();
                tax_ = book.tax_percent;
                road_ = book.road_percent;
                police_ = book.police_percent;
                fire_ = book.fire_percent;
                open_tax_ = tax_;
                open_road_ = road_;
                open_police_ = police_;
                open_fire_ = fire_;
                budget_is_tax_ = false;
                open_dialog(Dialog::Budget);
                gamepad_nav_ = false;
            } else if (demo_ == "graphs") {
                session_.load_scenario(CitySession::scenario_def(0).id);
                speed_ = 0;
                session_.set_speed(0);
                open_dialog(Dialog::Graphs);
                graph_pointer_x_ = 280;
            } else if (demo_ == "evaluation") {
                session_.update_evaluation();
                open_dialog(Dialog::Evaluation);
            } else if (demo_ == "query") {
                info_body_ = query_body("Residential zone — population 120, land value 40, powered");
                open_dialog(Dialog::Query);
            } else if (demo_ == "osk") {
                gamepad_nav_ = true;
                open_dialog(Dialog::NewCity);
                focus_id_ = kOsk;
            } else if (demo_ == "options") {
                menu_.open_menu = 1;
                menu_.cursor = 7;
                menu_.alt_shown = true;
                gamepad_nav_ = true;
            } else if (demo_ == "scenario") {
                scenario_index_ = 2;
                list_index_ = 2;
                list_scroll_ = 0;
                open_dialog(Dialog::Scenario);
                focus_id_ = kListItem + 2;
                gamepad_nav_ = deck_;
            } else if (demo_ == "welcome") {
                welcome_gate_ = true;
                open_dialog(Dialog::Welcome);
            } else if (demo_ == "quit") {
                welcome_gate_ = true;
                open_dialog(Dialog::QuitConfirm);
            } else if (demo_ == "goal") {
                session_.load_scenario(CitySession::scenario_def(0).id);
                speed_ = 0;
                session_.set_speed(0);
                close_dialog();
            } else if (demo_ == "special") {
                select_tool(special_tool_index());
                special_choice_ = 0;
                special_cursor_ = 0;
                special_open_ = true;
                // Deck hint bar: A selects a building, B closes the list.
                gamepad_nav_ = true;
                cursor_x_ = static_cast<float>(view_x_ + view_w_ / 2);
                cursor_y_ = static_cast<float>(view_y_ + view_h_ / 2);
            } else if (demo_ == "gifts") {
                demo_place_gifts(false);
            } else if (demo_ == "giftquery") {
                demo_place_gifts(true);
            } else if (demo_ == "giftbudget") {
                demo_place_gifts(false);
                const auto book = session_.budget();
                tax_ = book.tax_percent;
                road_ = book.road_percent;
                police_ = book.police_percent;
                fire_ = book.fire_percent;
                open_tax_ = tax_;
                open_road_ = road_;
                open_police_ = police_;
                open_fire_ = fire_;
                budget_is_tax_ = false;
                open_dialog(Dialog::Budget);
                gamepad_nav_ = true;
            } else if (demo_ == "crime" || demo_ == "zones" || demo_ == "police" || demo_ == "fire") {
                const int which = demo_ == "crime" ? 5 : 0;
                session_.load_scenario(CitySession::scenario_def(which).id);
                speed_ = 0;
                session_.set_speed(0);
                close_dialog();
                overlay_.set_layer(demo_ == "zones" ? CitySession::MapLayer::Zones
                                   : demo_ == "police" ? CitySession::MapLayer::Police
                                   : demo_ == "fire" ? CitySession::MapLayer::Fire
                                                       : CitySession::MapLayer::Crime);
                overlay_on_ = true;
                goal_dismissed_ = true;
                center_on_fraction(0.5, 0.5);
                gamepad_nav_ = deck_;
            } else if (demo_ == "messages") {
                session_.set_disasters(true);
                session_.disaster_earthquake();
                session_.disaster_flood();
                open_dialog(Dialog::Messages);
                gamepad_nav_ = deck_;
            } else if (demo_ == "terrain") {
                open_dialog(Dialog::NewCity);
                new_page_ = 1;
                focus_id_ = kNewTerrain;
                gamepad_nav_ = deck_;
            } else if (demo_ == "files") {
                const auto dir = std::filesystem::temp_directory_path() / "lunduke-shot-cities";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                session_.save_city_as((dir / "Harbor.cty").string());
                session_.new_city("Mesa", 4);
                session_.set_speed(0);
                speed_ = 0;
                session_.save_city_as((dir / "Mesa.cty").string());
                files_dir_ = dir.string();
                files_save_ = false;
                list_index_ = 0;
                list_scroll_ = 0;
                refresh_files();
                open_dialog(Dialog::Files);
                gamepad_nav_ = deck_;
            } else if (demo_ == "won" || demo_ == "lost") {
                session_.load_scenario(CitySession::scenario_def(demo_ == "lost" ? 2 : 0).id);
                speed_ = 0;
                session_.set_speed(0);
                info_title_ = demo_ == "lost" ? "Scenario lost" : "Scenario won";
                info_body_ = session_.scenario_outcome_text(demo_ != "lost");
                open_dialog(Dialog::Outcome);
            } else if (demo_ == "controls") {
                open_dialog(Dialog::Controls);
                gamepad_nav_ = deck_;
            } else if (demo_ == "settings") {
                open_dialog(Dialog::Settings);
                gamepad_nav_ = deck_;
            } else if (demo_ == "querylive") {
                const int query = tool_by_index(kToolCount - 1)->engine_id;
                for (int y = 2; y < 30; ++y) {
                    for (int x = 2; x < 30; ++x) {
                        session_.use_tool(query, x, y);
                        if (!session_.message().empty()) {
                            y = 30;
                            break;
                        }
                    }
                }
                gamepad_nav_ = deck_;
            } else if (demo_ == "city" || demo_ == "paused") {
                gamepad_nav_ = deck_;
            } else if (demo_ == "unsaved") {
                gamepad_nav_ = true;
                open_dialog(Dialog::Unsaved);
                // Stale map hover: before the id fix this lights the Cancel button.
                hover_id_ = kMapId;
                cursor_x_ = static_cast<float>(view_x_ + view_w_ / 2.0);
                cursor_y_ = static_cast<float>(view_y_ + view_h_ / 2.0);
            }
        }
        if (ui_test_) {
            drive_ui_test();
        }
        if (!demo_.empty() && frame_ >= 3) {
            quitting_ = true;
        }
        write_final_shot();
        if (headless_ && (spin || animate)) {
            SDL_Delay(1);
        }
    }
}

void Shell::tick_sim(int)
{
    if (dialog_pauses()) {
        return;
    }
    session_.tick();
    const int month = session_.game_month_index();
    session_.note_evaluation_month(month);
}

void Shell::handle_event(const SDL_Event &event)
{
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        request_with_save(AfterSave::Quit);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        note_window_geometry();
        layout();
        clamp_scroll();
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        // The pixels under a newly uncovered area are not ours until we paint.
        window_exposed_ = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        reset_touch_state();
        if (pause_unfocused_ && speed_ > 0) {
            unfocus_speed_ = speed_;
            paused_for_unfocus_ = true;
            set_speed(0);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (paused_for_unfocus_) {
            paused_for_unfocus_ = false;
            set_speed(unfocus_speed_ > 0 ? unfocus_speed_ : 0);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        touch_used_ = false;
        int ww = 1;
        int wh = 1;
        int pw = 1;
        int ph = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        pointer_x_ = event.motion.x * (pw / static_cast<double>(std::max(ww, 1)));
        pointer_y_ = event.motion.y * (ph / static_cast<double>(std::max(wh, 1)));
        hover_id_ = hit(pointer_x_, pointer_y_);
        if (menu_.open() && dialog_ == Dialog::None) {
            break;
        }
        if (slider_drag_ != 0) {
            on_hotspot(slider_drag_, pointer_x_, pointer_y_);
        } else if (list_dragging_ && (dialog_ == Dialog::Files || dialog_ == Dialog::Scenario)) {
            on_hotspot(kListScroll, pointer_x_, pointer_y_);
        } else if (pan_pointer_ && dialog_ == Dialog::None && !menu_.open()) {
            const double dx = pointer_x_ - pan_start_x_;
            const double dy = pointer_y_ - pan_start_y_;
            if (dx * dx + dy * dy > 16.0) {
                pan_moved_ = true;
            }
            scroll_x_ = pan_scroll_x_ - dx;
            scroll_y_ = pan_scroll_y_ - dy;
            clamp_scroll();
        } else if (pointer_on_map_ && dialog_ == Dialog::None && !menu_.open()) {
            map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), pointer_down_);
        } else if (dialog_ == Dialog::Graphs && graph_chart_w_ > 1) {
            if (pointer_x_ >= graph_chart_x_ && pointer_x_ <= graph_chart_x_ + graph_chart_w_ &&
                pointer_y_ >= graph_chart_y_ && pointer_y_ <= graph_chart_y_ + graph_chart_h_) {
                graph_pointer_x_ = static_cast<int>(pointer_x_ - graph_chart_x_);
            } else {
                graph_pointer_x_ = -1;
            }
        } else if (hover_id_ >= kSpecialItem && hover_id_ < kSpecialItem + special_building_count()) {
            special_cursor_ = hover_id_ - kSpecialItem;
        } else if (hover_id_ == kPaletteId && !menu_.open()) {
            palette_.motion(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        note_keyboard_input();
        int ww = 1;
        int wh = 1;
        int pw = 1;
        int ph = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        pointer_x_ = event.button.x * (pw / static_cast<double>(std::max(ww, 1)));
        pointer_y_ = event.button.y * (ph / static_cast<double>(std::max(wh, 1)));
        if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
            if (dialog_ != Dialog::None || menu_.open()) {
                break;
            }
            pan_pointer_ = true;
            pan_moved_ = false;
            pan_button_ = event.button.button;
            pan_start_x_ = pointer_x_;
            pan_start_y_ = pointer_y_;
            pan_scroll_x_ = scroll_x_;
            pan_scroll_y_ = scroll_y_;
            SDL_CaptureMouse(true);
            break;
        }
        if (event.button.button != SDL_BUTTON_LEFT) {
            break;
        }
        if (notice_sticky_ && pointer_y_ >= win_h_ - message_h_) {
            dismiss_background_notice();
            break;
        }
        pointer_down_ = true;
        const int id = hit(pointer_x_, pointer_y_);
        hover_id_ = id;
        if (menu_.open() && dialog_ == Dialog::None) {
            on_hotspot(id, pointer_x_, pointer_y_);
            break;
        }
        if (id == kMapId && dialog_ == Dialog::None && !menu_.open()) {
            pointer_on_map_ = true;
            SDL_CaptureMouse(true);
            map_.press(map_local_x(pointer_x_), map_local_y(pointer_y_));
        } else if (id == kPaletteId && dialog_ == Dialog::None) {
            press_palette(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        } else if (id == kMinimapId && dialog_ == Dialog::None) {
            minimap_.press(pointer_x_ - side_x_, pointer_y_ - mini_y_, side_w_, mini_h_);
        } else {
            on_hotspot(id, pointer_x_, pointer_y_);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_MIDDLE || event.button.button == SDL_BUTTON_RIGHT) {
            if (event.button.button == SDL_BUTTON_RIGHT && pan_pointer_ && !pan_moved_) {
                if (pointer_on_map_) {
                    map_.release();
                    pointer_on_map_ = false;
                    pointer_down_ = false;
                }
                if (tool_index_ == 0) {
                    select_tool(kToolCount - 1);
                }
            }
            pan_pointer_ = false;
            SDL_CaptureMouse(false);
            break;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            pointer_down_ = false;
            slider_drag_ = 0;
            list_dragging_ = false;
            if (pointer_on_map_) {
                map_.release();
                pointer_on_map_ = false;
                SDL_CaptureMouse(false);
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        note_keyboard_input();
        const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        if ((dialog_ == Dialog::Files || dialog_ == Dialog::Scenario) && !ctrl) {
            const int rows = std::max(1, std::abs(static_cast<int>(event.wheel.y)));
            nudge_file_list(event.wheel.y > 0 ? -rows : rows, false, true);
            break;
        }
        if (dialog_ == Dialog::Controls && !ctrl) {
            const int step = std::max(1, std::abs(static_cast<int>(event.wheel.y))) * dp(28);
            controls_scroll_ += event.wheel.y > 0 ? -step : step;
            if (controls_scroll_ < 0) {
                controls_scroll_ = 0;
            }
            if (controls_scroll_ > controls_scroll_max_) {
                controls_scroll_ = controls_scroll_max_;
            }
            break;
        }
        if (dialog_ != Dialog::None || menu_.open()) {
            break;
        }
        if (ctrl || wheel_zooms_) {
            pointer_x_ = event.wheel.mouse_x;
            pointer_y_ = event.wheel.mouse_y;
            zoom_by(event.wheel.y > 0 ? 2 : -2, pointer_x_, pointer_y_);
        } else if (dialog_ == Dialog::None && !menu_.open()) {
            scroll_x_ -= event.wheel.x * 48.0;
            scroll_y_ -= event.wheel.y * 48.0;
            clamp_scroll();
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        note_keyboard_input();
        const SDL_Keycode key = event.key.key;
        const int mods = mods_from_sdl(event.key.mod);
        const AccelKey accel = accel_from_key(key);
        const char letter = letter_from_key(key);
        if (key == SDLK_F11 || (key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT) != 0)) {
            toggle_fullscreen();
            break;
        }
        if (dialog_ == Dialog::Files && accel == AccelKey::Return && (event.key.mod & SDL_KMOD_ALT) == 0 &&
            !(files_save_ && focus_id_ == kFieldName)) {
            activate_file_row();
            break;
        }
        if (active_field() != nullptr) {
            const bool ctrl = (mods & static_cast<int>(AccelMod::Ctrl)) != 0;
            const bool alt = (mods & static_cast<int>(AccelMod::Alt)) != 0;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            // Letter keycodes are the Unicode values ('a' is 0x61, 'v' is 0x76).
            // The SDLK_a / SDLK_v old-name macros do not compile with this SDL.
            if (ctrl && key == static_cast<SDL_Keycode>('a')) {
                select_field();
                break;
            }
            if (ctrl && key == static_cast<SDL_Keycode>('v')) {
                paste_field();
                break;
            }
            if (!ctrl && !alt) {
                if (accel == AccelKey::Escape) {
                    on_dialog_button(kCancel);
                    break;
                }
                if (accel == AccelKey::Return) {
                    activate_focus();
                    break;
                }
                if (key == SDLK_BACKSPACE) {
                    backspace_field();
                    break;
                }
                if (accel == AccelKey::Left || accel == AccelKey::Right) {
                    move_field_caret(accel == AccelKey::Left ? -1 : 1, shift, false, false);
                    break;
                }
                if (key == SDLK_HOME || key == SDLK_END) {
                    move_field_caret(0, shift, key == SDLK_HOME, key == SDLK_END);
                    break;
                }
                if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
                    nudge_file_list(key == SDLK_PAGEUP ? -1 : 1, true, true);
                    break;
                }
                if (key == SDLK_TAB) {
                    move_focus((event.key.mod & SDL_KMOD_SHIFT) != 0 ? -1 : 1);
                    break;
                }
                break;
            }
        }
        if (dialog_ != Dialog::None) {
            if (accel == AccelKey::Escape) {
                on_dialog_button(kCancel);
            } else if (accel == AccelKey::Return || accel == AccelKey::Space) {
                activate_focus();
            } else if (accel == AccelKey::Left || accel == AccelKey::Right) {
                const int direction = accel == AccelKey::Left ? -1 : 1;
                if (dialog_ == Dialog::Controls) {
                    controls_page_ = (controls_page_ + (direction > 0 ? 1 : 2)) % 3;
                    controls_scroll_ = 0;
                    break;
                }
                if (!adjust_focused_slider(direction)) {
                    move_focus(direction);
                }
            } else if (accel == AccelKey::Up) {
                if (dialog_ == Dialog::Controls && controls_scroll_max_ > 0) {
                    controls_scroll_ = std::max(0, controls_scroll_ - dp(28));
                    break;
                }
                if (nudge_file_list(-1, false, false)) {
                    break;
                }
                if (!move_osk_vertically(-1)) {
                    move_focus(-1);
                }
            } else if (accel == AccelKey::Down) {
                if (dialog_ == Dialog::Controls && controls_scroll_max_ > 0) {
                    controls_scroll_ = std::min(controls_scroll_max_, controls_scroll_ + dp(28));
                    break;
                }
                if (nudge_file_list(1, false, false)) {
                    break;
                }
                if (!move_osk_vertically(1)) {
                    move_focus(1);
                }
            } else if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
                nudge_file_list(key == SDLK_PAGEUP ? -1 : 1, true, true);
            } else if (key == SDLK_TAB) {
                move_focus((event.key.mod & SDL_KMOD_SHIFT) != 0 ? -1 : 1);
            }
            break;
        }
        if (accel == AccelKey::F10 && !menu_.open()) {
            menu_.open_first();
            break;
        }
        if (menu_.handle_key(accel, mods, letter)) {
            if (menu_.activated == Action::RecentIndex && menu_.activated_recent >= 0 &&
                menu_.activated_recent < static_cast<int>(recent_.size())) {
                queued_path_ = recent_[static_cast<std::size_t>(menu_.activated_recent)];
                request_with_save(AfterSave::OpenPath);
            } else if (menu_.activated != Action::None) {
                apply_action(menu_.activated);
            }
            break;
        }
        if (mods == 0 && !menu_.open() && !special_open_ && active_field() == nullptr) {
            const int keyed = tool_index_for_key(letter);
            if (keyed >= 0) {
                select_tool(keyed);
                break;
            }
        }
        if ((mods & static_cast<int>(AccelMod::Alt)) == 0) {
            const Action action = menu_table::accel_action(accel, mods);
            if (action != Action::None) {
                apply_action(action);
                break;
            }
            const ZoomAction zoom = zoom_action(gdk_key(key), gdk_mods(event.key.mod));
            if (zoom == ZoomAction::In) {
                zoom_by(2);
                break;
            }
            if (zoom == ZoomAction::Out) {
                zoom_by(-2);
                break;
            }
        }
        if (accel == AccelKey::Escape && dialog_ == Dialog::None && !menu_.open() && !special_open_ && overlay_on_) {
            overlay_on_ = false;
            break;
        }
        if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
            if (accel == AccelKey::Escape) {
                special_open_ = false;
                break;
            }
            if (accel == AccelKey::Return || accel == AccelKey::Space) {
                choose_special(special_cursor_);
                break;
            }
            if (accel == AccelKey::Up || accel == AccelKey::Left) {
                move_special_cursor(-1);
                break;
            }
            if (accel == AccelKey::Down || accel == AccelKey::Right) {
                move_special_cursor(1);
                break;
            }
        }
        if (accel == AccelKey::Space) {
            toggle_pause();
            break;
        }
        if (accel == AccelKey::Left) {
            scroll_x_ -= map_.tile_size() * 2;
        } else if (accel == AccelKey::Right) {
            scroll_x_ += map_.tile_size() * 2;
        } else if (accel == AccelKey::Up) {
            scroll_y_ -= map_.tile_size() * 2;
        } else if (accel == AccelKey::Down) {
            scroll_y_ += map_.tile_size() * 2;
        }
        clamp_scroll();
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text != nullptr) {
            type_text(event.text.text);
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (pad_ == nullptr) {
            pad_ = SDL_OpenGamepad(event.gdevice.which);
            gamepad_nav_ = pad_ != nullptr;
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        forget_gamepad(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        gamepad_nav_ = true;
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        if (button == SDL_GAMEPAD_BUTTON_START) {
            if (dialog_ != Dialog::None && focus_id_ >= kOsk && focus_id_ < kOsk + 64) {
                if (dialog_ == Dialog::NewCity) {
                    on_dialog_button(new_page_ < 2 ? kNext : kGenerate);
                } else if (dialog_ == Dialog::Rename) {
                    on_dialog_button(kRename);
                } else if (dialog_ == Dialog::Files && files_save_) {
                    on_dialog_button(kSave);
                } else {
                    on_dialog_button(kOk);
                }
                break;
            }
            if (active_field() != nullptr) {
                activate_focus();
                break;
            }
            special_open_ = false;
            if (dialog_ == Dialog::None) {
                if (!menu_.open()) {
                    menu_.open_first();
                } else {
                    menu_.close();
                }
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK) {
            if (active_field() != nullptr && dialog_ != Dialog::None) {
                if (active_field()->empty()) {
                    on_dialog_button(kCancel);
                } else {
                    backspace_field();
                }
                break;
            }
            if (overlay_on_ && dialog_ == Dialog::None && !menu_.open() && !special_open_) {
                overlay_on_ = false;
                break;
            }
            if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
                special_open_ = false;
            } else if (menu_.open()) {
                menu_.handle_key(AccelKey::Escape, 0, 0);
            } else if (dialog_ != Dialog::None) {
                on_dialog_button(kCancel);
            } else if (pad_dragging_ || pointer_on_map_) {
                release_pad_drag();
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
                choose_special(special_cursor_);
            } else if (menu_.open()) {
                menu_.handle_key(AccelKey::Return, 0, 0);
                if (menu_.activated == Action::RecentIndex && menu_.activated_recent >= 0 &&
                    menu_.activated_recent < static_cast<int>(recent_.size())) {
                    queued_path_ = recent_[static_cast<std::size_t>(menu_.activated_recent)];
                    request_with_save(AfterSave::OpenPath);
                } else if (menu_.activated != Action::None) {
                    apply_action(menu_.activated);
                }
            } else if (dialog_ != Dialog::None) {
                activate_focus();
            } else {
                gamepad_use_cursor(true);
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_WEST) {
            if (active_field() != nullptr) {
                backspace_field();
            } else if (dialog_ == Dialog::None && !menu_.open()) {
                select_tool(kToolCount - 1);
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_NORTH) {
            if (show_osk() && active_field() != nullptr) {
                type_text(" ");
            } else if (dialog_ == Dialog::NewCity && new_page_ == 1) {
                reroll_terrain();
            } else if (dialog_ == Dialog::None && !menu_.open()) {
                toggle_pause();
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && !menu_.open() && dialog_ == Dialog::None) {
            cycle_tool(-1);
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && !menu_.open() && dialog_ == Dialog::None) {
            cycle_tool(1);
            break;
        }
        AccelKey dir = AccelKey::None;
        if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
            dir = AccelKey::Up;
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
            dir = AccelKey::Down;
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
            dir = AccelKey::Left;
        } else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            dir = AccelKey::Right;
        }
        if (dir == AccelKey::None) {
            break;
        }
        if (pad_dragging_) {
            break;
        }
        if (special_open_ && dialog_ == Dialog::None && !menu_.open()) {
            if (dir == AccelKey::Up || dir == AccelKey::Left) {
                move_special_cursor(-1);
            } else {
                move_special_cursor(1);
            }
            break;
        }
        if (menu_.open()) {
            menu_.handle_key(dir, 0, 0);
        } else if (dialog_ != Dialog::None) {
            if (dialog_ == Dialog::Controls && (dir == AccelKey::Left || dir == AccelKey::Right)) {
                controls_page_ = (controls_page_ + (dir == AccelKey::Right ? 1 : 2)) % 3;
                controls_scroll_ = 0;
                break;
            }
            if (dialog_ == Dialog::Controls && controls_scroll_max_ > 0 &&
                (dir == AccelKey::Up || dir == AccelKey::Down)) {
                const int step = dp(28);
                controls_scroll_ += dir == AccelKey::Up ? -step : step;
                if (controls_scroll_ < 0) {
                    controls_scroll_ = 0;
                }
                if (controls_scroll_ > controls_scroll_max_) {
                    controls_scroll_ = controls_scroll_max_;
                }
                break;
            }
            if ((dir == AccelKey::Left || dir == AccelKey::Right) &&
                adjust_focused_slider(dir == AccelKey::Left ? -1 : 1)) {
                break;
            }
            if (dir == AccelKey::Up && nudge_file_list(-1, false, false)) {
                break;
            }
            if (dir == AccelKey::Down && nudge_file_list(1, false, false)) {
                break;
            }
            if ((dir == AccelKey::Up && move_osk_vertically(-1)) || (dir == AccelKey::Down && move_osk_vertically(1))) {
                break;
            }
            move_focus(dir == AccelKey::Up || dir == AccelKey::Left ? -1 : 1);
        } else {
            const int pan = map_.tile_size() * 3;
            if (dir == AccelKey::Left) {
                scroll_x_ -= pan;
            } else if (dir == AccelKey::Right) {
                scroll_x_ += pan;
            } else if (dir == AccelKey::Up) {
                scroll_y_ -= pan;
            } else if (dir == AccelKey::Down) {
                scroll_y_ += pan;
            }
            clamp_scroll();
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            release_pad_drag();
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const float norm = std::max(-1.0f, std::min(1.0f, event.gaxis.value / 32767.0f));
        const float dead = std::fabs(norm) < 0.18f ? 0.0f : norm;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            stick_x_ = dead;
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            stick_y_ = dead;
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX) {
            pan_x_ = dead;
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) {
            pan_y_ = dead;
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
            trig_l_ = std::max(0.0f, norm);
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            trig_r_ = std::max(0.0f, norm);
        }
        if (dead != 0.0f) {
            gamepad_nav_ = true;
        }
        break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
        touch_used_ = true;
        const float x = event.tfinger.x * static_cast<float>(win_w_);
        const float y = event.tfinger.y * static_cast<float>(win_h_);
        const bool down = event.type == SDL_EVENT_FINGER_DOWN;
        const bool up = event.type == SDL_EVENT_FINGER_UP;
        if (down) {
            fingers_.push_back(Finger{event.tfinger.fingerID, x, y});
        } else if (up) {
            fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(),
                                          [&](const Finger &finger) { return finger.id == event.tfinger.fingerID; }),
                           fingers_.end());
        } else {
            for (Finger &finger : fingers_) {
                if (finger.id == event.tfinger.fingerID) {
                    finger.x = x;
                    finger.y = y;
                }
            }
        }
        if (menu_.open() && dialog_ == Dialog::None) {
            pointer_x_ = x;
            pointer_y_ = y;
            if (down) {
                on_hotspot(hit(pointer_x_, pointer_y_), pointer_x_, pointer_y_);
            }
            if (up && fingers_.empty()) {
                reset_touch_state();
            }
            break;
        }
        // 8px. A shorter move is still a tap. Past that, the drag builds.
        constexpr float kTouchArm2 = 8.0f * 8.0f;
        if (fingers_.size() >= 2) {
            // Pending: nothing was built, so cancel. Armed: end the stroke
            // and keep the tiles. Never restore a map or the treasury.
            if (touch_armed_) {
                map_.release();
                pointer_on_map_ = false;
                pointer_down_ = false;
            }
            touch_pending_ = false;
            touch_armed_ = false;
            touch_minimap_ = false;
            const float dx = fingers_[0].x - fingers_[1].x;
            const float dy = fingers_[0].y - fingers_[1].y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float cx = (fingers_[0].x + fingers_[1].x) * 0.5f;
            const float cy = (fingers_[0].y + fingers_[1].y) * 0.5f;
            if (!(pinch_dist_ > 1.0f)) {
                pinch_dist_ = dist;
            } else if (event.type == SDL_EVENT_FINGER_MOTION && dialog_ == Dialog::None) {
                scroll_x_ -= (cx - pointer_x_);
                scroll_y_ -= (cy - pointer_y_);
                clamp_scroll();
                const int pinch = pinch_zoom_step(dist, pinch_dist_);
                if (pinch > 0) {
                    zoom_by(2, cx, cy);
                } else if (pinch < 0) {
                    zoom_by(-2, cx, cy);
                }
            }
            pointer_x_ = cx;
            pointer_y_ = cy;
        } else if (up && fingers_.empty()) {
            // The finger is already gone, so this must not look at fingers_.
            pointer_x_ = x;
            pointer_y_ = y;
            if (touch_pending_ && !touch_armed_) {
                const float dx = x - touch_down_x_;
                const float dy = y - touch_down_y_;
                if (dx * dx + dy * dy <= kTouchArm2 && dialog_ == Dialog::None && !menu_.open()) {
                    map_.press(map_local_x(touch_down_x_), map_local_y(touch_down_y_));
                    map_.release();
                }
            } else if (touch_armed_) {
                map_.release();
                pointer_on_map_ = false;
                pointer_down_ = false;
            }
            reset_touch_state();
        } else if (up) {
            // One finger of a pinch lifted. The one still down is not a tap.
            pinch_dist_ = 0;
            touch_pending_ = false;
            touch_armed_ = false;
            touch_minimap_ = false;
            pointer_down_ = false;
            if (pointer_on_map_) {
                map_.release();
                pointer_on_map_ = false;
            }
        } else if (fingers_.size() == 1) {
            pinch_dist_ = 0;
            pointer_x_ = fingers_[0].x;
            pointer_y_ = fingers_[0].y;
            if (down) {
                pointer_down_ = true;
                const int id = hit(pointer_x_, pointer_y_);
                if (id == kPaletteId && dialog_ == Dialog::None) {
                    press_palette(pointer_x_ - side_x_, pointer_y_ - pal_y_);
                } else if (id == kMinimapId && dialog_ == Dialog::None) {
                    touch_pending_ = false;
                    touch_armed_ = false;
                    touch_minimap_ = true;
                    minimap_.press(pointer_x_ - side_x_, pointer_y_ - mini_y_, side_w_, mini_h_);
                } else if (id == kMapId && dialog_ == Dialog::None) {
                    touch_minimap_ = false;
                    touch_pending_ = true;
                    touch_armed_ = false;
                    touch_down_x_ = pointer_x_;
                    touch_down_y_ = pointer_y_;
                } else {
                    on_hotspot(id, pointer_x_, pointer_y_);
                }
                if (dialog_ == Dialog::Files || dialog_ == Dialog::Scenario || dialog_ == Dialog::Messages) {
                    list_touch_y_ = y;
                }
            } else if (event.type == SDL_EVENT_FINGER_MOTION && touch_minimap_) {
                minimap_.press(pointer_x_ - side_x_, pointer_y_ - mini_y_, side_w_, mini_h_);
            } else if (event.type == SDL_EVENT_FINGER_MOTION &&
                       (dialog_ == Dialog::Files || dialog_ == Dialog::Scenario || dialog_ == Dialog::Messages) &&
                       list_touch_y_ >= 0.0f) {
                const int rows = static_cast<int>((y - list_touch_y_) / 32.0f);
                if (rows != 0) {
                    nudge_file_list(-rows, false, true);
                    list_touch_y_ += static_cast<float>(rows) * 32.0f;
                }
            } else if (event.type == SDL_EVENT_FINGER_MOTION && touch_pending_ && !touch_armed_) {
                const float dx = pointer_x_ - touch_down_x_;
                const float dy = pointer_y_ - touch_down_y_;
                if (dx * dx + dy * dy > kTouchArm2) {
                    arm_touch_tool();
                }
            } else if (event.type == SDL_EVENT_FINGER_MOTION && touch_armed_) {
                map_.motion(map_local_x(pointer_x_), map_local_y(pointer_y_), true);
            }
        }
        break;
    }
    default:
        break;
    }
}

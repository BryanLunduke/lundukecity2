#include <sstream>
#include <fstream>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "city_seed.hpp"
#include "dialog_copy.hpp"
#include "notice_bar.hpp"
#include "platform.hpp"
#include "save_path.hpp"
#include "tools.hpp"
#include "version.hpp"
#include "zoom_keys.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

namespace {

constexpr int kMapId = 1;
constexpr int kPaletteId = 2;
constexpr int kMinimapId = 3;
constexpr int kWelcomeId = 4;
constexpr int kMenuTitle = 10;
constexpr int kMenuItem = 1000;
constexpr int kRecentItem = 2000;
constexpr int kListItem = 7000;
constexpr int kFieldName = 6001;
constexpr int kFieldSeed = 6002;
constexpr int kOsk = 6100;
constexpr int kSlider = 8000;
constexpr int kChoice = 8300;

constexpr int kCancel = 1;
constexpr int kOk = 2;
constexpr int kDiscard = 3;
constexpr int kSave = 4;
constexpr int kBack = 5;
constexpr int kNext = 6;
constexpr int kGenerate = 7;
constexpr int kPlay = 8;
constexpr int kRename = 9;
constexpr int kClose = 10;
constexpr int kReset = 11;
constexpr int kCollect = 12;
constexpr int kKeep = 13;
constexpr int kStay = 14;
constexpr int kScenarioList = 15;
constexpr int kRecover = 16;
constexpr int kNotNow = 17;
constexpr int kDelete = 18;
constexpr int kUp = 19;
constexpr int kSystemFiles = 20;
constexpr int kShortScale = 21;
constexpr int kLongScale = 22;

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
    // Pause before the first city exists, matching the gtkmm shell. Setting
    // speed 0 afterwards would mark the untouched city as unsaved work.
    session_.set_speed(0);
    speed_ = 0;
    session_.new_city("New City");
    session_.set_speed(0);
    bind_session();
    welcome_ = true;
    const std::string dismissed = city_config_dir().empty() ? std::string() : city_config_dir() + "/welcome-dismissed";
    if (!dismissed.empty()) {
        std::ifstream in(dismissed);
        welcome_ = !in.good();
    }
    load_recent();
    started_ = std::chrono::steady_clock::now();
    next_autosave_ = started_ + std::chrono::seconds(60);
    layout();
    center_on_fraction(0.5, 0.5);
    // Command-line opens happen before the first tick, so a brand-new city
    // has not yet become unsaved work and the file replaces it directly.
    while (dialog_ == Dialog::None && !pending_opens_.empty()) {
        queued_path_ = pending_opens_.front();
        pending_opens_.erase(pending_opens_.begin());
        request_with_save(AfterSave::OpenPath);
    }
    if (smoke_ && !ui_test_ && pending_opens_.empty() && session_.city_name() == "New City") {
        session_.load_scenario(CitySession::scenario_def(0).id);
        speed_ = session_.speed();
        center_on_fraction(0.5, 0.5);
    }
    if (ui_test_) {
        welcome_ = false;
        recovery_checked_ = true;
        speed_ = 0;
        session_.set_speed(0);
    }
    if (!demo_.empty() && demo_ != "budget" && demo_ != "menu") {
        int ox = 0;
        int oy = 0;
        if (session_.stamp_neighborhood(ox, oy)) {
            center_on_fraction((ox + 8.0) / CitySession::kWorldW, (oy + 8.0) / CitySession::kWorldH);
        }
    }
#ifndef _WIN32
    std::signal(SIGTERM, on_terminate);
    std::signal(SIGHUP, on_terminate);
#endif
    loop();
    shutdown();
    if ((smoke_ || ui_test_) && !ran_ok_) {
        std::cerr << smoke_error_ << "\n";
        return 2;
    }
    return 0;
}

bool Shell::init_video()
{
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
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    window_ = SDL_CreateWindow(kProductName, 1280, 800, flags);
    if (window_ == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }
    if (deck_) {
        SDL_SetWindowFullscreen(window_, true);
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    sync_title();
    return true;
}

void Shell::bind_session()
{
    map_.set_session(&session_);
    minimap_.set_session(&session_);
    demand_.set_session(&session_);
    overlay_.set_session(&session_);
    select_tool(kDefaultToolIndex);
    map_.on_tool_down = [this](int x, int y) { session_.use_tool(tool_by_index(tool_index_)->engine_id, x, y); };
    map_.on_tool_drag = [this](int x0, int y0, int x1, int y1) {
        session_.drag_tool(tool_by_index(tool_index_)->engine_id, x0, y0, x1, y1);
    };
    palette_.on_selected = [this](int index) { select_tool(index); };
    minimap_.set_viewport_provider([this](double &fx, double &fy, double &fw, double &fh) {
        const double mw = std::max(1, map_.pixel_width());
        const double mh = std::max(1, map_.pixel_height());
        fx = scroll_x_ / mw;
        fy = scroll_y_ / mh;
        fw = view_w_ / mw;
        fh = view_h_ / mh;
    });
    minimap_.on_jump = [this](double fx, double fy) { center_on_fraction(fx, fy); };
    session_.set_listener([this] { note_message(); });
}

void Shell::shutdown()
{
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
        SDL_Event event;
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
        if (elapsed > 100) {
            elapsed = 100;
        }
        last = now;
        sim_acc += elapsed;
        map_.tick(elapsed);
        if (zoom_cool_ > 0) {
            zoom_cool_ -= elapsed;
        }
        while (sim_acc >= 100) {
            sim_acc -= 100;
            tick_sim(100);
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
            if (zoom_cool_ <= 0 && trig_r_ > 0.6f) {
                zoom_by(2);
                zoom_cool_ = 180;
            }
            if (zoom_cool_ <= 0 && trig_l_ > 0.6f) {
                zoom_by(-2);
                zoom_cool_ = 180;
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
            map_.motion(cursor_x_ - view_x_ + scroll_x_, cursor_y_ - view_y_ + scroll_y_, true);
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
                map_.motion(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_, true);
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

        // ---- local verification harness (not for upstream) ----
        {
            static std::vector<std::string> script;
            static bool loaded = false;
            static std::size_t pos = 0;
            static SDL_Joystick *vj = nullptr;
            static int wait_until = 0;
            const char *path = std::getenv("LUNDUKE_CITY2_PADSCRIPT");
            if (path != nullptr && !loaded) {
                loaded = true;
                std::ifstream in(path);
                std::string line;
                while (std::getline(in, line)) {
                    if (!line.empty() && line[0] != '#') script.push_back(line);
                }
                SDL_VirtualJoystickDesc desc;
                SDL_INIT_INTERFACE(&desc);
                desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
                desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
                desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
                desc.button_mask = 0x7FFFu;
                desc.axis_mask = 0x3Fu;
                desc.name = "Harness Pad";
                const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
                vj = SDL_OpenJoystick(id);
                std::cerr << "harness: pad id " << id << " vj " << (vj != nullptr) << "\n";
                wait_until = frame_ + 10;
            }
            while (path != nullptr && pos < script.size() && frame_ >= wait_until) {
                std::istringstream ls(script[pos++]);
                std::string cmd, arg;
                int v = 0;
                ls >> cmd >> arg >> v;
                auto dump = [&](const std::string &what) {
                    std::cerr << "harness: " << what << " frame=" << frame_ << " tool=" << tool_index_ << " dialog=" << static_cast<int>(dialog_)
                              << " menu=" << menu_.open_menu << " cursorItem=" << menu_.cursor << " focus=" << focus_id_ << " tax=" << tax_
                              << " road=" << road_ << " cur=" << cursor_x_ << "," << cursor_y_ << " scroll=" << scroll_x_ << "," << scroll_y_
                              << " funds=" << session_.funds_text() << " speed=" << speed_ << " padnav=" << gamepad_nav_ << " pad=" << (pad_ != nullptr) << "\n";
                };
                if (cmd == "wait") { wait_until = frame_ + std::stoi(arg); break; }
                if (cmd == "shot") { shot_path_ = arg; render(); shot_path_.clear(); continue; }
                if (cmd == "quit") { quitting_ = true; break; }
                if (cmd == "state") { dump("state"); continue; }
                if (cmd == "cursor") { cursor_x_ = std::stof(arg); cursor_y_ = static_cast<float>(v); continue; }
                if (cmd == "btn") {
                    const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(arg.c_str());
                    SDL_SetJoystickVirtualButton(vj, b, v != 0);
                    SDL_UpdateJoysticks();
                    wait_until = frame_ + 2;
                    break;
                }
                if (cmd == "axis") {
                    const SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(arg.c_str());
                    SDL_SetJoystickVirtualAxis(vj, a, static_cast<Sint16>(v));
                    SDL_UpdateJoysticks();
                    continue;
                }
            }
        }
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
                open_dialog(Dialog::Graphs);
            } else if (demo_ == "evaluation") {
                session_.update_evaluation();
                open_dialog(Dialog::Evaluation);
            } else if (demo_ == "query") {
                info_body_ = query_body("Residential zone — population 120, land value 40, powered");
                open_dialog(Dialog::Query);
            } else if (demo_ == "osk") {
                gamepad_nav_ = true;
                open_dialog(Dialog::NewCity);
            }
        }
        if (ui_test_) {
            drive_ui_test();
        }
        if (!demo_.empty() && frame_ >= 3) {
            quitting_ = true;
        }
        if (headless_ || deck_) {
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
        layout();
        clamp_scroll();
        break;
    case SDL_EVENT_MOUSE_MOTION: {
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
        } else if (pointer_on_map_ && dialog_ == Dialog::None && !menu_.open()) {
            map_.motion(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_, pointer_down_);
        } else if (hover_id_ == kPaletteId && !menu_.open()) {
            palette_.motion(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        int ww = 1;
        int wh = 1;
        int pw = 1;
        int ph = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        pointer_x_ = event.button.x * (pw / static_cast<double>(std::max(ww, 1)));
        pointer_y_ = event.button.y * (ph / static_cast<double>(std::max(wh, 1)));
        if (event.button.button != SDL_BUTTON_LEFT) {
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
            map_.press(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_);
        } else if (id == kPaletteId && dialog_ == Dialog::None) {
            palette_.press(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        } else if (id == kMinimapId && dialog_ == Dialog::None) {
            minimap_.press(pointer_x_ - side_x_, pointer_y_ - mini_y_, side_w_, mini_h_);
        } else {
            on_hotspot(id, pointer_x_, pointer_y_);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) {
            pointer_down_ = false;
            slider_drag_ = 0;
            if (pointer_on_map_) {
                map_.release();
                pointer_on_map_ = false;
                SDL_CaptureMouse(false);
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        if (ctrl) {
            zoom_by(event.wheel.y > 0 ? 2 : -2);
        } else if (dialog_ == Dialog::None && !menu_.open()) {
            scroll_x_ -= event.wheel.x * 48.0;
            scroll_y_ -= event.wheel.y * 48.0;
            clamp_scroll();
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode key = event.key.key;
        const int mods = mods_from_sdl(event.key.mod);
        const AccelKey accel = accel_from_key(key);
        const char letter = letter_from_key(key);
        if (key == SDLK_F11 || (key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT) != 0)) {
            const bool full = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window_, !full);
            break;
        }
        if (active_field() != nullptr && (mods & static_cast<int>(AccelMod::Ctrl)) == 0 &&
            (mods & static_cast<int>(AccelMod::Alt)) == 0) {
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
            if (key == SDLK_TAB) {
                move_focus((event.key.mod & SDL_KMOD_SHIFT) != 0 ? -1 : 1);
                break;
            }
            break;
        }
        if (dialog_ != Dialog::None) {
            if (accel == AccelKey::Escape) {
                on_dialog_button(kCancel);
            } else if (accel == AccelKey::Return || accel == AccelKey::Space) {
                activate_focus();
            } else if (accel == AccelKey::Left || accel == AccelKey::Right) {
                const int direction = accel == AccelKey::Left ? -1 : 1;
                if (!adjust_focused_slider(direction)) {
                    move_focus(direction);
                }
            } else if (accel == AccelKey::Up) {
                move_focus(-1);
            } else if (accel == AccelKey::Down) {
                move_focus(1);
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
        if (pad_ != nullptr && SDL_GetGamepadID(pad_) == event.gdevice.which) {
            SDL_CloseGamepad(pad_);
            pad_ = nullptr;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        gamepad_nav_ = true;
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        if (button == SDL_GAMEPAD_BUTTON_START) {
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
            if (menu_.open()) {
                menu_.handle_key(AccelKey::Escape, 0, 0);
            } else if (dialog_ != Dialog::None) {
                on_dialog_button(kCancel);
            } else if (pad_dragging_ || pointer_on_map_) {
                release_pad_drag();
            }
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            if (menu_.open()) {
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
        if (button == SDL_GAMEPAD_BUTTON_WEST && dialog_ == Dialog::None && !menu_.open()) {
            select_tool(kToolCount - 1);
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_NORTH && dialog_ == Dialog::None && !menu_.open()) {
            toggle_pause();
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && !menu_.open()) {
            cycle_tool(-1);
            break;
        }
        if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && !menu_.open()) {
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
        if (menu_.open()) {
            menu_.handle_key(dir, 0, 0);
        } else if (dialog_ != Dialog::None) {
            if ((dir == AccelKey::Left || dir == AccelKey::Right) &&
                adjust_focused_slider(dir == AccelKey::Left ? -1 : 1)) {
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
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            fingers_.push_back(Finger{event.tfinger.fingerID, x, y});
        } else if (event.type == SDL_EVENT_FINGER_UP) {
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
            if (event.type == SDL_EVENT_FINGER_DOWN) {
                on_hotspot(hit(pointer_x_, pointer_y_), pointer_x_, pointer_y_);
            }
            break;
        }
        if (fingers_.size() >= 2) {
            if (pointer_on_map_) {
                map_.release();
                pointer_on_map_ = false;
            }
            const float dx = fingers_[0].x - fingers_[1].x;
            const float dy = fingers_[0].y - fingers_[1].y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float cx = (fingers_[0].x + fingers_[1].x) * 0.5f;
            const float cy = (fingers_[0].y + fingers_[1].y) * 0.5f;
            if (event.type == SDL_EVENT_FINGER_MOTION && pinch_dist_ > 1.0f) {
                scroll_x_ -= (cx - pointer_x_);
                scroll_y_ -= (cy - pointer_y_);
                clamp_scroll();
                if (pinch_dist_ > 0 && dist > pinch_dist_ * 1.08f) {
                    zoom_by(2);
                    pinch_dist_ = dist;
                } else if (dist < pinch_dist_ * 0.92f) {
                    zoom_by(-2);
                    pinch_dist_ = dist;
                }
            }
            pinch_dist_ = dist;
            pointer_x_ = cx;
            pointer_y_ = cy;
        } else if (fingers_.size() == 1) {
            pinch_dist_ = 0;
            pointer_x_ = fingers_[0].x;
            pointer_y_ = fingers_[0].y;
            if (event.type == SDL_EVENT_FINGER_DOWN) {
                pointer_down_ = true;
                const int id = hit(pointer_x_, pointer_y_);
                if (menu_.open() && dialog_ == Dialog::None) {
                    on_hotspot(id, pointer_x_, pointer_y_);
                } else if (id == kMapId && dialog_ == Dialog::None) {
                    pointer_on_map_ = true;
                    map_.press(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_);
                } else {
                    on_hotspot(id, pointer_x_, pointer_y_);
                }
            } else if (event.type == SDL_EVENT_FINGER_MOTION && pointer_on_map_) {
                map_.motion(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_, true);
            } else if (event.type == SDL_EVENT_FINGER_UP) {
                pointer_down_ = false;
                if (pointer_on_map_) {
                    map_.release();
                    pointer_on_map_ = false;
                }
            }
        } else {
            pinch_dist_ = 0;
            pointer_down_ = false;
            if (pointer_on_map_) {
                map_.release();
                pointer_on_map_ = false;
            }
        }
        break;
    }
    default:
        break;
    }
}

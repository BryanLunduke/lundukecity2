// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell_state.hpp"

#include "osk_layout.hpp"

#include <SDL3/SDL.h>

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_menu_rows = 0;
bool g_menu_seen = false;

const Hotspot *find_hot(const std::vector<Hotspot> &hot, int id, double min_y)
{
    const Hotspot *found = nullptr;
    for (const Hotspot &item : hot) {
        if (item.id == id && item.y >= min_y) {
            found = &item;
        }
    }
    return found;
}

bool channel_at(cairo_surface_t *surface, int x, int y, int &r, int &g, int &b)
{
    if (surface == nullptr || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) {
        return false;
    }
    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return false;
    }
    const int stride = cairo_image_surface_get_stride(surface);
    const auto *data = cairo_image_surface_get_data(surface);
    std::uint32_t pixel = 0;
    std::memcpy(&pixel, data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * 4u, 4);
    r = static_cast<int>((pixel >> 16) & 255u);
    g = static_cast<int>((pixel >> 8) & 255u);
    b = static_cast<int>(pixel & 255u);
    return true;
}

bool rects_overlap(const Hotspot &a, const Hotspot &b)
{
    return a.x < b.x + b.w - 0.5 && b.x < a.x + a.w - 0.5 && a.y < b.y + b.h - 0.5 && b.y < a.y + a.h - 0.5;
}

} // namespace

bool Shell::attach_test_pad()
{
    if (pad_ != nullptr && virtual_joy_ != nullptr) {
        gamepad_nav_ = true;
        return true;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                       (1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                       (1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) | (1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) |
                       (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) | (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                     (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                     (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    desc.name = "Lunduke City 2 Pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        ran_ok_ = false;
        smoke_error_ = "virtual gamepad did not attach";
        return false;
    }
    if (pad_ == nullptr) {
        pad_ = SDL_OpenGamepad(id);
    }
    if (pad_ == nullptr) {
        ran_ok_ = false;
        smoke_error_ = "virtual gamepad did not open";
        return false;
    }
    virtual_joy_ = SDL_GetGamepadJoystick(pad_);
    gamepad_nav_ = true;
    return virtual_joy_ != nullptr;
}

void Shell::pump_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            request_with_save(AfterSave::Quit);
        } else {
            handle_event(event);
        }
    }
}

void Shell::release_pad_drag()
{
    map_.release();
    pad_dragging_ = false;
    pointer_on_map_ = false;
    pointer_down_ = false;
    pad_repeat_ms_ = 0;
}

void Shell::forget_gamepad(SDL_JoystickID id)
{
    if (pad_ == nullptr || id == 0 || SDL_GetGamepadID(pad_) != id) {
        return;
    }
    if (virtual_joy_ != nullptr && SDL_GetGamepadJoystick(pad_) == virtual_joy_) {
        virtual_joy_ = nullptr;
    }
    SDL_CloseGamepad(pad_);
    pad_ = nullptr;
    stick_x_ = 0;
    stick_y_ = 0;
    pan_x_ = 0;
    pan_y_ = 0;
    trig_l_ = 0;
    trig_r_ = 0;
    release_pad_drag();
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (ids[i] == id) {
                continue;
            }
            pad_ = SDL_OpenGamepad(ids[i]);
            if (pad_ != nullptr) {
                break;
            }
        }
        SDL_free(ids);
    }
    if (pad_ != nullptr) {
        return;
    }
    if (gamepad_nav_) {
        set_speed(0);
        shown_message_ = "Controller disconnected";
        shown_message_serial_ = session_.message_serial();
        hint_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
}

void Shell::gamepad_use_cursor(bool press)
{
    if (!press) {
        release_pad_drag();
        return;
    }
    pointer_x_ = cursor_x_;
    pointer_y_ = cursor_y_;
    const int id = hit(pointer_x_, pointer_y_);
    hover_id_ = id;
    if (id == kMapId && dialog_ == Dialog::None && !menu_.open()) {
        pointer_down_ = true;
        pointer_on_map_ = true;
        pad_repeat_ms_ = 0;
        pad_dragging_ = map_.press(pointer_x_ - view_x_ + scroll_x_, pointer_y_ - view_y_ + scroll_y_);
        if (!pad_dragging_) {
            pointer_on_map_ = false;
            pointer_down_ = false;
        }
        return;
    }
    if (id == kPaletteId && dialog_ == Dialog::None && !menu_.open()) {
        palette_.press(pointer_x_ - side_x_, pointer_y_ - pal_y_);
        return;
    }
    if (id == kMinimapId && dialog_ == Dialog::None && !menu_.open()) {
        minimap_.press(pointer_x_ - side_x_, pointer_y_ - mini_y_, side_w_, mini_h_);
        return;
    }
    on_hotspot(id, pointer_x_, pointer_y_);
}

void Shell::note_ui_frame(cairo_surface_t *surface)
{
    if (!ui_test_ || !ran_ok_) {
        return;
    }
    if (ui_step_ == 1 && menu_.open() && dialog_ == Dialog::None) {
        const int column = ui_column_;
        if (menu_.open_menu != column) {
            ran_ok_ = false;
            smoke_error_ = "menu column " + std::to_string(column) + " was not the open menu";
            quitting_ = true;
            return;
        }
        const MenuEntry *items = menu_table::column_items(column);
        const int count = menu_table::column_count(column);
        for (int i = 0; i < count; ++i) {
            if (items[i].kind == ItemKind::Separator) {
                continue;
            }
            const int id = kMenuItem + column * 40 + i;
            const Hotspot *hot = find_hot(hot_, id, 0);
            if (hot == nullptr) {
                ran_ok_ = false;
                smoke_error_ = "menu row missing: column " + std::to_string(column) + " index " + std::to_string(i);
                quitting_ = true;
                return;
            }
            if (hot->y + hot->h > win_h_ + 1 || hot->x + hot->w > win_w_ + 1) {
                ran_ok_ = false;
                smoke_error_ = "menu row clipped: column " + std::to_string(column) + " index " + std::to_string(i);
                quitting_ = true;
                return;
            }
            const int sx = static_cast<int>(hot->x + 40);
            const int sy = static_cast<int>(hot->y + 4);
            int r = 0;
            int g = 0;
            int b = 0;
            if (!channel_at(surface, sx, sy, r, g, b) || r <= 170 || g <= 170 || b <= 170) {
                ran_ok_ = false;
                smoke_error_ = "menu row drawn under the map: column " + std::to_string(column) + " index " +
                               std::to_string(i) + " pixel " + std::to_string(r) + "," + std::to_string(g) + "," +
                               std::to_string(b);
                quitting_ = true;
                return;
            }
            if (hit(sx, sy) != id) {
                ran_ok_ = false;
                smoke_error_ = "menu row not topmost hit: column " + std::to_string(column) + " index " +
                               std::to_string(i) + " hit " + std::to_string(hit(sx, sy));
                quitting_ = true;
                return;
            }
            ++g_menu_rows;
        }
        g_menu_seen = true;
    }
    if (ui_step_ == 6 && dialog_ == Dialog::Graphs) {
        const Hotspot *close = find_hot(hot_, kClose, menu_h_ + 40);
        if (close == nullptr) {
            ran_ok_ = false;
            smoke_error_ = "graphs close button missing";
            quitting_ = true;
            return;
        }
        const int sx = static_cast<int>(close->x + 8);
        const int sy = static_cast<int>(close->y - 6);
        int r = 0;
        int g = 0;
        int b = 0;
        if (!channel_at(surface, sx, sy, r, g, b) || r < 160 || g < 160 || b < 160) {
            ran_ok_ = false;
            smoke_error_ = "graphs legend overlaps Close (" + std::to_string(r) + "," + std::to_string(g) + "," +
                           std::to_string(b) + ")";
            quitting_ = true;
        }
    }
    if (ui_step_ == 18 && dialog_ == Dialog::Unsaved) {
        auto sample = [&](const Hotspot *hot, int &r, int &g, int &b) {
            // Top-left padding, so the sample is the button fill and not the label.
            return hot != nullptr &&
                   channel_at(surface, static_cast<int>(hot->x + 8), static_cast<int>(hot->y + 4), r, g, b);
        };
        const Hotspot *cancel = find_hot(hot_, kCancel, 0);
        const Hotspot *save = find_hot(hot_, kSave, 0);
        const Hotspot *dismiss = find_hot(hot_, kWelcomeId, 0);
        int cr = 0;
        int cg = 0;
        int cb = 0;
        int sr = 0;
        int sg = 0;
        int sb = 0;
        int dr = 0;
        int dg = 0;
        int db = 0;
        if (!sample(cancel, cr, cg, cb) || !sample(save, sr, sg, sb) || !sample(dismiss, dr, dg, db)) {
            ran_ok_ = false;
            smoke_error_ = "save dialog is missing Cancel, Save, or Dismiss";
            quitting_ = true;
            return;
        }
        // Map hover must not turn Cancel blue, and Save's focus must not turn
        // the welcome Dismiss blue. Save itself stays the focused blue.
        if (cb > cr + 20 || cr < 180) {
            ran_ok_ = false;
            smoke_error_ = "Cancel lit up while the pointer was over the map (" + std::to_string(cr) + "," +
                           std::to_string(cg) + "," + std::to_string(cb) + ")";
            quitting_ = true;
            return;
        }
        if (sb < sr + 20) {
            ran_ok_ = false;
            smoke_error_ = "Save is not the focused button";
            quitting_ = true;
            return;
        }
        if (db > dr + 20) {
            ran_ok_ = false;
            smoke_error_ = "Dismiss lit up behind the save dialog (" + std::to_string(dr) + "," + std::to_string(dg) +
                           "," + std::to_string(db) + ")";
            quitting_ = true;
        }
    }
}

void Shell::drive_ui_test()
{
    if (!ran_ok_ || quitting_) {
        return;
    }
    auto fail = [&](const std::string &message) {
        ran_ok_ = false;
        smoke_error_ = message;
        quitting_ = true;
    };
    auto tap = [&](SDL_GamepadButton button, const auto &unchanged) {
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, button, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (unchanged()) {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = static_cast<Uint8>(button);
            event.gbutton.down = true;
            handle_event(event);
        }
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, button, false);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            up.gbutton.down = false;
            handle_event(up);
        }
    };
    auto click_at = [&](double x, double y) {
        int ww = 1;
        int wh = 1;
        int pw = 1;
        int ph = 1;
        SDL_GetWindowSize(window_, &ww, &wh);
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        event.button.clicks = 1;
        event.button.x = static_cast<float>(x * ww / std::max(pw, 1));
        event.button.y = static_cast<float>(y * wh / std::max(ph, 1));
        handle_event(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.down = false;
        handle_event(event);
    };
    auto key_down = [&](SDL_Keycode key) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = key;
        event.key.down = true;
        event.key.mod = SDL_KMOD_NONE;
        handle_event(event);
    };
    auto open_column = [&](int column) {
        menu_.open_menu = column;
        menu_.cursor = 0;
        menu_.recent_open = false;
        menu_.alt_shown = true;
        menu_.activated = Action::None;
    };

    switch (ui_step_) {
    case 0:
        if (!attach_test_pad()) {
            quitting_ = true;
            return;
        }
        g_menu_rows = 0;
        g_menu_seen = false;
        ui_column_ = 0;
        open_column(0);
        ui_step_ = 1;
        return;
    case 1: {
        if (!g_menu_seen) {
            fail("open menu was not drawn");
            return;
        }
        if (ui_column_ == 0) {
            const int tool = tool_index_;
            tap(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, [&] { return tool_index_ == tool; });
            if (tool_index_ != tool || !menu_.open()) {
                fail("gamepad shoulder reached the toolbar while a menu was open");
                return;
            }
        }
        g_menu_seen = false;
        ++ui_column_;
        if (ui_column_ < menu_table::kColumnCount) {
            open_column(ui_column_);
            return;
        }
        if (g_menu_rows != menu_table::actionable_count()) {
            fail("menu rows seen " + std::to_string(g_menu_rows) + " wanted " +
                 std::to_string(menu_table::actionable_count()));
            return;
        }
        menu_.close();
        ui_step_ = 2;
        return;
    }
    case 2:
        open_column(3);
        ui_step_ = 3;
        return;
    case 3: {
        const Hotspot *hot = find_hot(hot_, kMenuItem + 3 * 40 + 2, 0);
        if (hot == nullptr) {
            fail("Evaluation menu row has no hotspot");
            return;
        }
        click_at(hot->x + hot->w * 0.5, hot->y + hot->h * 0.5);
        if (dialog_ != Dialog::Evaluation || menu_.open()) {
            fail("mouse did not activate Evaluation");
            return;
        }
        ui_step_ = 4;
        return;
    }
    case 4:
        close_dialog();
        open_column(3);
        ui_step_ = 5;
        return;
    case 5: {
        const Hotspot *hot = find_hot(hot_, kMenuItem + 3 * 40 + 1, 0);
        if (hot == nullptr || win_w_ < 1 || win_h_ < 1) {
            fail("Graphs menu row has no hotspot");
            return;
        }
        SDL_Event finger{};
        finger.type = SDL_EVENT_FINGER_DOWN;
        finger.tfinger.fingerID = 7;
        finger.tfinger.x = static_cast<float>((hot->x + hot->w * 0.5) / win_w_);
        finger.tfinger.y = static_cast<float>((hot->y + hot->h * 0.5) / win_h_);
        handle_event(finger);
        finger.type = SDL_EVENT_FINGER_UP;
        handle_event(finger);
        if (dialog_ != Dialog::Graphs || menu_.open()) {
            fail("touch did not activate Graphs");
            return;
        }
        ui_step_ = 6;
        return;
    }
    case 6: {
        const double dw = std::min(860.0, win_w_ - 48.0);
        const double dh = std::min(640.0, win_h_ - 48.0);
        const double x = (win_w_ - dw) / 2.0;
        const double y = (win_h_ - dh) / 2.0;
        const Hotspot *close = find_hot(hot_, kClose, y);
        if (close == nullptr || close->x < x - 1 || close->x + close->w > x + dw + 1 || close->y + close->h > y + dh + 1) {
            fail("graphs Close is outside the dialog");
            return;
        }
        close_dialog();
        info_body_ = "Residential zone - population 120, land value 40, powered";
        open_dialog(Dialog::Query);
        ui_step_ = 7;
        return;
    }
    case 7: {
        const Hotspot *close = find_hot(hot_, kClose, 0);
        if (close == nullptr || close->x < 0 || close->y < menu_h_ || close->x + close->w > win_w_ ||
            close->y + close->h > win_h_) {
            fail("query Close is outside the window");
            return;
        }
        if (close->w > 220) {
            fail("query Close is stretched across the dialog");
            return;
        }
        close_dialog();
        gamepad_nav_ = true;
        open_dialog(Dialog::NewCity);
        ui_step_ = 8;
        return;
    }
    case 8: {
        int key_count = 0;
        osk_keys(key_count);
        std::vector<Hotspot> keys;
        for (const Hotspot &hot : hot_) {
            if (hot.id >= kOsk && hot.id < kOsk + key_count) {
                keys.push_back(hot);
            }
        }
        std::sort(keys.begin(), keys.end(), [](const Hotspot &a, const Hotspot &b) { return a.id < b.id; });
        if (static_cast<int>(keys.size()) != key_count) {
            fail("on-screen keyboard has " + std::to_string(keys.size()) + " keys");
            return;
        }
        for (int i = 0; i < key_count; ++i) {
            for (int j = i + 1; j < key_count; ++j) {
                if (rects_overlap(keys[static_cast<std::size_t>(i)], keys[static_cast<std::size_t>(j)])) {
                    fail("on-screen keyboard keys overlap");
                    return;
                }
            }
        }
        if (win_w_ >= 1200 && win_h_ >= 700) {
            for (const Hotspot &key : keys) {
                if (key.w < 47 || key.h < 47) {
                    fail("on-screen keyboard key is smaller than 48px");
                    return;
                }
            }
        }
        for (int i = 1; i < 10; ++i) {
            if (keys[static_cast<std::size_t>(i)].x <= keys[static_cast<std::size_t>(i - 1)].x) {
                fail("on-screen keyboard top row is not left to right");
                return;
            }
        }
        if (!(keys[10].y > keys[0].y + 8 && keys[10].x > keys[0].x && keys[20].y > keys[10].y + 8)) {
            fail("on-screen keyboard is not QWERTY");
            return;
        }
        const double field_w = std::min(860.0, win_w_ - 48.0);
        const double field_right = (win_w_ - field_w) / 2.0 + field_w - 16.0;
        for (const Hotspot &key : keys) {
            if (key.x + key.w > field_right + 0.75) {
                fail("on-screen keyboard key " + std::to_string(key.id) + " extends past the field");
                return;
            }
        }
        int key_n = 0;
        const OskKey *okeys = osk_keys(key_n);
        std::vector<double> center(static_cast<std::size_t>(key_n), 0);
        std::vector<char> present(static_cast<std::size_t>(key_n), 0);
        for (const Hotspot &key : keys) {
            const int index = key.id - kOsk;
            if (index < 0 || index >= key_n) {
                continue;
            }
            center[static_cast<std::size_t>(index)] = key.x + key.w * 0.5;
            present[static_cast<std::size_t>(index)] = 1;
        }
        if (osk_vertical_target(okeys, key_n, 0, 1, center.data(), present.data()) != 10 ||
            osk_vertical_target(okeys, key_n, 9, 1, center.data(), present.data()) != 18) {
            fail("OSK rows do not line up q-above-a and p-above-l");
            return;
        }
        focus_id_ = kOsk;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk; });
        if (focus_id_ != kOsk + 10) {
            fail("D-pad Down from q went to " + std::to_string(focus_id_) + " instead of a");
            return;
        }
        focus_id_ = kOsk + 9;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk + 9; });
        if (focus_id_ != kOsk + 18) {
            fail("D-pad Down from p went to " + std::to_string(focus_id_) + " instead of l");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kOsk + 18; });
        if (focus_id_ != kOsk + 9) {
            fail("D-pad Up from l went to " + std::to_string(focus_id_) + " instead of p");
            return;
        }
        focus_id_ = kOsk;
        tap(SDL_GAMEPAD_BUTTON_DPAD_UP, [&] { return focus_id_ == kOsk; });
        if (focus_id_ != kFieldName) {
            fail("D-pad Up from the top OSK row did not return to the text field");
            return;
        }
        focus_id_ = kOsk + key_n - 1;
        tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == kOsk + key_n - 1; });
        if (focus_id_ != kCancel) {
            fail("D-pad Down from the last OSK row did not reach Cancel");
            return;
        }
        close_dialog();
        ui_step_ = 9;
        return;
    }
    case 9: {
        const bool was_open = menu_.open();
        tap(SDL_GAMEPAD_BUTTON_START, [&] { return menu_.open() == was_open; });
        if (!menu_.open() || menu_.open_menu != 0) {
            fail("gamepad Start did not open the System menu");
            return;
        }
        ui_step_ = 10;
        return;
    }
    case 10:
        for (int i = 0; i < 3; ++i) {
            const int column = menu_.open_menu;
            tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, [&] { return menu_.open_menu == column; });
        }
        if (menu_.open_menu != 3) {
            fail("gamepad could not reach the Windows menu");
            return;
        }
        tap(SDL_GAMEPAD_BUTTON_SOUTH, [&] { return dialog_ == Dialog::None; });
        if (dialog_ != Dialog::Budget) {
            fail("gamepad did not activate Budget");
            return;
        }
        ui_step_ = 11;
        return;
    case 11: {
        if (focus_id_ != kSlider + 1 || sliders_.empty()) {
            fail("budget did not focus the tax slider");
            return;
        }
        const int tax0 = tax_;
        const int road0 = road_;
        const int focus0 = focus_id_;
        tap(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, [&] { return tax_ == tax0 && focus_id_ == focus0; });
        if (tax_ != tax0 + 1 || focus_id_ != kSlider + 1) {
            fail("gamepad Right changed focus instead of the tax slider");
            return;
        }
        key_down(SDLK_LEFT);
        if (tax_ != tax0 || focus_id_ != kSlider + 1) {
            fail("keyboard Left did not adjust the focused slider");
            return;
        }
        key_down(SDLK_DOWN);
        if (focus_id_ != kSlider + 2) {
            fail("Down did not move focus off the tax slider");
            return;
        }
        key_down(SDLK_LEFT);
        if (road_ != road0 - 5 || focus_id_ != kSlider + 2) {
            fail("Left did not adjust the road slider");
            return;
        }
        key_down(SDLK_UP);
        if (focus_id_ != kSlider + 1) {
            fail("Up did not move focus back to the tax slider");
            return;
        }
        close_dialog();
        ui_step_ = 17;
        return;
    }
    case 17:
        welcome_ = true;
        open_dialog(Dialog::Unsaved);
        // Stale map hover. Cancel used to share this id and light up.
        hover_id_ = kMapId;
        ui_step_ = 18;
        return;
    case 18: {
        if (dialog_ != Dialog::Unsaved) {
            fail("save dialog did not open");
            return;
        }
        if (kCancel == kMapId || kOk == kPaletteId || kDiscard == kMinimapId || kSave == kWelcomeId ||
            kClose == kMenuTitle) {
            fail("dialog button id still matches a screen region");
            return;
        }
        std::vector<int> seen;
        for (const Hotspot &hot : hot_) {
            if (hot.id == kHotNone) {
                continue;
            }
            if (std::find(seen.begin(), seen.end(), hot.id) != seen.end()) {
                fail("duplicate hotspot id " + std::to_string(hot.id));
                return;
            }
            seen.push_back(hot.id);
        }
        bool saw_cancel = false;
        bool saw_discard = false;
        bool saw_save = false;
        for (int id : focus_order_) {
            if (!is_modal_control(id)) {
                fail("modal focus includes background id " + std::to_string(id));
                return;
            }
            saw_cancel = saw_cancel || id == kCancel;
            saw_discard = saw_discard || id == kDiscard;
            saw_save = saw_save || id == kSave;
        }
        if (!saw_cancel || !saw_discard || !saw_save) {
            fail("save dialog is missing a button in the focus cycle");
            return;
        }
        if (std::find(focus_order_.begin(), focus_order_.end(), kWelcomeId) != focus_order_.end() ||
            std::find(focus_order_.begin(), focus_order_.end(), kMapId) != focus_order_.end() ||
            std::find(focus_order_.begin(), focus_order_.end(), kMenuTitle) != focus_order_.end()) {
            fail("background hotspot joined the modal focus cycle");
            return;
        }
        const int steps = static_cast<int>(focus_order_.size()) + 1;
        for (int i = 0; i < steps; ++i) {
            const int before = focus_id_;
            tap(SDL_GAMEPAD_BUTTON_DPAD_DOWN, [&] { return focus_id_ == before; });
            if (!is_modal_control(focus_id_)) {
                fail("D-pad left the dialog onto id " + std::to_string(focus_id_));
                return;
            }
        }
        close_dialog();
        welcome_ = false;
        ui_step_ = 12;
        return;
    }
    case 12: {
        open_column(0);
        ui_step_ = 13;
        return;
    }
    case 13: {
        const std::int64_t funds = session_.funds();
        const int tool = tool_index_;
        const double side_x = side_x_ + 8;
        const double side_y = menu_h_ + 420.0;
        const double map_x = static_cast<double>(win_w_ - 40);
        const double map_y = view_y_ + view_h_ * 0.5;
        if (!menu_.open() || hit(side_x, side_y) != 0 || hit(map_x, map_y) != 0) {
            fail("open menu did not capture the map and toolbar");
            return;
        }
        click_at(side_x, side_y);
        if (menu_.open() || dialog_ != Dialog::None || tool_index_ != tool || session_.funds() != funds) {
            fail("a toolbar click while the menu was open fell through");
            return;
        }
        open_column(0);
        ui_step_ = 16;
        return;
    }
    case 16: {
        const std::int64_t funds = session_.funds();
        const int tool = tool_index_;
        const double map_x = static_cast<double>(win_w_ - 40);
        const double map_y = view_y_ + view_h_ * 0.5;
        if (!menu_.open() || hit(map_x, map_y) != 0) {
            fail("reopened menu did not capture the map");
            return;
        }
        click_at(map_x, map_y);
        if (menu_.open() || dialog_ != Dialog::None || tool_index_ != tool || session_.funds() != funds) {
            fail("a map click while the menu was open fell through");
            return;
        }
        ui_step_ = 14;
        return;
    }
    case 14: {
        select_tool(1);
        int tx = CitySession::kWorldW / 2;
        int ty = CitySession::kWorldH / 2;
        bool found = false;
        for (int radius = 0; radius < 45 && !found; ++radius) {
            for (int dy = -radius; dy <= radius && !found; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int x = CitySession::kWorldW / 2 + dx;
                    const int y = CitySession::kWorldH / 2 + dy;
                    if (x < 2 || y < 2 || x + 6 >= CitySession::kWorldW || y + 2 >= CitySession::kWorldH) {
                        continue;
                    }
                    bool dry = true;
                    for (int i = 0; i < 6; ++i) {
                        if (session_.layer_value(CitySession::MapLayer::Water, x + i, y) != 0) {
                            dry = false;
                            break;
                        }
                    }
                    if (dry) {
                        tx = x;
                        ty = y;
                        found = true;
                    }
                }
            }
        }
        if (!found) {
            fail("no dry land for the drag-build");
            return;
        }
        center_on_fraction((tx + 0.5) / CitySession::kWorldW, (ty + 0.5) / CitySession::kWorldH);
        cursor_x_ = static_cast<float>(view_x_ + view_w_ / 2.0);
        cursor_y_ = static_cast<float>(view_y_ + view_h_ / 2.0);
        if (hit(cursor_x_, cursor_y_) != kMapId) {
            fail("gamepad cursor is not on the map");
            return;
        }
        ui_map_before_.assign(static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH), 0);
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)] =
                    session_.map_value(x, y) & 0x03ff;
            }
        }
        stick_x_ = 0;
        stick_y_ = 0;
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_SOUTH, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (!pad_dragging_) {
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            event.gbutton.down = true;
            handle_event(event);
        }
        if (!pad_dragging_) {
            fail("A did not press the map");
            return;
        }
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualAxis(virtual_joy_, SDL_GAMEPAD_AXIS_LEFTX, 30000);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (stick_x_ < 0.5f) {
            SDL_Event axis{};
            axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
            axis.gaxis.value = 30000;
            handle_event(axis);
        }
        if (stick_x_ < 0.5f) {
            fail("stick did not deflect");
            return;
        }
        ui_drag_frames_ = 0;
        ui_step_ = 15;
        return;
    }
    case 15: {
        if (!pad_dragging_) {
            fail("A released the drag before the button came up");
            return;
        }
        ++ui_drag_frames_;
        if (ui_drag_frames_ < 28) {
            return;
        }
        stick_x_ = 0;
        stick_y_ = 0;
        if (virtual_joy_ != nullptr) {
            SDL_SetJoystickVirtualAxis(virtual_joy_, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
            SDL_SetJoystickVirtualButton(virtual_joy_, SDL_GAMEPAD_BUTTON_SOUTH, false);
            SDL_UpdateJoysticks();
            pump_events();
        }
        if (pad_dragging_) {
            SDL_Event up{};
            up.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            up.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
            up.gbutton.down = false;
            handle_event(up);
        }
        if (pad_dragging_) {
            fail("A up did not release the drag");
            return;
        }
        int changed = 0;
        for (int y = 0; y < CitySession::kWorldH; ++y) {
            for (int x = 0; x < CitySession::kWorldW; ++x) {
                const int now = session_.map_value(x, y) & 0x03ff;
                if (now != ui_map_before_[static_cast<std::size_t>(y * CitySession::kWorldW + x)]) {
                    ++changed;
                }
            }
        }
        if (changed < 3) {
            fail("holding A and moving did not drag-build (" + std::to_string(changed) + " tiles)");
            return;
        }
        quitting_ = true;
        return;
    }
    default:
        quitting_ = true;
        return;
    }
}

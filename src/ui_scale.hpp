#pragma once
// Display-fit and UI scale. Header-only so the shell and the unit tests share
// one implementation. SDL is not included: the shell passes in the display
// scale and the usable bounds.

#include "window_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace ui_scale {

struct Placement {
    int x = 0;
    int y = 0;
    int w = 1280;
    int h = 800;
};

// 0 means "follow the display". A positive value is a user override.
// The result is the factor applied to fonts and layout metrics drawn in
// pixels. SDL_GetWindowDisplayScale is already relative to the pixel size,
// so this is not multiplied by the pixel density again.
inline float resolve_ui_scale(float display_scale, float user_override)
{
    const bool user = user_override > 0.05f;
    float scale = user ? user_override : display_scale;
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    if (scale < 0.5f) {
        scale = 0.5f;
    }
    if (scale > 4.0f) {
        scale = 4.0f;
    }
    return scale;
}

inline int scaled_px(int px, float scale)
{
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(px) * static_cast<double>(scale))));
}

inline std::string scaled_font(const char *family, int points, float scale)
{
    return std::string(family) + " " + std::to_string(scaled_px(points, scale));
}

// Classic 1280x800 when the work area can hold it. A larger display grows
// the window to about 80% of the usable client. A smaller display (a
// 1366x768 laptop) shrinks to that same 80% so the frame stays on screen.
inline Placement fit_default_window(int preferred_w, int preferred_h, int ax, int ay, int aw, int ah)
{
    const int avail_w = std::max(1, aw - 2 * window_fit::kFrameSide);
    const int avail_h = std::max(1, ah - window_fit::kFrameTop - window_fit::kFrameBottom);
    const int pct_w = std::max(1, avail_w * 80 / 100);
    const int pct_h = std::max(1, avail_h * 80 / 100);
    Placement place;
    if (preferred_w <= avail_w && preferred_h <= avail_h) {
        place.w = std::min(avail_w, std::max(preferred_w, pct_w));
        place.h = std::min(avail_h, std::max(preferred_h, pct_h));
    } else {
        place.w = std::min(avail_w, pct_w);
        place.h = std::min(avail_h, pct_h);
    }
    place.x = ax + std::max(0, (aw - place.w) / 2);
    place.y = ay + std::max(0, (ah - place.h) / 2);
    window_fit::clamp_origin(place.x, place.y, place.w, place.h, ax, ay, aw, ah);
    return place;
}

// A remembered window must come back inside the work area it is opened on.
inline Placement clamp_saved_window(int x, int y, int w, int h, int ax, int ay, int aw, int ah)
{
    const int avail_w = std::max(1, aw - 2 * window_fit::kFrameSide);
    const int avail_h = std::max(1, ah - window_fit::kFrameTop - window_fit::kFrameBottom);
    Placement place;
    place.w = std::max(1, std::min(w, avail_w));
    place.h = std::max(1, std::min(h, avail_h));
    place.x = x;
    place.y = y;
    window_fit::clamp_origin(place.x, place.y, place.w, place.h, ax, ay, aw, ah);
    return place;
}

struct StoredSettings {
    bool has_geometry = false;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool maximized = false;
    float user_scale = 0.0f;
    // -1 means the key was not in the file.
    int speed = -1;
    int sound = -1;
    int volume = -1;
    int auto_budget = -1;
    int auto_bulldoze = -1;
    int disasters = -1;
    int auto_goto = -1;
    int pause_unfocused = -1;
};

inline std::string settings_text(const StoredSettings &settings)
{
    std::ostringstream out;
    out << "ui_scale=" << settings.user_scale << '\n';
    if (settings.has_geometry) {
        out << "window_x=" << settings.x << '\n';
        out << "window_y=" << settings.y << '\n';
        out << "window_w=" << settings.w << '\n';
        out << "window_h=" << settings.h << '\n';
        out << "maximized=" << (settings.maximized ? 1 : 0) << '\n';
    }
    auto put = [&](const char *key, int value) {
        if (value >= 0) {
            out << key << '=' << value << '\n';
        }
    };
    put("speed", settings.speed);
    put("sound", settings.sound);
    put("volume", settings.volume);
    put("auto_budget", settings.auto_budget);
    put("auto_bulldoze", settings.auto_bulldoze);
    put("disasters", settings.disasters);
    put("auto_goto", settings.auto_goto);
    put("pause_unfocused", settings.pause_unfocused);
    return out.str();
}

// A Steam Deck session is fullscreen and must not replace the desktop
// window rectangle. When the file already has one, write that rectangle
// back. When it does not, leave the keys out rather than storing the
// Deck window.
inline void keep_desktop_geometry(StoredSettings &stored, const StoredSettings &loaded, bool deck, bool live_valid, int x,
                                  int y, int w, int h, bool maximized)
{
    if (deck) {
        stored.has_geometry = loaded.has_geometry;
        stored.x = loaded.x;
        stored.y = loaded.y;
        stored.w = loaded.w;
        stored.h = loaded.h;
        stored.maximized = loaded.maximized;
        return;
    }
    stored.has_geometry = live_valid;
    stored.x = x;
    stored.y = y;
    stored.w = w;
    stored.h = h;
    stored.maximized = maximized;
}

// Unknown keys are ignored so later settings can share the file.
inline StoredSettings settings_parse(const std::string &text)
{
    StoredSettings settings;
    bool saw_x = false;
    bool saw_y = false;
    bool saw_w = false;
    bool saw_h = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        auto as_int = [&](int &dest, bool &saw) {
            char *end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (end == value.c_str()) {
                return;
            }
            dest = static_cast<int>(parsed);
            saw = true;
        };
        if (key == "ui_scale") {
            char *end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && parsed >= 0.0f) {
                settings.user_scale = parsed;
            }
        } else if (key == "window_x") {
            as_int(settings.x, saw_x);
        } else if (key == "window_y") {
            as_int(settings.y, saw_y);
        } else if (key == "window_w") {
            as_int(settings.w, saw_w);
        } else if (key == "window_h") {
            as_int(settings.h, saw_h);
        } else if (key == "maximized") {
            int flag = 0;
            bool saw = false;
            as_int(flag, saw);
            if (saw) {
                settings.maximized = flag != 0;
            }
        } else if (key == "speed") {
            bool saw = false;
            as_int(settings.speed, saw);
        } else if (key == "sound") {
            bool saw = false;
            as_int(settings.sound, saw);
        } else if (key == "volume") {
            bool saw = false;
            as_int(settings.volume, saw);
        } else if (key == "auto_budget") {
            bool saw = false;
            as_int(settings.auto_budget, saw);
        } else if (key == "auto_bulldoze") {
            bool saw = false;
            as_int(settings.auto_bulldoze, saw);
        } else if (key == "disasters") {
            bool saw = false;
            as_int(settings.disasters, saw);
        } else if (key == "auto_goto") {
            bool saw = false;
            as_int(settings.auto_goto, saw);
        } else if (key == "pause_unfocused") {
            bool saw = false;
            as_int(settings.pause_unfocused, saw);
        }
    }
    if (saw_w && saw_h && settings.w >= 320 && settings.h >= 240) {
        settings.has_geometry = true;
        if (!saw_x) {
            settings.x = 0;
        }
        if (!saw_y) {
            settings.y = 0;
        }
    }
    return settings;
}

// The window may not be dragged smaller than 960×600, unless the display
// itself is smaller. The floor stays large enough to show the side panel.
inline void window_minimum(int usable_w, int usable_h, int &min_w, int &min_h)
{
    min_w = 960;
    min_h = 600;
    if (usable_w > 0 && usable_w < min_w) {
        min_w = std::max(320, usable_w);
    }
    if (usable_h > 0 && usable_h < min_h) {
        min_h = std::max(240, usable_h);
    }
}

} // namespace ui_scale

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The toolbar ships original 40px sprites. They have to be found from the
// source tree and from an installed share/ directory, and they have to be
// more than a handful of flat blocks. The fit math fills a Deck button,
// shrinks only below UI scale 1, and otherwise uses a whole multiple.

#include "assets.hpp"
#include "tool_palette.hpp"
#include "ui_scale.hpp"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

bool is_opaque(const unsigned char *pixel)
{
    return pixel[3] > 200;
}

// Cairo ARGB32 is native-endian B, G, R, A. The road dashes are yellow.
bool is_yellow(const unsigned char *pixel)
{
    return pixel[3] > 200 && pixel[2] > 200 && pixel[1] > 140 && pixel[0] < 80;
}

const char *kNames[] = {
    "bulldozer", "road",    "rail",    "wire",     "park",      "residential", "commercial", "industrial",
    "police",    "fire",    "stadium", "seaport",  "coal",      "nuclear",     "airport",    "special",
    "query",
};

const char *kMenuIcons[] = {"zoo", "bigpark", "library"};
const char *kMapTiles[] = {"zoo", "big-park", "library"};

} // namespace

int main()
{
    for (const char *name : kNames) {
        const std::string path = ui_data_file(std::string("tool-icons/") + name + ".png");
        if (path.empty()) {
            return fail(std::string("missing toolbar icon ") + name);
        }
        cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
        if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            return fail(std::string("could not read ") + path);
        }
        if (cairo_image_surface_get_width(surface) != 40 || cairo_image_surface_get_height(surface) != 40) {
            cairo_surface_destroy(surface);
            return fail(std::string(name) + " is not a 40px sprite");
        }
        cairo_surface_flush(surface);
        const unsigned char *data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        int opaque = 0;
        int transparent = 0;
        int colors = 0;
        unsigned seen[64] = {};
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                const unsigned char *px = data + y * stride + x * 4;
                if (px[3] < 16) {
                    ++transparent;
                    continue;
                }
                ++opaque;
                const unsigned key = (static_cast<unsigned>(px[2]) << 16) | (static_cast<unsigned>(px[1]) << 8) | px[0];
                bool known = false;
                for (int i = 0; i < colors; ++i) {
                    if (seen[i] == key) {
                        known = true;
                        break;
                    }
                }
                if (!known && colors < 64) {
                    seen[colors++] = key;
                }
            }
        }
        cairo_surface_destroy(surface);
        // A glyph keeps a transparent margin. A full-bleed scene (the airport)
        // covers the canvas instead, and still has to be more than one flat color.
        if (opaque < 400 || colors < 4 || (transparent < 8 && opaque < 1200)) {
            return fail(std::string(name) + " does not have a drawn silhouette");
        }
    }
    const double deck = icon_pixel_scale(40, 40);
    const double half = icon_pixel_scale(40, 16);
    const double grown = icon_pixel_scale(40, 64);
    const double doubled = icon_pixel_scale(40, 88);
    if (std::abs(deck - 1.0) > 0.01) {
        return fail("a 40px sprite does not fill a 40px button");
    }
    if (std::abs(half - 0.5) > 0.01) {
        return fail("a slot smaller than the sprite must use a crisp half scale");
    }
    // 64px is not a whole multiple of 40. Stretching would look lumpy, and
    // shrinking below the 1x sprite is worse than leaving a margin.
    if (std::abs(grown - 1.0) > 0.01) {
        return fail("a slot between 1x and 2x must stay at 1x");
    }
    if (std::abs(doubled - 2.0) > 0.01) {
        return fail("a double-size button did not use a 2x nearest scale");
    }
    if (std::abs(icon_pixel_scale(40, 80) - 2.0) > 0.01) {
        return fail("an 80px slot did not use a 2x nearest scale");
    }
    const int cell_1x = ui_scale::toolbar_cell(1.0f, 800);
    const int cell_2x = ui_scale::toolbar_cell(2.0f, 4000);
    if (cell_1x - 8 < 40 || std::abs(icon_pixel_scale(40, cell_1x - 8) - 1.0) > 0.01) {
        return fail("UI scale 1 drew the toolbar icons smaller than 40px");
    }
    if (cell_2x - 8 < 80 || std::abs(icon_pixel_scale(40, cell_2x - 8) - 2.0) > 0.01) {
        return fail("a large UI scale 2 window did not step the icons up to 2x");
    }
    const float fitted = ui_scale::clamp_ui_scale(2.0f, 1280, 800);
    const int fitted_body = 800 - ui_scale::scaled_px(36, fitted) - ui_scale::scaled_px(36, fitted) -
                            ui_scale::scaled_px(44, fitted);
    const int fitted_slot = ui_scale::toolbar_cell(fitted, fitted_body) - 8;
    const double fitted_scale = icon_pixel_scale(40, fitted_slot);
    if (fitted_slot < 40 || fitted_scale < 1.0 || std::abs(fitted_scale - std::floor(fitted_scale + 0.001)) > 0.01) {
        return fail("raising UI scale on 1280x800 shrank or fractionally scaled the icons");
    }
    if (std::abs(ui_scale::demand_label_px(0.5f) - 7.0) > 0.01 ||
        std::abs(ui_scale::demand_label_px(1.0f) - 14.0) > 0.01 ||
        std::abs(ui_scale::demand_label_px(2.0f) - 28.0) > 0.01) {
        return fail("R/C/I labels do not follow the UI scale");
    }
    cairo_surface_t *fallback = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 40, 40);
    cairo_t *cr = cairo_create(fallback);
    paint_tool_fallback(cr, 3, 0, 0, 40);
    cairo_destroy(cr);
    cairo_surface_flush(fallback);
    const unsigned char *px = cairo_image_surface_get_data(fallback);
    const int stride = cairo_image_surface_get_stride(fallback);
    int varied = 0;
    unsigned first = 0;
    bool have = false;
    for (int y = 4; y < 36; ++y) {
        for (int x = 4; x < 36; ++x) {
            const unsigned char *p = px + y * stride + x * 4;
            const unsigned key = (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[1]) << 8) | p[0];
            if (!have) {
                first = key;
                have = true;
            } else if (key != first) {
                ++varied;
            }
        }
    }
    cairo_surface_destroy(fallback);
    if (!have || varied < 8) {
        return fail("a missing icon leaves a blank button");
    }
    if (tool_icon_fallback_label(3)[0] == '\0' || tool_icon_fallback_label(16)[0] != '?') {
        return fail("missing-icon labels are empty");
    }
    auto load_png = [&](const std::string &rel) -> cairo_surface_t * {
        const std::string path = ui_data_file(rel);
        if (path.empty()) {
            return nullptr;
        }
        cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
        if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            return nullptr;
        }
        return surface;
    };
    for (const char *name : kMenuIcons) {
        cairo_surface_t *surface = load_png(std::string("tool-icons/") + name + ".png");
        if (surface == nullptr || cairo_image_surface_get_width(surface) != 40 ||
            cairo_image_surface_get_height(surface) != 40) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            return fail(std::string("missing special-building icon ") + name);
        }
        cairo_surface_destroy(surface);
    }
    for (const char *name : kMapTiles) {
        cairo_surface_t *surface = load_png(std::string("special-tiles/") + name + ".png");
        if (surface == nullptr || cairo_image_surface_get_width(surface) != 48 ||
            cairo_image_surface_get_height(surface) != 48) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            return fail(std::string("missing special-building map tile ") + name);
        }
        cairo_surface_destroy(surface);
    }
    cairo_surface_t *coal = load_png("tool-icons/coal.png");
    cairo_surface_t *nuclear = load_png("tool-icons/nuclear.png");
    if (coal == nullptr || nuclear == nullptr) {
        if (coal != nullptr) {
            cairo_surface_destroy(coal);
        }
        if (nuclear != nullptr) {
            cairo_surface_destroy(nuclear);
        }
        return fail("coal and nuclear icons did not load together");
    }
    cairo_surface_flush(coal);
    cairo_surface_flush(nuclear);
    const unsigned char *coal_px = cairo_image_surface_get_data(coal);
    const unsigned char *nuclear_px = cairo_image_surface_get_data(nuclear);
    const int coal_stride = cairo_image_surface_get_stride(coal);
    const int nuclear_stride = cairo_image_surface_get_stride(nuclear);
    bool hall_matches = true;
    for (int y = 0; y < 40 && hall_matches; ++y) {
        for (int x = 0; x < 22; ++x) {
            const unsigned char *a = coal_px + y * coal_stride + x * 4;
            const unsigned char *b = nuclear_px + y * nuclear_stride + x * 4;
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) {
                hall_matches = false;
                break;
            }
        }
    }
    cairo_surface_destroy(coal);
    cairo_surface_destroy(nuclear);
    if (!hall_matches) {
        return fail("coal and nuclear do not share the same power hall");
    }
    cairo_surface_t *road = load_png("tool-icons/road.png");
    cairo_surface_t *rail = load_png("tool-icons/rail.png");
    if (road == nullptr || rail == nullptr) {
        if (road != nullptr) {
            cairo_surface_destroy(road);
        }
        if (rail != nullptr) {
            cairo_surface_destroy(rail);
        }
        return fail("road and rail icons did not load together");
    }
    cairo_surface_flush(road);
    cairo_surface_flush(rail);
    const unsigned char *road_px = cairo_image_surface_get_data(road);
    const unsigned char *rail_px = cairo_image_surface_get_data(rail);
    const int road_stride = cairo_image_surface_get_stride(road);
    const int rail_stride = cairo_image_surface_get_stride(rail);
    auto at = [](const unsigned char *px, int stride, int x, int y) { return px + y * stride + x * 4; };
    int yellow_top = 40;
    int yellow_bot = -1;
    int yellow_col = -1;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (!is_yellow(at(road_px, road_stride, x, y))) {
                continue;
            }
            yellow_top = std::min(yellow_top, y);
            yellow_bot = std::max(yellow_bot, y);
            yellow_col = x;
        }
    }
    const int yellow_h = yellow_bot - yellow_top + 1;
    if (yellow_col < 0 || yellow_h < 1 || yellow_h > 2) {
        cairo_surface_destroy(road);
        cairo_surface_destroy(rail);
        return fail("the road centre line is not a 1px or 2px yellow band");
    }
    int road_top = 40;
    int road_bot = -1;
    for (int y = 0; y < 40; ++y) {
        if (!is_opaque(at(road_px, road_stride, yellow_col, y))) {
            continue;
        }
        road_top = std::min(road_top, y);
        road_bot = std::max(road_bot, y);
    }
    const int above = yellow_top - road_top;
    const int below = road_bot - yellow_bot;
    if (above != below || above < 1) {
        std::cerr << "road centre above " << above << " below " << below << "\n";
        cairo_surface_destroy(road);
        cairo_surface_destroy(rail);
        return fail("the road centre line is not vertically centred");
    }
    auto band = [&](const unsigned char *px, int stride, int &top, int &bot, bool &left, bool &right) {
        top = 40;
        bot = -1;
        left = false;
        right = false;
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                if (!is_opaque(at(px, stride, x, y))) {
                    continue;
                }
                top = std::min(top, y);
                bot = std::max(bot, y);
                if (x == 0) {
                    left = true;
                }
                if (x == 39) {
                    right = true;
                }
            }
        }
    };
    int road_band_top = 0;
    int road_band_bot = 0;
    int rail_band_top = 0;
    int rail_band_bot = 0;
    bool road_left = false;
    bool road_right = false;
    bool rail_left = false;
    bool rail_right = false;
    band(road_px, road_stride, road_band_top, road_band_bot, road_left, road_right);
    band(rail_px, rail_stride, rail_band_top, rail_band_bot, rail_left, rail_right);
    cairo_surface_destroy(road);
    cairo_surface_destroy(rail);
    if (!road_left || !road_right || !rail_left || !rail_right) {
        return fail("road and rail icons do not both run from left to right");
    }
    if (road_band_bot - road_band_top != rail_band_bot - rail_band_top) {
        return fail("road and rail icons are not the same width");
    }
    return 0;
}

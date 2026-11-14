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

const char *kNames[] = {
    "bulldozer", "road",    "rail",    "wire",     "park",      "residential", "commercial", "industrial",
    "police",    "fire",    "stadium", "seaport",  "coal",      "nuclear",     "airport",    "special",
    "query", "zoo", "bigpark", "library", "mayor", "casino", "amusement", "prison", "marina", "fountain",
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
        for (const char *look : {"-svga", "-spooky"}) {
            if (!ui_data_file(std::string("tool-icons/") + name + look + ".png").empty()) {
                return fail(std::string("a per-look toolbar icon is still present for ") + name);
            }
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
        // Outlined objects keep transparent space and several engraved shades.
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
    struct SheetExpect {
        const char *name;
        int size;
    };
    const SheetExpect kNewSheets[] = {
        {"prison", 64}, {"prison-svga", 64}, {"prison-spooky", 64},
        {"marina", 48}, {"marina-svga", 48}, {"marina-spooky", 48},
        {"fountain", 16}, {"fountain-svga", 16}, {"fountain-spooky", 16},
    };
    for (const SheetExpect &sheet : kNewSheets) {
        cairo_surface_t *surface = load_png(std::string("special-tiles/") + sheet.name + ".png");
        if (surface == nullptr || cairo_image_surface_get_width(surface) != sheet.size ||
            cairo_image_surface_get_height(surface) != sheet.size) {
            if (surface != nullptr) {
                cairo_surface_destroy(surface);
            }
            return fail(std::string("missing special-building map tile ") + sheet.name);
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
    // The previous set used matching halls, road/rail strips, and PD/FD letters.
    // This set uses distinct objects; the generator check covers exact pixels.
    return 0;
}

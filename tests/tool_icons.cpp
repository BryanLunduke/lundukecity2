// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The toolbar ships original 40px sprites. They have to be found from the
// source tree and from an installed share/ directory, and they have to be
// more than a handful of flat blocks. The fit math must fill a Deck button
// and shrink instead of clipping at UI scale 0.5.

#include "assets.hpp"
#include "tool_palette.hpp"

#include <cairo.h>

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
    if (!(half < 1.0) || std::abs(half * 40.0 - 16.0) > 0.01) {
        return fail("a sprite larger than the button is clipped instead of shrunk");
    }
    if (grown <= 1.0 || grown * 40.0 > 64.0 + 0.01) {
        return fail("icons stay at 1x until the button is twice as big");
    }
    if (std::abs(doubled - 2.0) > 0.01) {
        return fail("a double-size button did not use a 2x nearest scale");
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
    return 0;
}

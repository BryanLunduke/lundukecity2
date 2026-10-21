// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The toolbar ships original 24px sprites. They have to be found from the
// source tree and from an installed share/ directory, and they have to be
// more than a handful of flat blocks.

#include "assets.hpp"

#include <cairo.h>

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
    "police",    "fire",    "stadium", "seaport",  "coal",      "nuclear",     "airport",    "query",
};

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
        if (cairo_image_surface_get_width(surface) != 24 || cairo_image_surface_get_height(surface) != 24) {
            cairo_surface_destroy(surface);
            return fail(std::string(name) + " is not a 24px sprite");
        }
        cairo_surface_flush(surface);
        const unsigned char *data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        int opaque = 0;
        int transparent = 0;
        int colors = 0;
        unsigned seen[64] = {};
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 24; ++x) {
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
        if (opaque < 40 || transparent < 8 || colors < 4) {
            return fail(std::string(name) + " does not have a drawn silhouette");
        }
    }
    return 0;
}

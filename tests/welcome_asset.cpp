// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The Welcome screen uses the editor's city plate at full resolution.
// The test binary sits in build/tests and must still find data/welcome.

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

} // namespace

int main()
{
    const std::string path = ui_data_file("welcome/welcome-bg.png");
    if (path.empty()) {
        return fail("welcome/welcome-bg.png was not found");
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface != nullptr) {
            cairo_surface_destroy(surface);
        }
        return fail("welcome/welcome-bg.png did not load");
    }
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    cairo_surface_destroy(surface);
    if (width != 1712 || height != 1152) {
        return fail("welcome background is " + std::to_string(width) + "x" + std::to_string(height) +
                    ", expected 1712x1152");
    }
    for (const char *name : {"fonts/DejaVuSerif.ttf", "fonts/DejaVuSerif-Bold.ttf", "fonts/LICENSE-DejaVu"}) {
        if (ui_data_file(name).empty()) {
            return fail(std::string(name) + " was not found");
        }
    }
    return 0;
}

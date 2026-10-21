// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Each scenario thumbnail is painted from that scenario's own map.

#include "city_session.hpp"
#include "scenario_preview.hpp"

#include <cairo.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int fail(const std::string &message)
{
    std::cerr << message << "\n";
    return 1;
}

std::uint64_t checksum(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    std::uint64_t sum = 1469598103934665603ull;
    int samples = 0;
    for (int y = 0; y < height; y += 3) {
        for (int x = 0; x < width; x += 3) {
            const unsigned char *px = data + y * stride + x * 4;
            sum ^= static_cast<std::uint64_t>(px[0]) << 16 ^ static_cast<std::uint64_t>(px[1]) << 8 ^ px[2];
            sum *= 1099511628211ull;
            ++samples;
        }
    }
    if (samples < 10) {
        return 0;
    }
    return sum;
}

} // namespace

int main()
{
    std::uint64_t hashes[CitySession::kScenarioCount] = {};
    for (int i = 0; i < CitySession::kScenarioCount; ++i) {
        cairo_surface_t *surface = scenario_preview(i);
        if (surface == nullptr) {
            return fail("scenario " + std::to_string(i) + " has no preview");
        }
        if (cairo_image_surface_get_width(surface) != CitySession::kWorldW ||
            cairo_image_surface_get_height(surface) != CitySession::kWorldH) {
            return fail("scenario preview is not the map size");
        }
        hashes[i] = checksum(surface);
        if (hashes[i] == 0) {
            return fail("scenario " + std::to_string(i) + " preview is empty");
        }
        for (int earlier = 0; earlier < i; ++earlier) {
            if (hashes[earlier] == hashes[i]) {
                return fail("scenario previews " + std::to_string(earlier) + " and " + std::to_string(i) + " match");
            }
        }
    }
    if (scenario_preview(0) == nullptr || checksum(scenario_preview(0)) != hashes[0]) {
        return fail("scenario previews were rebuilt instead of cached");
    }
    return 0;
}

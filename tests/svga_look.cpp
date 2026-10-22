// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "tile_atlas.hpp"
#include "ui_scale.hpp"
#include "vga_palette.hpp"

#include <cairo.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char *message)
{
    std::cerr << message << "\n";
    return 1;
}

std::uint32_t word_at(const unsigned char *px)
{
    return static_cast<std::uint32_t>(px[0]) | (static_cast<std::uint32_t>(px[1]) << 8) |
           (static_cast<std::uint32_t>(px[2]) << 16) | (static_cast<std::uint32_t>(px[3]) << 24);
}

unsigned mix4(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, int shift)
{
    const unsigned sum = ((a >> shift) & 255u) + ((b >> shift) & 255u) + ((c >> shift) & 255u) + ((d >> shift) & 255u);
    return (sum + 2u) / 4u;
}

} // namespace

int main()
{
    ui_scale::StoredSettings saved;
    saved.look = 1;
    saved.user_scale = 1.0f;
    const std::string text = ui_scale::settings_text(saved);
    if (text.find("look=1") == std::string::npos) {
        return fail("settings text omitted the SVGA look");
    }
    const ui_scale::StoredSettings restored = ui_scale::settings_parse(text);
    if (restored.look != 1) {
        return fail("settings parse did not restore SVGA");
    }
    const ui_scale::StoredSettings classic = ui_scale::settings_parse("look=0\nui_scale=1\n");
    if (classic.look != 0) {
        return fail("look=0 should be Classic");
    }
    if (ui_scale::settings_parse("ui_scale=1\n").look != -1) {
        return fail("a file without look should leave it unset");
    }

    TileAtlas &atlas = tile_atlas();
    if (!atlas.loaded() || !atlas.svga_available() || atlas.use_svga()) {
        return fail("Classic should load, with the SVGA sheet available and not active");
    }
    if (atlas.count() < 960) {
        return fail("the atlas is short");
    }

    // Classic pixels and the 8px overview must match the PNG and the box
    // filter exactly. The SVGA snap is not applied on this path.
    cairo_surface_t *surface = nullptr;
    const char *asset = std::getenv("LUNDUKE_CITY_ASSETS");
    std::string tiles = asset != nullptr ? std::string(asset) + "/images/micropolisEngine/tiles.png" : std::string();
    if (tiles.empty()) {
        return fail("LUNDUKE_CITY_ASSETS is not set");
    }
    surface = cairo_image_surface_create_from_png(tiles.c_str());
    if (surface == nullptr || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return fail("could not read classic tiles.png");
    }
    cairo_surface_flush(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char *src = cairo_image_surface_get_data(surface);
    const int columns = cairo_image_surface_get_width(surface) / TileAtlas::kSize;
    std::vector<std::uint32_t> classic16(static_cast<std::size_t>(TileAtlas::kSize * TileAtlas::kSize));
    std::vector<std::uint32_t> classic8(static_cast<std::size_t>(TileAtlas::kOverview * TileAtlas::kOverview));
    std::vector<std::uint32_t> classic_ground;
    for (int tile = 0; tile < 960; ++tile) {
        const int col = tile % columns;
        const int row = tile / columns;
        atlas.blit(tile, reinterpret_cast<unsigned char *>(classic16.data()), TileAtlas::kSize * 4);
        atlas.blit_overview(tile, reinterpret_cast<unsigned char *>(classic8.data()), TileAtlas::kOverview * 4);
        if (tile == 0) {
            classic_ground = classic16;
        }
        for (int y = 0; y < TileAtlas::kSize; ++y) {
            const unsigned char *line = src + (row * TileAtlas::kSize + y) * stride + col * TileAtlas::kSize * 4;
            for (int x = 0; x < TileAtlas::kSize; ++x) {
                const std::uint32_t file = word_at(line + x * 4);
                const unsigned r = (file >> 16) & 255u;
                const unsigned g = (file >> 8) & 255u;
                const unsigned b = file & 255u;
                const std::uint32_t packed = (255u << 24) | (r << 16) | (g << 8) | b;
                if (classic16[static_cast<std::size_t>(y * TileAtlas::kSize + x)] != packed) {
                    cairo_surface_destroy(surface);
                    return fail("Classic 16px pixels differ from tiles.png");
                }
            }
        }
        for (int y = 0; y < TileAtlas::kOverview; ++y) {
            for (int x = 0; x < TileAtlas::kOverview; ++x) {
                const std::uint32_t p00 = classic16[static_cast<std::size_t>((y * 2) * TileAtlas::kSize + x * 2)];
                const std::uint32_t p10 = classic16[static_cast<std::size_t>((y * 2) * TileAtlas::kSize + x * 2 + 1)];
                const std::uint32_t p01 = classic16[static_cast<std::size_t>((y * 2 + 1) * TileAtlas::kSize + x * 2)];
                const std::uint32_t p11 = classic16[static_cast<std::size_t>((y * 2 + 1) * TileAtlas::kSize + x * 2 + 1)];
                const std::uint32_t mixed = (mix4(p00, p10, p01, p11, 24) << 24) | (mix4(p00, p10, p01, p11, 16) << 16) |
                                            (mix4(p00, p10, p01, p11, 8) << 8) | mix4(p00, p10, p01, p11, 0);
                if (classic8[static_cast<std::size_t>(y * TileAtlas::kOverview + x)] != mixed) {
                    cairo_surface_destroy(surface);
                    return fail("Classic 8px overview is not the box average");
                }
            }
        }
    }
    cairo_surface_destroy(surface);

    double cr = 0;
    double cg = 0;
    double cb = 0;
    atlas.average_color(0, cr, cg, cb);
    if (!atlas.set_look(true) || !atlas.use_svga()) {
        return fail("set_look did not switch to SVGA");
    }
    double sr = 0;
    double sg = 0;
    double sb = 0;
    atlas.average_color(0, sr, sg, sb);
    if (sr == cr && sg == cg && sb == cb) {
        return fail("the minimap colour of dirt did not change with the look");
    }
    std::vector<std::uint32_t> svga16(classic16.size());
    std::vector<std::uint32_t> svga8(classic8.size());
    int off_palette = 0;
    atlas.blit(0, reinterpret_cast<unsigned char *>(svga16.data()), TileAtlas::kSize * 4);
    if (std::memcmp(svga16.data(), classic_ground.data(), classic_ground.size() * sizeof(std::uint32_t)) == 0) {
        return fail("SVGA ground matches Classic");
    }
    for (int tile = 0; tile < 960; ++tile) {
        atlas.blit(tile, reinterpret_cast<unsigned char *>(svga16.data()), TileAtlas::kSize * 4);
        atlas.blit_overview(tile, reinterpret_cast<unsigned char *>(svga8.data()), TileAtlas::kOverview * 4);
        for (int y = 0; y < TileAtlas::kSize; ++y) {
            for (int x = 0; x < TileAtlas::kSize; ++x) {
                const std::uint32_t px = svga16[static_cast<std::size_t>(y * TileAtlas::kSize + x)];
                const auto snapped = snap_vga_rgb((px >> 16) & 255u, (px >> 8) & 255u, px & 255u);
                if (((px & 0x00ffffffu) | (255u << 24)) != snapped) {
                    ++off_palette;
                }
            }
        }
        for (int y = 0; y < TileAtlas::kOverview; ++y) {
            for (int x = 0; x < TileAtlas::kOverview; ++x) {
                const std::uint32_t p00 = svga16[static_cast<std::size_t>((y * 2) * TileAtlas::kSize + x * 2)];
                const std::uint32_t p10 = svga16[static_cast<std::size_t>((y * 2) * TileAtlas::kSize + x * 2 + 1)];
                const std::uint32_t p01 = svga16[static_cast<std::size_t>((y * 2 + 1) * TileAtlas::kSize + x * 2)];
                const std::uint32_t p11 = svga16[static_cast<std::size_t>((y * 2 + 1) * TileAtlas::kSize + x * 2 + 1)];
                const unsigned r = mix4(p00, p10, p01, p11, 16);
                const unsigned g = mix4(p00, p10, p01, p11, 8);
                const unsigned b = mix4(p00, p10, p01, p11, 0);
                const std::uint32_t expect = snap_vga_rgb(r, g, b);
                if (svga8[static_cast<std::size_t>(y * TileAtlas::kOverview + x)] != expect) {
                    return fail("SVGA 8px overview is not the snapped box average");
                }
            }
        }
    }
    if (off_palette != 0) {
        return fail("SVGA 16px atlas left the VGA palette");
    }

    if (atlas.set_look(false) || atlas.use_svga()) {
        return fail("set_look(false) did not return to Classic");
    }
    std::vector<std::uint32_t> back(classic_ground.size());
    atlas.blit(0, reinterpret_cast<unsigned char *>(back.data()), TileAtlas::kSize * 4);
    if (std::memcmp(back.data(), classic_ground.data(), classic_ground.size() * sizeof(std::uint32_t)) != 0) {
        return fail("Classic look did not restore the ground tile");
    }
    std::vector<std::uint32_t> tile0(classic16.size());
    atlas.blit(52, reinterpret_cast<unsigned char *>(tile0.data()), TileAtlas::kSize * 4);

    atlas.forget_svga();
    if (atlas.set_look(true) || atlas.use_svga()) {
        return fail("a missing SVGA sheet must fall back to Classic");
    }
    std::vector<std::uint32_t> fell(classic16.size());
    atlas.blit(52, reinterpret_cast<unsigned char *>(fell.data()), TileAtlas::kSize * 4);
    if (std::memcmp(tile0.data(), fell.data(), tile0.size() * sizeof(std::uint32_t)) != 0) {
        return fail("fallback changed Classic pixels");
    }
    // The stored choice stays SVGA. Only the drawn sheet falls back.
    ui_scale::LookPreference stored_svga;
    stored_svga.svga = true;
    if (stored_svga.active(atlas.svga_available()) || stored_svga.setting() != 1) {
        return fail("a missing SVGA sheet must not replace the stored choice");
    }
    ui_scale::LookPreference picked_svga;
    picked_svga.svga = true;
    if (picked_svga.active(false) || picked_svga.setting() != 1) {
        return fail("picking SVGA with no sheet must not store Classic");
    }
    const ui_scale::LookPreference picked_classic;
    if (picked_classic.setting() != 0 || picked_classic.active(true)) {
        return fail("picking Classic must store Classic");
    }
    if (!stored_svga.active(true)) {
        return fail("SVGA should be drawn when the sheet is ready");
    }
    std::cout << "svga look ok\n";
    return 0;
}

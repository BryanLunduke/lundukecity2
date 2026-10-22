// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "map_view.hpp"

#include "assets.hpp"
#include "map_fit.hpp"
#include "special_buildings.hpp"

#include "sprite_art.hpp"
#include "tile_atlas.hpp"
#include "tools.hpp"
#include "vga_palette.hpp"
#include "view_math.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

MapView::MapView()
{
    set_tile_size(tile_size_);
}

MapView::~MapView()
{
    if (map_pixels_ != nullptr) {
        cairo_surface_destroy(map_pixels_);
        map_pixels_ = nullptr;
    }
}

bool MapView::tick(int elapsed_ms)
{
    if (!active_) {
        return false;
    }
    blink_ms_ += elapsed_ms;
    if (blink_ms_ < 500) {
        return false;
    }
    blink_ms_ = 0;
    blink_on_ = !blink_on_;
    sync();
    return last_sync_blits_ > 0;
}

void MapView::invalidate_all()
{
    if (!cached_raw_.empty()) {
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
        std::fill(cached_shown_.begin(), cached_shown_.end(), -1);
    }
}

void MapView::set_session(CitySession *session)
{
    session_ = session;
    cached_raw_.clear();
    cached_shown_.clear();
}

void MapView::set_tool(int engine_tool)
{
    engine_tool_ = engine_tool;
}

void MapView::set_hover_tile(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= CitySession::kWorldW || ty >= CitySession::kWorldH) {
        clear_hover();
        return;
    }
    hover_valid_ = true;
    hover_x_ = tx;
    hover_y_ = ty;
}

void MapView::clear_hover()
{
    hover_valid_ = false;
}

void MapView::set_tile_size(int pixels)
{
    tile_size_ = snap_map_tile(pixels);
}

void MapView::set_shake(int dx, int dy)
{
    shake_x_ = dx;
    shake_y_ = dy;
}

int MapView::pixel_width() const
{
    return CitySession::kWorldW * tile_size_;
}

int MapView::pixel_height() const
{
    return CitySession::kWorldH * tile_size_;
}

bool MapView::tile_at(double x, double y, int &tx, int &ty) const
{
    return map_tile_at(x, y, shake_x_, shake_y_, tile_size_, CitySession::kWorldW, CitySession::kWorldH, tx, ty);
}

int MapView::source_px() const
{
    return tile_size_ <= kMapTileOverview ? TileAtlas::kOverview : TileAtlas::kSize;
}

void MapView::ensure_surface()
{
    const int width = CitySession::kWorldW * source_px();
    const int height = CitySession::kWorldH * source_px();
    if (map_pixels_ == nullptr || cairo_image_surface_get_width(map_pixels_) != width ||
        cairo_image_surface_get_height(map_pixels_) != height) {
        if (map_pixels_ != nullptr) {
            cairo_surface_destroy(map_pixels_);
        }
        map_pixels_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cached_raw_.clear();
        cached_shown_.clear();
    }
    const std::size_t cells = static_cast<std::size_t>(CitySession::kWorldW * CitySession::kWorldH);
    if (cached_raw_.size() != cells) {
        cached_raw_.assign(cells, -1);
        cached_shown_.assign(cells, -1);
    }
}

int MapView::display_tile(int raw, const TileAtlas &atlas) const
{
    int tile = raw & LOMASK;
    if (blink_on_ && (raw & ZONEBIT) != 0 && (raw & PWRBIT) == 0 && atlas.loaded() &&
        LIGHTNINGBOLT < atlas.count()) {
        tile = LIGHTNINGBOLT;
    }
    return tile;
}

void MapView::blit_tile(int x, int y, int shown, const TileAtlas &atlas)
{
    const int px = source_px();
    unsigned char *data = cairo_image_surface_get_data(map_pixels_);
    const int stride = cairo_image_surface_get_stride(map_pixels_);
    unsigned char *dest = data + y * px * stride + x * px * 4;
    if (atlas.loaded()) {
        if (px == TileAtlas::kOverview) {
            atlas.blit_overview(shown, dest, stride);
        } else {
            atlas.blit(shown, dest, stride);
        }
        return;
    }
    const std::uint32_t dirt = (255u << 24) | (204u << 16) | (127u << 8) | 102u;
    for (int row = 0; row < px; ++row) {
        auto *out = reinterpret_cast<std::uint32_t *>(dest + row * stride);
        for (int col = 0; col < px; ++col) {
            out[col] = dirt;
        }
    }
}

namespace {

cairo_surface_t *load_png(const std::string &path)
{
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
}

cairo_surface_t *special_sprite(int index, bool &vga_snap)
{
    vga_snap = false;
    struct Slot {
        cairo_surface_t *image = nullptr;
        bool tried = false;
    };
    struct Cache {
        Slot slots[8];
        Slot spooky_park;
        ~Cache()
        {
            for (Slot &slot : slots) {
                if (slot.image != nullptr) {
                    cairo_surface_destroy(slot.image);
                }
            }
            if (spooky_park.image != nullptr) {
                cairo_surface_destroy(spooky_park.image);
            }
        }
    };
    static Cache cache;
    if (index < 0 || index >= 8 || index >= special_building_count()) {
        return nullptr;
    }
    // Big Park is a separate sprite, not an atlas cell. Spooky draws the
    // graveyard sheet when it loaded. A missing file keeps the classic park
    // and does not snap that fallback.
    if (tile_atlas().look() == 2 && std::strcmp(special_building(index).map_sprite, "big-park") == 0) {
        Slot &park = cache.spooky_park;
        if (!park.tried) {
            park.tried = true;
            park.image = load_png(ui_data_file("special-tiles/big-park-spooky.png"));
        }
        if (park.image != nullptr) {
            vga_snap = true;
            return park.image;
        }
    }
    Slot &slot = cache.slots[index];
    if (slot.tried) {
        return slot.image;
    }
    slot.tried = true;
    slot.image = load_png(ui_data_file(std::string("special-tiles/") + special_building(index).map_sprite + ".png"));
    return slot.image;
}

// 0 when this cell is not part of a special building. Otherwise a key that
// changes when the building identity or the cell inside it changes.
int special_display_key(const CitySession *session, int x, int y, int &index, int &cell)
{
    index = -1;
    cell = 4;
    if (session == nullptr) {
        return 0;
    }
    const int raw = session->map_value(x, y);
    const int tile = raw & LOMASK;
    if (tile != kSpecialBodyTile && special_index_for_tile(tile) < 0) {
        return 0;
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int cx = x + dx;
            const int cy = y + dy;
            const int neighbor = session->map_value(cx, cy);
            if ((neighbor & ZONEBIT) == 0) {
                continue;
            }
            const int found = special_index_for_tile(neighbor & LOMASK);
            if (found < 0) {
                continue;
            }
            if (x < cx - 1 || x > cx + 1 || y < cy - 1 || y > cy + 1) {
                continue;
            }
            index = found;
            const int ox = x - (cx - 1);
            const int oy = y - (cy - 1);
            cell = oy * 3 + ox;
            return 40000 + found * 16 + cell;
        }
    }
    return 39990;
}

} // namespace

void MapView::blit_special(int x, int y, int index, int cell)
{
    const int px = source_px();
    unsigned char *data = cairo_image_surface_get_data(map_pixels_);
    const int stride = cairo_image_surface_get_stride(map_pixels_);
    unsigned char *dest = data + y * px * stride + x * px * 4;
    auto fill = [&](std::uint32_t color) {
        for (int row = 0; row < px; ++row) {
            auto *out = reinterpret_cast<std::uint32_t *>(dest + row * stride);
            for (int col = 0; col < px; ++col) {
                out[col] = color;
            }
        }
    };
    bool vga_snap = false;
    cairo_surface_t *sprite = special_sprite(index, vga_snap);
    const int col = cell % 3;
    const int row = cell / 3;
    if (sprite == nullptr || col < 0 || row < 0) {
        const std::uint32_t fallback[3] = {
            (255u << 24) | (48u << 16) | (140u << 8) | 40u,
            (255u << 24) | (48u << 16) | (160u << 8) | 32u,
            (255u << 24) | (90u << 16) | (110u << 8) | 150u,
        };
        fill(index >= 0 && index < 3 ? fallback[index] : ((255u << 24) | (40u << 16) | (60u << 8) | 70u));
        return;
    }
    cairo_surface_flush(sprite);
    const int sw = cairo_image_surface_get_width(sprite);
    const int sh = cairo_image_surface_get_height(sprite);
    const int sx = col * TileAtlas::kSize;
    const int sy = row * TileAtlas::kSize;
    if (sx + TileAtlas::kSize > sw || sy + TileAtlas::kSize > sh) {
        fill((255u << 24) | (40u << 16) | (60u << 8) | 70u);
        return;
    }
    const int sstride = cairo_image_surface_get_stride(sprite);
    const unsigned char *src = cairo_image_surface_get_data(sprite);
    if (px == TileAtlas::kOverview) {
        auto word_at = [&](int rx, int ry) {
            const unsigned char *p = src + (sy + ry) * sstride + (sx + rx) * 4;
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
                   (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        };
        auto mix = [](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, int shift) {
            const unsigned sum = ((a >> shift) & 255u) + ((b >> shift) & 255u) + ((c >> shift) & 255u) + ((d >> shift) & 255u);
            return (sum + 2u) / 4u;
        };
        for (int r = 0; r < TileAtlas::kOverview; ++r) {
            auto *out = reinterpret_cast<std::uint32_t *>(dest + r * stride);
            for (int c = 0; c < TileAtlas::kOverview; ++c) {
                const std::uint32_t p00 = word_at(c * 2, r * 2);
                const std::uint32_t p10 = word_at(c * 2 + 1, r * 2);
                const std::uint32_t p01 = word_at(c * 2, r * 2 + 1);
                const std::uint32_t p11 = word_at(c * 2 + 1, r * 2 + 1);
                const unsigned red = mix(p00, p10, p01, p11, 16);
                const unsigned green = mix(p00, p10, p01, p11, 8);
                const unsigned blue = mix(p00, p10, p01, p11, 0);
                const unsigned alpha = mix(p00, p10, p01, p11, 24);
                out[c] = vga_snap ? snap_vga_rgb(red, green, blue)
                                  : ((alpha << 24) | (red << 16) | (green << 8) | blue);
            }
        }
        return;
    }
    for (int r = 0; r < TileAtlas::kSize; ++r) {
        std::memcpy(dest + r * stride, src + (sy + r) * sstride + sx * 4, static_cast<std::size_t>(TileAtlas::kSize * 4));
    }
}

void MapView::sync()
{
    ensure_surface();
    TileAtlas &atlas = tile_atlas();
    if (atlas.loaded() != atlas_was_loaded_ || atlas.look_generation() != look_generation_) {
        atlas_was_loaded_ = atlas.loaded();
        look_generation_ = atlas.look_generation();
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
        std::fill(cached_shown_.begin(), cached_shown_.end(), -1);
    }
    int dirty = 0;
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    for (int y = 0; y < world_h; ++y) {
        for (int x = 0; x < world_w; ++x) {
            const int raw = session_ != nullptr ? session_->map_value(x, y) : 0;
            const std::size_t index = static_cast<std::size_t>(y * world_w + x);
            int special_index = -1;
            int special_cell = 0;
            const int special_key = special_display_key(session_, x, y, special_index, special_cell);
            if (special_key != 0) {
                if (cached_raw_[index] == raw && cached_shown_[index] == special_key) {
                    continue;
                }
                blit_special(x, y, special_index, special_cell);
                cached_raw_[index] = raw;
                cached_shown_[index] = special_key;
                ++dirty;
                continue;
            }
            const int shown = display_tile(raw, atlas);
            if (cached_raw_[index] == raw && cached_shown_[index] == shown) {
                continue;
            }
            blit_tile(x, y, shown, atlas);
            cached_raw_[index] = raw;
            cached_shown_[index] = shown;
            ++dirty;
        }
    }
    last_sync_blits_ = dirty;
    if (dirty > 0) {
        cairo_surface_mark_dirty(map_pixels_);
    }
}

void MapView::follow_local_pointer(double x, double y, bool drag)
{
    int tx = 0;
    int ty = 0;
    if (!tile_at(x, y, tx, ty)) {
        clear_hover();
        return;
    }
    set_hover_tile(tx, ty);
    if (!drag || !dragging_) {
        return;
    }
    if (tx == last_x_ && ty == last_y_) {
        return;
    }
    if (on_tool_drag) {
        on_tool_drag(last_x_, last_y_, tx, ty);
    }
    last_x_ = tx;
    last_y_ = ty;
}

bool MapView::press(double x, double y)
{
    int tx = 0;
    int ty = 0;
    if (!tile_at(x, y, tx, ty)) {
        return false;
    }
    dragging_ = true;
    last_x_ = tx;
    last_y_ = ty;
    set_hover_tile(tx, ty);
    if (on_tool_down) {
        on_tool_down(tx, ty);
    }
    return true;
}

void MapView::release()
{
    dragging_ = false;
}

void MapView::motion(double x, double y, bool button_down)
{
    if (dragging_ && !button_down) {
        dragging_ = false;
    }
    follow_local_pointer(x, y, dragging_ && button_down);
}

void MapView::leave()
{
    if (!dragging_) {
        clear_hover();
    }
}

bool MapView::edge_scroll(double view_x, double view_y, int view_w, int view_h, double &dx, double &dy) const
{
    dx = 0;
    dy = 0;
    if (!dragging_ || view_w < 1 || view_h < 1) {
        return false;
    }
    const int margin = 28;
    const double step = std::max(tile_size_, 8);
    if (view_x < margin) {
        dx = -step;
    } else if (view_x > view_w - margin) {
        dx = step;
    }
    if (view_y < margin) {
        dy = -step;
    } else if (view_y > view_h - margin) {
        dy = step;
    }
    return dx != 0.0 || dy != 0.0;
}

void MapView::draw_footprint(cairo_t *cr) const
{
    if (!hover_valid_) {
        return;
    }
    ToolFootprint foot = tool_footprint(engine_tool_);
    // Query has no building footprint. At the overview zoom the crosshair is
    // a whole tile, so the cell under it still needs an outline before A.
    if (tile_size_ <= kMapTileOverview && !foot.placeable) {
        foot.placeable = true;
        foot.width = 1;
        foot.height = 1;
        foot.cursor_to_left = 0;
        foot.cursor_to_top = 0;
    }
    if (!foot.placeable || foot.width < 1 || foot.height < 1) {
        return;
    }
    const double x = static_cast<double>(hover_x_ - foot.cursor_to_left) * tile_size_;
    const double y = static_cast<double>(hover_y_ - foot.cursor_to_top) * tile_size_;
    const double w = static_cast<double>(foot.width) * tile_size_;
    const double h = static_cast<double>(foot.height) * tile_size_;
    if (w < 2.0 || h < 2.0) {
        return;
    }
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (tile_size_ <= kMapTileOverview) {
        cairo_rectangle(cr, x, y, w, h);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.90);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    } else {
        cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.90);
        cairo_set_line_width(cr, 3.0);
        cairo_stroke(cr);
        cairo_rectangle(cr, x + 1.0, y + 1.0, w - 2.0, h - 2.0);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

void MapView::draw(cairo_t *cr)
{
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (map_pixels_ == nullptr) {
        sync();
    }
    if (map_pixels_ == nullptr) {
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, shake_x_, shake_y_);
    // 8px draws the prefiltered half atlas 1:1. Larger steps scale the 16px
    // atlas by an integer. Sprite positions are in 16px map pixels.
    const int source = source_px();
    const double scale = static_cast<double>(tile_size_) / static_cast<double>(source);
    const double sprite_to_source = static_cast<double>(source) / static_cast<double>(TileAtlas::kSize);
    cairo_save(cr);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, map_pixels_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);

    if (session_ != nullptr) {
        for (const auto &dot : session_->sprites()) {
            cairo_surface_t *image = sprite_frame(dot.type, dot.frame);
            if (image == nullptr) {
                continue;
            }
            cairo_save(cr);
            cairo_translate(cr, (dot.x + dot.x_offset) * sprite_to_source, (dot.y + dot.y_offset) * sprite_to_source);
            cairo_scale(cr, sprite_to_source, sprite_to_source);
            cairo_set_source_surface(cr, image, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }
    cairo_restore(cr);
    draw_footprint(cr);
    cairo_restore(cr);
}

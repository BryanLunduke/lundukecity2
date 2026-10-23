// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "map_view.hpp"

#include <cairomm/context.h>

#include "sprite_art.hpp"
#include "tile_atlas.hpp"
#include "tools.hpp"
#include "view_math.hpp"

#include "micropolis.h"

#include <algorithm>
#include <cstdint>

MapView::MapView()
{
    set_tile_size(tile_size_);
}

MapView::~MapView() = default;

void MapView::tick(int elapsed_ms)
{
    if (!active_) {
        return;
    }
    blink_ms_ += elapsed_ms;
    if (blink_ms_ < 500) {
        return;
    }
    blink_ms_ = 0;
    blink_on_ = !blink_on_;
    sync();
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
    tile_size_ = std::max(6, std::min(16, pixels));
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

void MapView::ensure_surface()
{
    const int width = CitySession::kWorldW * TileAtlas::kSize;
    const int height = CitySession::kWorldH * TileAtlas::kSize;
    if (!map_pixels_ || map_pixels_->get_width() != width || map_pixels_->get_height() != height) {
        map_pixels_ = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, width, height);
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
    unsigned char *data = map_pixels_->get_data();
    const int stride = map_pixels_->get_stride();
    unsigned char *dest = data + y * TileAtlas::kSize * stride + x * TileAtlas::kSize * 4;
    if (atlas.loaded()) {
        atlas.blit(shown, dest, stride);
        return;
    }
    const std::uint32_t dirt = (255u << 24) | (204u << 16) | (127u << 8) | 102u;
    for (int row = 0; row < TileAtlas::kSize; ++row) {
        auto *px = reinterpret_cast<std::uint32_t *>(dest + row * stride);
        for (int col = 0; col < TileAtlas::kSize; ++col) {
            px[col] = dirt;
        }
    }
}

void MapView::sync()
{
    ensure_surface();
    TileAtlas &atlas = tile_atlas();
    if (atlas.loaded() != atlas_was_loaded_) {
        atlas_was_loaded_ = atlas.loaded();
        std::fill(cached_raw_.begin(), cached_raw_.end(), -1);
        std::fill(cached_shown_.begin(), cached_shown_.end(), -1);
    }
    int dirty = 0;
    const int world_w = CitySession::kWorldW;
    const int world_h = CitySession::kWorldH;
    for (int y = 0; y < world_h; ++y) {
        for (int x = 0; x < world_w; ++x) {
            const int raw = session_ != nullptr ? session_->map_value(x, y) : 0;
            const int shown = display_tile(raw, atlas);
            const std::size_t index = static_cast<std::size_t>(y * world_w + x);
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
        map_pixels_->mark_dirty();
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

void MapView::draw_footprint(const Cairo::RefPtr<Cairo::Context> &cr) const
{
    if (!hover_valid_) {
        return;
    }
    const ToolFootprint foot = tool_footprint(engine_tool_);
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
    cr->save();
    cr->set_antialias(Cairo::ANTIALIAS_NONE);
    cr->rectangle(x + 1.0, y + 1.0, w - 2.0, h - 2.0);
    cr->set_source_rgba(0.0, 0.0, 0.0, 0.90);
    cr->set_line_width(3.0);
    cr->stroke();
    cr->rectangle(x + 1.0, y + 1.0, w - 2.0, h - 2.0);
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.95);
    cr->set_line_width(1.5);
    cr->stroke();
    cr->restore();
}

void MapView::draw(const Cairo::RefPtr<Cairo::Context> &cr)
{
    cr->set_antialias(Cairo::ANTIALIAS_NONE);
    if (!map_pixels_) {
        sync();
    }
    if (!map_pixels_) {
        return;
    }

    cr->save();
    cr->translate(shake_x_, shake_y_);
    const double scale = static_cast<double>(tile_size_) / static_cast<double>(TileAtlas::kSize);
    cr->save();
    cr->scale(scale, scale);
    cr->set_source(map_pixels_, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr->cobj()), CAIRO_FILTER_NEAREST);
    cr->paint();

    if (session_ != nullptr) {
        for (const auto &dot : session_->sprites()) {
            auto image = sprite_frame(dot.type, dot.frame);
            if (!image) {
                continue;
            }
            cr->save();
            cr->translate(dot.x + dot.x_offset, dot.y + dot.y_offset);
            cr->set_source(image, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr->cobj()), CAIRO_FILTER_NEAREST);
            cr->paint();
            cr->restore();
        }
    }
    cr->restore();
    draw_footprint(cr);
    cr->restore();
}

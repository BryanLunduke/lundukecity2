// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

// obj<type>-<frame>.png sprite frames. type is the engine sprite id
// (1 train .. 8 bus). frame is 1-based, matching SimSprite::frame.
// The returned surface is owned by the sprite cache. Do not destroy it.
cairo_surface_t *sprite_frame(int type, int frame);

// Drops every cached explosion and vehicle frame. Safe to call twice.
void sprite_art_shutdown();

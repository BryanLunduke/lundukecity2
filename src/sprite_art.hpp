// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

// obj<type>-<frame>.png sprite frames. type is the engine sprite id
// (1 train .. 8 bus). frame is 1-based, matching SimSprite::frame.
// SVGA and Spooky use obj<type>-<frame>-svga.png and -spooky.png when those
// files exist, and the classic frame otherwise.
// The returned surface is owned by the sprite cache. Do not destroy it.
// look is 0 classic, 1 SVGA, 2 Spooky. The cache keeps one sheet per frame
// and reloads when the look changes. A missing sheet stays on Classic.
void sprite_art_set_look(int look);
cairo_surface_t *sprite_frame(int type, int frame);

// Drops every cached explosion and vehicle frame. Safe to call twice.
void sprite_art_shutdown();

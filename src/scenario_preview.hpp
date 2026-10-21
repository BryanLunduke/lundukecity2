// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

// Starting map of one scenario, 120x100, one pixel per tile. Built from the
// scenario file the game actually loads. Null when that scenario cannot be read.
cairo_surface_t *scenario_preview(int index);

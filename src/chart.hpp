// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairo.h>

#include <string>
#include <vector>

class CitySession;

struct ChartLegend {
    std::string caption;
    double r = 0;
    double g = 0;
    double b = 0;
    double dash_on = 0;
    double dash_off = 0;
};

// enabled_mask bit i draws series i. pointer_x is relative to the chart;
// negative means no readout. readout, when set, receives the hover line.
void draw_history_chart(cairo_t *cr, CitySession *session, bool long_scale, int width, int height,
                        int enabled_mask = 0x7f, int pointer_x = -1, std::string *readout = nullptr);
std::vector<ChartLegend> chart_legend(CitySession *session, bool long_scale);
std::string chart_population_line(CitySession *session);
std::string chart_funds_line(CitySession *session);
const char *chart_scale_note(bool long_scale);

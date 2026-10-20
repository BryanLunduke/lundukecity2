// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cairomm/context.h>
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

void draw_history_chart(const Cairo::RefPtr<Cairo::Context> &cr, CitySession *session, bool long_scale, int width,
                        int height);
std::vector<ChartLegend> chart_legend(CitySession *session, bool long_scale);
std::string chart_population_line(CitySession *session);
std::string chart_funds_line(CitySession *session);
const char *chart_scale_note(bool long_scale);

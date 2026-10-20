// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "chart.hpp"

#include <cairomm/context.h>

#include "city_session.hpp"
#include "graph_legend.hpp"
#include "graph_palette.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct SeriesSpec {
    const char *name;
    double r;
    double g;
    double b;
    double dash_on;
    double dash_off;
    bool population;
    bool cash;
    GraphLegendKind legend;
    CitySession::HistorySeries history;
};

void apply_dash(const Cairo::RefPtr<Cairo::Context> &cr, double on, double off)
{
    if (on <= 0.0 || off <= 0.0) {
        cr->unset_dash();
        return;
    }
    const std::vector<double> pattern{on, off};
    cr->set_dash(pattern, 0.0);
}

long population_at(const CitySession &session, CitySession::HistoryScale scale, int index)
{
    const int residential = session.history_value(CitySession::HistorySeries::Residential, scale, index);
    const int commercial = session.history_value(CitySession::HistorySeries::Commercial, scale, index);
    const int industrial = session.history_value(CitySession::HistorySeries::Industrial, scale, index);
    return static_cast<long>(residential + commercial + industrial) * kHistoryPeoplePerSample;
}

void fill_series(SeriesSpec out[kGraphSeriesCount])
{
    const GraphLegendKind kinds[kGraphSeriesCount] = {
        GraphLegendKind::Population, GraphLegendKind::People, GraphLegendKind::People, GraphLegendKind::People,
        GraphLegendKind::CashFlow,   GraphLegendKind::Level,  GraphLegendKind::Level,
    };
    const CitySession::HistorySeries histories[kGraphSeriesCount] = {
        CitySession::HistorySeries::Residential, CitySession::HistorySeries::Residential,
        CitySession::HistorySeries::Commercial,  CitySession::HistorySeries::Industrial,
        CitySession::HistorySeries::CashFlow,    CitySession::HistorySeries::Crime,
        CitySession::HistorySeries::Pollution,
    };
    for (int i = 0; i < kGraphSeriesCount; ++i) {
        const GraphSeriesStyle &style = kGraphSeries[i];
        out[i] = SeriesSpec{style.name,
                            graph_channel(style.light_r),
                            graph_channel(style.light_g),
                            graph_channel(style.light_b),
                            style.dash_on,
                            style.dash_off,
                            i == 0,
                            i == 4,
                            kinds[i],
                            histories[i]};
    }
}

double sample_series(const CitySession &session, const SeriesSpec &item, CitySession::HistoryScale scale, int index)
{
    if (item.population) {
        return static_cast<double>(population_at(session, scale, index));
    }
    if (item.cash) {
        return static_cast<double>(session.cash_flow_history(scale, index));
    }
    return session.history_value(item.history, scale, index);
}

} // namespace

const char *chart_scale_note(bool long_scale)
{
    if (long_scale) {
        return "120 yearly samples. Each line is scaled to its own range. The right edge is the newest year.";
    }
    return "120 monthly samples. Each line is scaled to its own range. The right edge is the newest month.";
}

std::string chart_population_line(CitySession *session)
{
    if (session == nullptr) {
        return "Population";
    }
    return graph_legend_caption("Population", GraphLegendKind::Population, session->evaluation().population);
}

std::string chart_funds_line(CitySession *session)
{
    if (session == nullptr) {
        return "Funds";
    }
    return "Funds: " + graph_money(session->funds());
}

std::vector<ChartLegend> chart_legend(CitySession *session, bool long_scale)
{
    SeriesSpec specs[kGraphSeriesCount];
    fill_series(specs);
    const auto scale = long_scale ? CitySession::HistoryScale::Long : CitySession::HistoryScale::Short;
    std::vector<ChartLegend> out;
    const long census = session != nullptr ? session->evaluation().population : 0;
    for (int i = 0; i < kGraphSeriesCount; ++i) {
        ChartLegend row;
        row.r = specs[i].r;
        row.g = specs[i].g;
        row.b = specs[i].b;
        row.dash_on = specs[i].dash_on;
        row.dash_off = specs[i].dash_off;
        if (session == nullptr) {
            row.caption = specs[i].name;
        } else {
            long newest = static_cast<long>(std::lround(sample_series(*session, specs[i], scale, 0)));
            if (specs[i].population) {
                newest = census;
            }
            const bool exact = !specs[i].cash || session->cash_flow_history_exact(scale, 0);
            row.caption = graph_legend_caption(specs[i].name, specs[i].legend, newest, exact);
        }
        out.push_back(row);
    }
    return out;
}

void draw_history_chart(const Cairo::RefPtr<Cairo::Context> &cr, CitySession *session, bool long_scale, int width,
                        int height)
{
    if (width < 20 || height < 20) {
        return;
    }
    cr->set_source_rgb(1, 1, 1);
    cr->rectangle(0, 0, width, height);
    cr->fill();
    const int left = 16;
    const int right = 16;
    const int top = 12;
    const int bottom = 12;
    const double plot_w = std::max(1, width - left - right);
    const double plot_h = std::max(1, height - top - bottom);
    const double plot_bottom = top + plot_h;
    cr->set_source_rgba(0, 0, 0, 0.18);
    cr->set_line_width(1);
    for (int i = 0; i <= 4; ++i) {
        const double y = top + plot_h * (i / 4.0);
        cr->move_to(left, y);
        cr->line_to(left + plot_w, y);
    }
    cr->stroke();
    cr->move_to(left, top);
    cr->line_to(left, plot_bottom);
    cr->line_to(left + plot_w, plot_bottom);
    cr->stroke();
    if (session == nullptr) {
        return;
    }
    const auto scale = long_scale ? CitySession::HistoryScale::Long : CitySession::HistoryScale::Short;
    SeriesSpec series[kGraphSeriesCount];
    fill_series(series);
    const int n = CitySession::kHistoryPoints;
    for (const SeriesSpec &item : series) {
        double min_v = sample_series(*session, item, scale, 0);
        double max_v = min_v;
        for (int i = 1; i < n; ++i) {
            const double value = sample_series(*session, item, scale, i);
            min_v = std::min(min_v, value);
            max_v = std::max(max_v, value);
        }
        const bool flat = max_v <= min_v;
        if (flat) {
            max_v = min_v + 1.0;
        }
        cr->set_source_rgb(item.r, item.g, item.b);
        cr->set_line_width(item.population ? 2.6 : 2.0);
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        apply_dash(cr, item.dash_on, item.dash_off);
        for (int i = 0; i < n; ++i) {
            const int index = (n - 1) - i;
            const double value = sample_series(*session, item, scale, index);
            const double x = left + (n == 1 ? 0 : (plot_w * i) / (n - 1));
            const double y = flat ? (top + plot_h / 2.0)
                                  : (plot_bottom - ((value - min_v) / (max_v - min_v)) * plot_h);
            if (i == 0) {
                cr->move_to(x, y);
            } else {
                cr->line_to(x, y);
            }
        }
        cr->stroke();
    }
}

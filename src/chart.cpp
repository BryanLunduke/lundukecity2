// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include <cstdint>
#include "chart.hpp"

#include "city_session.hpp"
#include "graph_legend.hpp"
#include "graph_palette.hpp"
#include "history_span.hpp"
#include "present.hpp"
#include "theme.hpp"

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

void apply_dash(cairo_t *cr, double on, double off)
{
    if (on <= 0.0 || off <= 0.0) {
        cairo_set_dash(cr, nullptr, 0, 0.0);
        return;
    }
    const double pattern[2] = {on, off};
    cairo_set_dash(cr, pattern, 2, 0.0);
}

std::int64_t population_at(const CitySession &session, CitySession::HistoryScale scale, int index)
{
    const int residential = session.history_value(CitySession::HistorySeries::Residential, scale, index);
    const int commercial = session.history_value(CitySession::HistorySeries::Commercial, scale, index);
    const int industrial = session.history_value(CitySession::HistorySeries::Industrial, scale, index);
    return static_cast<std::int64_t>(residential + commercial + industrial) * kHistoryPeoplePerSample;
}

void fill_series(SeriesSpec out[kGraphSeriesCount], bool night)
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
                            graph_channel(night ? style.dark_r : style.light_r),
                            graph_channel(night ? style.dark_g : style.light_g),
                            graph_channel(night ? style.dark_b : style.light_b),
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
    double value = 0.0;
    if (item.population) {
        value = static_cast<double>(population_at(session, scale, index));
    } else if (item.cash) {
        value = static_cast<double>(session.plot_cash_flow(scale, index));
    } else {
        value = session.history_value(item.history, scale, index);
    }
    // A scenario file stores 0 in the newest crime and pollution sample
    // until the first census. The live averages are what Evaluation shows.
    if (index == 0 && value <= 0.0) {
        if (item.history == CitySession::HistorySeries::Crime) {
            const int live = session.crime_average();
            if (live > 0) {
                return live;
            }
        }
        if (item.history == CitySession::HistorySeries::Pollution) {
            const int live = session.pollution_average();
            if (live > 0) {
                return live;
            }
        }
    }
    return value;
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
    fill_series(specs, chart_plate().night);
    const auto scale = long_scale ? CitySession::HistoryScale::Long : CitySession::HistoryScale::Short;
    std::vector<ChartLegend> out;
    const std::int64_t census = session != nullptr ? session->evaluation().population : 0;
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
            std::int64_t newest = static_cast<std::int64_t>(std::lround(sample_series(*session, specs[i], scale, 0)));
            if (specs[i].population) {
                newest = census;
            }
            const bool exact = !specs[i].cash || session->cash_flow_history_exact(scale, 0);
            const char *name = specs[i].name;
            std::string cash_name;
            if (specs[i].cash) {
                // A fresh short-scale sample of zero is the budget estimate,
                // not a month that has been collected. See plot_cash_flow.
                if (!long_scale && session->cash_flow_history(scale, 0) == 0) {
                    cash_name = "Cash flow, estimate";
                } else if (long_scale) {
                    cash_name = "Cash flow, last year";
                } else {
                    cash_name = "Cash flow, last month";
                }
                name = cash_name.c_str();
            }
            row.caption = graph_legend_caption(name, specs[i].legend, newest, exact);
        }
        out.push_back(row);
    }
    return out;
}

void draw_history_chart(cairo_t *cr, CitySession *session, bool long_scale, int width, int height, int enabled_mask,
                        int pointer_x, std::string *readout, float ui_scale)
{
    if (readout != nullptr) {
        readout->clear();
    }
    if (width < 20 || height < 20) {
        return;
    }
    const ChartPlate plate = chart_plate();
    cairo_set_source_rgb(cr, plate.paper_r, plate.paper_g, plate.paper_b);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    const float chart_scale = ui_scale > 0.05f ? ui_scale : 1.0f;
    auto label_px = [&](double design) {
        double px = design * static_cast<double>(chart_scale);
        if (px < 12.0) {
            px = 12.0;
        }
        return px;
    };
    const int axis_px = static_cast<int>(std::lround(label_px(10)));
    int enabled_count = 0;
    for (int mask_i = 0; mask_i < kGraphSeriesCount; ++mask_i) {
        if ((enabled_mask & (1 << mask_i)) != 0) {
            ++enabled_count;
        }
    }
    const int left = enabled_count <= 1 ? std::max(72, axis_px * 7) : std::max(44, axis_px * 4);
    const int right = 8;
    const int top = std::max(22, axis_px + 8);
    const int bottom = std::max(18, axis_px);
    const double plot_w = std::max(1, width - left - right);
    const double plot_h = std::max(1, height - top - bottom);
    const double plot_bottom = top + plot_h;
    cairo_set_line_width(cr, 1);
    for (int i = 0; i <= 4; ++i) {
        const double y = top + plot_h * (i / 4.0);
        if (i == 2) {
            // The middle of each series' own scale. Dashed so it is not
            // another history line. The legend names it.
            const double dash[2] = {4.0, 3.0};
            cairo_set_dash(cr, dash, 2, 0.0);
            cairo_set_source_rgba(cr, plate.grid_r, plate.grid_g, plate.grid_b, 0.7);
        } else {
            cairo_set_dash(cr, nullptr, 0, 0.0);
            cairo_set_source_rgba(cr, plate.grid_r, plate.grid_g, plate.grid_b, 0.35);
        }
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, left + plot_w, y);
        cairo_stroke(cr);
    }
    cairo_set_dash(cr, nullptr, 0, 0.0);
    cairo_set_source_rgb(cr, plate.ink_r, plate.ink_g, plate.ink_b);
    cairo_move_to(cr, left, top);
    cairo_line_to(cr, left, plot_bottom);
    cairo_line_to(cr, left + plot_w, plot_bottom);
    cairo_stroke(cr);
    if (session == nullptr) {
        return;
    }
    const auto scale = long_scale ? CitySession::HistoryScale::Long : CitySession::HistoryScale::Short;
    SeriesSpec series[kGraphSeriesCount];
    fill_series(series, plate.night);
    const int n = CitySession::kHistoryPoints;
    bool labeled = false;
    int hover_index = -1;
    if (pointer_x >= left && pointer_x <= left + plot_w && n > 1) {
        const double t = (static_cast<double>(pointer_x) - left) / plot_w;
        const int from_left = static_cast<int>(std::lround(t * (n - 1)));
        hover_index = (n - 1) - std::max(0, std::min(n - 1, from_left));
    }
    std::string hover;
    for (int s = 0; s < kGraphSeriesCount; ++s) {
        if ((enabled_mask & (1 << s)) == 0) {
            continue;
        }
        const SeriesSpec &item = series[s];
        double samples[CitySession::kHistoryPoints];
        for (int i = 0; i < n; ++i) {
            samples[i] = sample_series(*session, item, scale, i);
        }
        double min_v = samples[0];
        double max_v = samples[0];
        bool any = false;
        for (int i = 0; i < n; ++i) {
            if (history_point_skipped(samples, n, i)) {
                continue;
            }
            if (!any) {
                min_v = max_v = samples[i];
                any = true;
            } else {
                min_v = std::min(min_v, samples[i]);
                max_v = std::max(max_v, samples[i]);
            }
        }
        if (!any) {
            min_v = 0;
            max_v = 1;
        }
        const bool flat = max_v <= min_v;
        chart_headroom(min_v, max_v);
        if (!labeled) {
            labeled = true;
            cairo_set_source_rgb(cr, plate.ink_r, plate.ink_g, plate.ink_b);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, label_px(10));
            int enabled = 0;
            for (int mask_i = 0; mask_i < kGraphSeriesCount; ++mask_i) {
                if ((enabled_mask & (1 << mask_i)) != 0) {
                    ++enabled;
                }
            }
            auto axis_text = [&](double value) {
                const auto n = static_cast<long long>(std::llround(value));
                if (item.population) {
                    return grouped_number(n) + " people";
                }
                if (item.cash) {
                    return graph_money(n);
                }
                if (item.legend == GraphLegendKind::People) {
                    return grouped_number(static_cast<std::int64_t>(n) * kHistoryPeoplePerSample) + " people";
                }
                return grouped_number(n);
            };
            auto stamp = [&](const std::string &text, double y) {
                cairo_move_to(cr, 2, y);
                cairo_show_text(cr, text.c_str());
            };
            // Each line is scaled to itself. A population range on that axis
            // would describe only the first series.
            if (enabled == 1) {
                stamp(axis_text(max_v), top + 10);
                stamp(axis_text(min_v), plot_bottom);
            } else {
                stamp("high", top + 10);
                stamp("mid", top + plot_h * 0.5 + 4.0);
                stamp("low", plot_bottom);
            }
        }
        const bool pollution = item.history == CitySession::HistorySeries::Pollution;
        cairo_set_source_rgb(cr, item.r, item.g, item.b);
        cairo_set_line_width(cr, item.population ? 2.6 : pollution ? 2.8 : 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        apply_dash(cr, item.dash_on, item.dash_off);
        int kept = 0;
        double only_y = top + plot_h / 2.0;
        bool drawing = false;
        int oldest_kept = 0;
        for (int scan = n - 1; scan >= 0; --scan) {
            if (!history_point_skipped(samples, n, scan)) {
                oldest_kept = scan;
                break;
            }
        }
        for (int i = 0; i < n; ++i) {
            const int index = (n - 1) - i;
            if (history_point_skipped(samples, n, index)) {
                drawing = false;
                continue;
            }
            const double value = samples[index];
            const double x = history_plot_x(index, oldest_kept, left, plot_w);
            const double y = flat ? (top + plot_h / 2.0)
                                  : (plot_bottom - ((value - min_v) / (max_v - min_v)) * plot_h);
            only_y = y;
            ++kept;
            if (!drawing) {
                cairo_move_to(cr, x, y);
                drawing = true;
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        // One sample (a fresh city, or a history that is all zeros except
        // the newest reading) is a dot. Draw it as a line so Pollution is
        // visible when the city has not varied yet.
        if (kept == 1) {
            cairo_move_to(cr, left, only_y);
            cairo_line_to(cr, left + plot_w, only_y);
        }
        cairo_stroke(cr);
        if (hover_index >= 0 && !history_point_skipped(samples, n, hover_index)) {
            if (!hover.empty()) {
                hover += "  ";
            }
            const auto sample = static_cast<long long>(std::llround(samples[hover_index]));
            const char *brief = item.name;
            if (item.population) {
                brief = "Pop";
            } else if (item.name == kGraphSeries[1].name) {
                brief = "Res";
            } else if (item.name == kGraphSeries[2].name) {
                brief = "Com";
            } else if (item.name == kGraphSeries[3].name) {
                brief = "Ind";
            } else if (item.name == kGraphSeries[4].name) {
                brief = "Cash";
            }
            hover += brief;
            hover += " ";
            if (item.population) {
                hover += grouped_number(sample);
            } else if (item.cash) {
                hover += graph_money(sample);
            } else if (item.legend == GraphLegendKind::People) {
                hover += grouped_number(static_cast<std::int64_t>(sample) * kHistoryPeoplePerSample);
            } else {
                hover += grouped_number(sample);
            }
        }
    }
    if (hover_index >= 0 && pointer_x >= 0) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pointer_x, top);
        cairo_line_to(cr, pointer_x, plot_bottom);
        cairo_stroke(cr);
    }
    if (!hover.empty()) {
        cairo_save(cr);
        const double band = static_cast<double>(top - 1);
        cairo_rectangle(cr, 0, 0, width, band);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, plate.ink_r, plate.ink_g, plate.ink_b);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, label_px(11));
        std::string shown = hover;
        const double limit = static_cast<double>(width) - 8.0;
        auto fits = [&](const std::string &text) {
            cairo_text_extents_t ext;
            cairo_text_extents(cr, text.c_str(), &ext);
            return ext.x_advance <= limit;
        };
        if (!fits(shown)) {
            cairo_set_font_size(cr, label_px(9));
        }
        if (!fits(shown)) {
            const std::string dots = "...";
            while (shown.size() > 1 && !fits(shown + dots)) {
                shown.pop_back();
            }
            shown += dots;
        }
        cairo_move_to(cr, 4, label_px(11) + 3.0);
        cairo_show_text(cr, shown.c_str());
        cairo_restore(cr);
    }
    if (readout != nullptr) {
        *readout = hover;
    }
}

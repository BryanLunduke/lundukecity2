// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Text for the Evaluation and Budget overlays, and the query-dialog body.
// The wording matches the 0.9.5 gtkmm windows (evaluation_window.cpp and
// budget_window.cpp at f85446f).

#include <cstdint>
#include "city_session.hpp"
#include "graph_legend.hpp"

#include <algorithm>
#include <string>
#include <vector>

struct EvaluationCopy {
    std::string score_caption = "Score";
    std::string score;
    std::string score_delta;
    std::string opinion_caption = "Public opinion";
    std::string opinion_question = "Is the mayor doing a good job?";
    std::string yes;
    std::string no;
    // -1 when there is no poll yet.
    int yes_percent = -1;
    std::string opinion_bar;
    std::string problems_caption = "Worst problems";
    std::vector<std::string> problems;
    std::string stats_caption = "Statistics";
    std::string population;
    std::string migration;
    std::string assessed;
    std::string category;
    std::string difficulty;
    std::string scenario;
};

inline EvaluationCopy evaluation_copy(const CitySession::Evaluation &report, const std::string &scenario_progress)
{
    EvaluationCopy copy;
    if (report.no_residents) {
        copy.score = "Score: not rated yet";
        copy.score_delta = "The city is evaluated once people move in.";
        copy.yes = "Yes: —";
        copy.no = "No: —";
        copy.yes_percent = -1;
        copy.opinion_bar = "No poll yet";
        copy.problems.push_back("No complaints yet — nobody lives here.");
        copy.population = "Population: 0";
        copy.migration = "Net migration (last year): 0";
    } else {
        copy.score = "Score: " + std::to_string(report.score);
        if (report.score_delta > 0) {
            copy.score_delta = "Change since the last evaluation: +" + std::to_string(report.score_delta);
        } else if (report.score_delta < 0) {
            copy.score_delta = "Change since the last evaluation: " + std::to_string(report.score_delta);
        } else {
            copy.score_delta = "Change since the last evaluation: none";
        }
        const int no_percent = 100 - report.yes_percent;
        copy.yes = "Yes: " + std::to_string(report.yes_percent) + "%";
        copy.no = "No: " + std::to_string(no_percent) + "%";
        copy.yes_percent = report.yes_percent;
        copy.opinion_bar = "Yes " + std::to_string(report.yes_percent) + "%";
        if (report.problems.empty()) {
            copy.problems.push_back("No complaints recorded.");
        } else {
            const int shown = std::min(4, static_cast<int>(report.problems.size()));
            for (int i = 0; i < shown; ++i) {
                const CitySession::Problem &problem = report.problems[static_cast<std::size_t>(i)];
                copy.problems.push_back(problem.name + ": " + std::to_string(problem.votes) + "%");
            }
        }
        copy.population = "Population: " + grouped_number(report.population);
        copy.migration = "Net migration (last year): " + grouped_number(report.migration);
    }
    copy.assessed = "Assessed value: " + graph_money(report.assessed_value);
    copy.category = "Category: " + report.category;
    copy.difficulty = "Difficulty: " + report.difficulty;
    copy.scenario = scenario_progress;
    return copy;
}

struct BudgetCopy {
    std::string taxes;
    std::string previous_funds;
    std::string cash_flow;
    std::string current_funds;
    std::string closing_note;
    std::string tax_title;
    std::string tax_value;
    std::string road;
    std::string police;
    std::string fire;
};

inline BudgetCopy budget_copy(const CitySession::BudgetBook &book, bool tax_year)
{
    BudgetCopy copy;
    if (tax_year) {
        copy.taxes = "Taxes collected: " + graph_money(book.taxes);
        copy.closing_note = "This year is collected when the window closes.";
        copy.tax_title = "Tax rate";
    } else if (book.estimates || !book.taxes_known) {
        copy.taxes = "Estimated taxes, not yet collected: " + graph_money(book.taxes);
        copy.tax_title = "Next year's tax rate";
        if (book.estimates) {
            copy.closing_note = "The first tax year has not been collected yet. These figures are estimates.";
        }
    } else {
        copy.taxes = "Last January's taxes: " + graph_money(book.taxes);
        copy.tax_title = "Next year's tax rate";
    }
    const std::string flow = (book.cash_flow > 0 ? "+" : "") + graph_money(book.cash_flow);
    copy.cash_flow = std::string(book.estimates ? "Estimated cash flow: " : "Cash flow: ") + flow;
    copy.previous_funds = "Previous funds: " + graph_money(book.previous_funds);
    copy.current_funds = "Current funds: " + graph_money(book.funds);
    copy.tax_value = std::to_string(book.tax_percent) + "%";
    const char *request = book.estimates ? "Estimated request " : "Request ";
    auto funding_line = [request](std::int64_t need, int percent, std::int64_t spent, const std::string &note) {
        if (note.empty()) {
            return std::string(request) + graph_money(need) + "    " + std::to_string(percent) + "% = " +
                   graph_money(spent);
        }
        return std::string(request) + graph_money(need) + "    " + note + " = " + graph_money(spent);
    };
    copy.road = funding_line(book.road_need, book.road_percent, book.road_spent, book.road_note);
    copy.police = funding_line(book.police_need, book.police_percent, book.police_spent, book.police_note);
    copy.fire = funding_line(book.fire_need, book.fire_percent, book.fire_spent, book.fire_note);
    return copy;
}

// The query message uses an em dash (" — ", five UTF-8 bytes) and commas as
// line breaks. Comparing three bytes leaves the dash on the first line.
inline std::string query_body(const std::string &text)
{
    const std::string dash = " — ";
    std::string body;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, dash.size(), dash) == 0) {
            body.push_back('\n');
            i += dash.size();
            continue;
        }
        if (text.compare(i, 2, ", ") == 0) {
            body.push_back('\n');
            i += 2;
            continue;
        }
        body.push_back(text[i]);
        ++i;
    }
    return body;
}

// Which pending action the save prompt is guarding, and which file Replace
// would overwrite. The city name is quoted when we have one.
enum class SavePromptKind { Continue, Quit, NewCity, Scenario, Open, Recover };

inline std::string save_changes_question(SavePromptKind kind, const std::string &city)
{
    const std::string who = city.empty() ? std::string("this city") : "\"" + city + "\"";
    switch (kind) {
    case SavePromptKind::Quit:
        return "Save changes to " + who + " before quitting?";
    case SavePromptKind::NewCity:
        return "Save changes to " + who + " before starting a new city?";
    case SavePromptKind::Scenario:
        return "Save changes to " + who + " before playing a scenario?";
    case SavePromptKind::Open:
        return "Save changes to " + who + " before opening another city?";
    case SavePromptKind::Recover:
        return "Save changes to " + who + " before recovering another city?";
    case SavePromptKind::Continue:
        break;
    }
    return "Save changes to " + who + " before continuing?";
}

inline std::string replace_file_question(const std::string &filename)
{
    if (filename.empty()) {
        return "A city file with that name already exists. Replace it?";
    }
    return "Replace \"" + filename + "\"? The city saved there will be overwritten.";
}

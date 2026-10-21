// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// Evaluation and Budget strings restored from the 0.9.5 windows, plus the
// query-dialog em dash split.

#include "dialog_copy.hpp"

#include <iostream>
#include <string>

namespace {

int fails = 0;

void expect(bool ok, const std::string &detail)
{
    if (ok) {
        return;
    }
    std::cerr << detail << "\n";
    ++fails;
}

void expect_has(const std::string &text, const char *needle)
{
    expect(text.find(needle) != std::string::npos, std::string("missing \"") + needle + "\" in: " + text);
}

} // namespace

int main()
{
    CitySession::Evaluation empty;
    empty.no_residents = true;
    empty.population = 0;
    empty.category = "Village";
    empty.difficulty = "Easy";
    empty.assessed_value = 0;
    const EvaluationCopy vacant = evaluation_copy(empty, "");
    expect_has(vacant.score, "not rated yet");
    expect_has(vacant.score_delta, "once people move in");
    expect(vacant.opinion_question == "Is the mayor doing a good job?", "poll question");
    expect(vacant.opinion_caption == "Public opinion", "opinion caption");
    expect(vacant.problems_caption == "Worst problems", "problems caption");
    expect(vacant.stats_caption == "Statistics", "statistics caption");
    expect_has(vacant.yes, "Yes:");
    expect_has(vacant.no, "No:");
    expect(vacant.yes_percent < 0, "no poll percent");
    expect(!vacant.problems.empty(), "empty-city problem line");
    expect_has(vacant.problems[0], "nobody lives here");
    expect_has(vacant.migration, "Net migration");
    expect_has(vacant.assessed, "Assessed value:");
    expect_has(vacant.category, "Category:");
    expect_has(vacant.difficulty, "Difficulty:");

    CitySession::Evaluation full;
    full.score = 512;
    full.score_delta = 12;
    full.yes_percent = 62;
    full.population = 18060;
    full.migration = -40;
    full.assessed_value = 125000;
    full.category = "Town";
    full.difficulty = "Hard";
    full.problems.push_back(CitySession::Problem{"Crime", 18});
    full.problems.push_back(CitySession::Problem{"Taxes", 9});
    const EvaluationCopy live = evaluation_copy(full, "Reach 10,000 people. 4 years left.");
    expect(live.score == "Score: 512", live.score);
    expect_has(live.score_delta, "Change since the last evaluation: +12");
    expect(live.yes == "Yes: 62%", live.yes);
    expect(live.no == "No: 38%", live.no);
    expect(live.yes_percent == 62, "yes percent");
    expect_has(live.population, "18,060");
    expect_has(live.migration, "-40");
    expect_has(live.assessed, "$125,000");
    expect_has(live.category, "Town");
    expect_has(live.difficulty, "Hard");
    expect(live.problems.size() == 2 && live.problems[0] == "Crime: 18%", "problem votes");
    expect_has(live.scenario, "4 years left");

    CitySession::BudgetBook book;
    book.taxes = 1500;
    book.previous_funds = 20000;
    book.funds = 20750;
    book.cash_flow = 750;
    book.tax_percent = 7;
    book.road_percent = 100;
    book.police_percent = 80;
    book.fire_percent = 100;
    book.road_need = 400;
    book.police_need = 200;
    book.fire_need = 150;
    book.road_spent = 400;
    book.police_spent = 160;
    book.fire_spent = 150;
    book.estimates = true;
    book.taxes_known = false;
    const BudgetCopy estimated = budget_copy(book, false);
    expect_has(estimated.taxes, "Estimated taxes, not yet collected:");
    expect_has(estimated.taxes, "$1,500");
    expect_has(estimated.previous_funds, "Previous funds:");
    expect_has(estimated.previous_funds, "$20,000");
    expect_has(estimated.current_funds, "Current funds:");
    expect_has(estimated.cash_flow, "Estimated cash flow:");
    expect_has(estimated.cash_flow, "+$750");
    expect(estimated.tax_title == "Next year's tax rate", estimated.tax_title);
    expect_has(estimated.closing_note, "estimates");
    expect_has(estimated.road, "Estimated request ");
    expect_has(estimated.road, "$400");
    expect_has(estimated.police, "80%");
    expect_has(estimated.fire, "$150");

    book.estimates = false;
    book.taxes_known = true;
    book.road_note = "cut to 40% because taxes ($100) plus the $50 on hand cannot cover 100%";
    const BudgetCopy collected = budget_copy(book, true);
    expect_has(collected.taxes, "Taxes collected:");
    expect(collected.tax_title == "Tax rate", collected.tax_title);
    expect_has(collected.closing_note, "collected when the window closes");
    expect_has(collected.road, "cut to 40%");
    expect_has(collected.cash_flow, "Cash flow:");

    const std::string body = query_body("Residential zone — population 120, land value 40, powered");
    expect(body.find(" — ") == std::string::npos, "em dash stayed on the first line: " + body);
    expect(body.find("Residential zone\n") == 0, "heading split: " + body);
    expect(body.find("\npopulation 120\n") != std::string::npos, "comma split: " + body);

    if (fails != 0) {
        std::cerr << fails << " dialog copy checks failed\n";
        return 1;
    }
    std::cout << "dialog copy ok\n";
    return 0;
}

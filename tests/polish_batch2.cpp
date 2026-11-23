// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "city_seed.hpp"
#include "mayor_notes.hpp"
#include "scenario_briefing.hpp"
#include "scenario_scorebook.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

std::string make_temp_dir()
{
    const auto stamp = std::filesystem::temp_directory_path() / "lunduke-scorebook-test";
    std::error_code ec;
    std::filesystem::remove_all(stamp, ec);
    std::filesystem::create_directories(stamp, ec);
    if (ec) {
        return {};
    }
    return stamp.string();
}

void remove_temp_dir(const std::string &dir)
{
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

} // namespace

int main()
{
    const std::string dir = make_temp_dir();
    if (dir.empty()) {
        return fail("could not create a temp scorebook directory");
    }

    // A missing file is an empty book, not an error.
    if (!load_scorebook(dir).empty()) {
        remove_temp_dir(dir);
        return fail("missing scorebook should be empty");
    }
    if (scorebook_summary(scorebook_find({}, 1)) != "Not played") {
        remove_temp_dir(dir);
        return fail("unknown scenario should say not played");
    }

    // A win records best score, earliest win month, and seed.
    if (!scorebook_record_file(dir, 1, true, 620, 1925, 5, 4242)) {
        remove_temp_dir(dir);
        return fail("first win did not save");
    }
    ScenarioScore dullsville = scorebook_find(load_scorebook(dir), 1);
    if (dullsville.wins != 1 || dullsville.losses != 0 || !dullsville.have_score || dullsville.best_score != 620 ||
        !dullsville.have_win || dullsville.earliest_win_year != 1925 || dullsville.earliest_win_month != 5 ||
        !dullsville.have_seed || dullsville.seed != 4242) {
        remove_temp_dir(dir);
        return fail("first win fields are wrong");
    }
    const std::string first = scorebook_summary(dullsville);
    if (first.find("Best 620") == std::string::npos || first.find("Won Jun 1925") == std::string::npos ||
        first.find("Seed 4242") == std::string::npos) {
        remove_temp_dir(dir);
        std::fprintf(stderr, "summary: %s\n", first.c_str());
        return fail("win summary is wrong");
    }

    // A later higher score updates the best and the seed; a later year does
    // not replace the earliest win month.
    if (!scorebook_record_file(dir, 1, true, 800, 1930, 0, 99)) {
        remove_temp_dir(dir);
        return fail("second win did not save");
    }
    dullsville = scorebook_find(load_scorebook(dir), 1);
    if (dullsville.wins != 2 || dullsville.best_score != 800 || dullsville.earliest_win_year != 1925 ||
        dullsville.earliest_win_month != 5 || dullsville.seed != 99) {
        remove_temp_dir(dir);
        return fail("best score should update without losing the earliest win");
    }

    // An earlier win month is kept even when the score is lower. The seed
    // stays with the best score.
    if (!scorebook_record_file(dir, 1, true, 500, 1920, 2, 7)) {
        remove_temp_dir(dir);
        return fail("earlier win did not save");
    }
    dullsville = scorebook_find(load_scorebook(dir), 1);
    if (dullsville.wins != 3 || dullsville.best_score != 800 || dullsville.earliest_win_year != 1920 ||
        dullsville.earliest_win_month != 2 || dullsville.seed != 99) {
        remove_temp_dir(dir);
        return fail("earliest win month should move; seed should stay with the best score");
    }

    // A loss is recorded too, and does not invent a win month.
    if (!scorebook_record_file(dir, 2, false, 310, 1910, 4, 12)) {
        remove_temp_dir(dir);
        return fail("loss did not save");
    }
    const ScenarioScore hamburg = scorebook_find(load_scorebook(dir), 2);
    if (hamburg.wins != 0 || hamburg.losses != 1 || !hamburg.have_score || hamburg.best_score != 310 ||
        hamburg.have_win || hamburg.seed != 12) {
        remove_temp_dir(dir);
        return fail("loss fields are wrong");
    }
    const std::string lost = scorebook_summary(hamburg);
    if (lost.find("Best 310") == std::string::npos || lost.find("No win yet") == std::string::npos ||
        lost.find("Seed 12") == std::string::npos) {
        remove_temp_dir(dir);
        std::fprintf(stderr, "loss summary: %s\n", lost.c_str());
        return fail("loss summary is wrong");
    }

    // A later loss with a lower score does not clobber the best.
    scorebook_record_file(dir, 2, false, 200, 1911, 0, 1);
    if (scorebook_find(load_scorebook(dir), 2).best_score != 310) {
        remove_temp_dir(dir);
        return fail("a lower loss should not replace the best score");
    }

    // Extra columns and comments are ignored (forward/backward compatible).
    {
        std::ofstream out(scorebook_path(dir), std::ios::trunc);
        out << "# comment\n";
        out << "3 1 0 540 1949 11 77 extra-column\n";
        out << "not-a-row\n";
        out << "0 1 0 1 1900 0 0\n";
    }
    const ScenarioScore hamburg_kept = scorebook_find(load_scorebook(dir), 2);
    // The rewrite above replaced the file, so id 2 is gone; id 3 is kept.
    if (hamburg_kept.id != 0) {
        remove_temp_dir(dir);
        return fail("replaced file should not keep the old Hamburg row");
    }
    const ScenarioScore bern = scorebook_find(load_scorebook(dir), 3);
    if (bern.wins != 1 || bern.best_score != 540 || bern.earliest_win_year != 1949 || bern.earliest_win_month != 11 ||
        bern.seed != 77) {
        remove_temp_dir(dir);
        return fail("extra-column row should still load");
    }

    // An older four-field line is skipped, not treated as a crash.
    {
        std::ofstream out(scorebook_path(dir), std::ios::trunc);
        out << "4 2 1 400\n";
        out << "5 1 0 501 1962 0 8\n";
    }
    const auto mixed = load_scorebook(dir);
    if (scorebook_find(mixed, 4).id != 0 || scorebook_find(mixed, 5).best_score != 501) {
        remove_temp_dir(dir);
        return fail("short lines should be skipped; complete lines should load");
    }

    // Difficulty tags (display only) follow the documented goal+deadline rule.
    auto expect_tag = [](int years, const char *goal, const char *want, const char *name) -> int {
        const char *got = scenario_difficulty_tag(years, goal);
        if (std::string(got) != want) {
            std::fprintf(stderr, "%s: got %s want %s\n", name, got, want);
            return 1;
        }
        return 0;
    };
    if (expect_tag(30, "Turn this city into a Metropolis (more than 100,000 people) within 30 years.", "Easy",
                   "Dullsville") ||
        expect_tag(5, "Keep the city a Metropolis (over 100,000) until 1911 despite the quake.", "Hard",
                   "San Francisco") ||
        expect_tag(5, "Keep the city a Metropolis (over 100,000) until 1949 despite the fire.", "Hard", "Hamburg") ||
        expect_tag(10, "Bring average traffic below 40 and keep at least 90,000 people within 10 years.", "Medium",
                   "Bern") ||
        expect_tag(5, "Keep the city score above 500 until 1962.", "Hard", "Tokyo") ||
        expect_tag(10, "Bring average crime below 60 within 10 years.", "Medium", "Detroit") ||
        expect_tag(5, "Raise the score above 500 by 2015.", "Hard", "Boston") ||
        expect_tag(10, "Keep the city score above 500 until 2057.", "Hard", "Rio") ||
        expect_tag(5, "Clear the Autonomous Zone and bring average crime below 60 by mid-2025.", "Hard", "Seattle") ||
        expect_tag(2, "Round up the Alcatraz escapees and bring average crime below 45 by mid-1964.", "Hard",
                   "San Francisco 1962") ||
        expect_tag(1, "Put out the riot fires and bring average crime below 60 by summer 2021.", "Hard", "Portland") ||
        expect_tag(2, "Have the police catch the lion within four months and keep at least 100,000 people by spring 2027.",
                   "Hard", "San Diego")) {
        remove_temp_dir(dir);
        return fail("scenario difficulty tag derivation");
    }
    if (scenario_years_label(1) != "1 year" || scenario_years_label(30) != "30 years") {
        remove_temp_dir(dir);
        return fail("years label");
    }

    // Mid-year mayor's notes: one per quarter, and only a real top complaint.
    if (mayor_note_stamp(1901, 3) != 1901 * 12 + 3) {
        remove_temp_dir(dir);
        return fail("mayor note stamp");
    }
    if (!mayor_note_due(100, -1) || mayor_note_due(100, 98) || !mayor_note_due(100, 97)) {
        remove_temp_dir(dir);
        return fail("mayor note should wait about a quarter");
    }
    if (mayor_note_has_complaint(0, 9, false) || mayor_note_has_complaint(0, 10, true) ||
        mayor_note_has_complaint(-1, 40, false) || !mayor_note_has_complaint(0, 10, false)) {
        remove_temp_dir(dir);
        return fail("mayor note should need a real top complaint");
    }
    const std::string note = mayor_note_text(0);
    if (!mayor_note_is(note) || note.find("Crime") == std::string::npos ||
        note.find("Tip: put police stations near homes.") == std::string::npos) {
        remove_temp_dir(dir);
        std::fprintf(stderr, "note: %s\n", note.c_str());
        return fail("mayor note should reuse civic advice_for copy");
    }

    // Seed field: live validation as you type, with the rule as a way out.
    {
        bool ok = false;
        if (std::string(city_seed_live_message("", ok)).find("New terrain") == std::string::npos || !ok) {
            remove_temp_dir(dir);
            return fail("blank seed should be valid and mention New terrain");
        }
        if (std::string(city_seed_live_message("auto", ok)).find("new seed") == std::string::npos || !ok) {
            remove_temp_dir(dir);
            return fail("auto seed should be valid");
        }
        if (std::string(city_seed_live_message("  42 ", ok)).find("This seed will be used") == std::string::npos || !ok) {
            remove_temp_dir(dir);
            return fail("a whole number should be accepted as you type");
        }
        if (city_seed_live_message("12ab", ok) == nullptr || ok) {
            remove_temp_dir(dir);
            return fail("a typo should be rejected as you type");
        }
        if (std::string(city_seed_live_message("12ab", ok)).find("New terrain") == std::string::npos) {
            remove_temp_dir(dir);
            return fail("a typo should still point at New terrain");
        }
        if (city_seed_live_message("1000000000", ok) != nullptr && ok) {
            remove_temp_dir(dir);
            return fail("a number over 999999999 should be rejected");
        }
        if (std::string(city_seed_rule_help()).find("New terrain") == std::string::npos) {
            remove_temp_dir(dir);
            return fail("the inline rule should mention New terrain");
        }
    }

    remove_temp_dir(dir);
    return 0;
}

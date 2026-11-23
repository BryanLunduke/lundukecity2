// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

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

    remove_temp_dir(dir);
    return 0;
}

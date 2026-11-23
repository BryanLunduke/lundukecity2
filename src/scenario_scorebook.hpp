// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

#include "atomic_write.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Local scenario scorebook in the config/save directory (typically
// ~/.config/lunduke-city-2/scorebook, or %APPDATA%\lunduke-city-2\scorebook
// on Windows). A missing file is an empty book. Comment lines, unknown
// rows, and extra columns are ignored, so a newer book still loads in an
// older build and an older book still loads here.

struct ScenarioScore {
    int id = 0;
    int wins = 0;
    int losses = 0;
    int best_score = 0;
    bool have_score = false;
    int earliest_win_year = 0;
    // Calendar month of the earliest win, 0 = January .. 11 = December.
    int earliest_win_month = 0;
    bool have_win = false;
    int seed = 0;
    bool have_seed = false;
};

inline std::string scorebook_path(const std::string &dir)
{
    if (dir.empty()) {
        return {};
    }
    const char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + "scorebook";
    }
    return dir + "/scorebook";
}

inline std::vector<ScenarioScore> load_scorebook(const std::string &dir)
{
    std::vector<ScenarioScore> book;
    const std::string path = scorebook_path(dir);
    if (path.empty()) {
        return book;
    }
    std::ifstream in(path);
    if (!in) {
        return book;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream(line);
        ScenarioScore row;
        int wins = 0;
        int losses = 0;
        int best = 0;
        int year = 0;
        int month = 0;
        int seed = 0;
        if (!(stream >> row.id >> wins >> losses >> best >> year >> month >> seed) || row.id <= 0) {
            continue;
        }
        row.wins = std::max(0, wins);
        row.losses = std::max(0, losses);
        row.best_score = best;
        row.have_score = (row.wins > 0 || row.losses > 0);
        if (row.wins > 0 && year > 0) {
            row.earliest_win_year = year;
            row.earliest_win_month = month;
            if (row.earliest_win_month < 0 || row.earliest_win_month > 11) {
                row.earliest_win_month = 0;
            }
            row.have_win = true;
        }
        row.seed = seed;
        row.have_seed = true;
        bool found = false;
        for (ScenarioScore &entry : book) {
            if (entry.id == row.id) {
                entry = row;
                found = true;
                break;
            }
        }
        if (!found) {
            book.push_back(row);
        }
    }
    return book;
}

inline bool save_scorebook(const std::string &dir, const std::vector<ScenarioScore> &book)
{
    const std::string path = scorebook_path(dir);
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }
    std::ostringstream body;
    body << "# lunduke-city-2 scenario scorebook v1\n";
    body << "# id wins losses best_score earliest_win_year earliest_win_month seed\n";
    for (const ScenarioScore &row : book) {
        if (row.id <= 0) {
            continue;
        }
        body << row.id << ' ' << row.wins << ' ' << row.losses << ' ' << row.best_score << ' '
             << row.earliest_win_year << ' ' << row.earliest_win_month << ' ' << row.seed << '\n';
    }
    return write_text_atomic(path, body.str());
}

inline ScenarioScore scorebook_find(const std::vector<ScenarioScore> &book, int id)
{
    for (const ScenarioScore &row : book) {
        if (row.id == id) {
            return row;
        }
    }
    return {};
}

inline void scorebook_record(std::vector<ScenarioScore> &book, int id, bool won, int score, int year, int month,
                             int seed)
{
    if (id <= 0) {
        return;
    }
    if (month < 0) {
        month = 0;
    }
    if (month > 11) {
        month = 11;
    }
    ScenarioScore *row = nullptr;
    for (ScenarioScore &entry : book) {
        if (entry.id == id) {
            row = &entry;
            break;
        }
    }
    if (row == nullptr) {
        book.push_back({});
        row = &book.back();
        row->id = id;
    }
    if (won) {
        row->wins += 1;
        if (!row->have_win || year < row->earliest_win_year ||
            (year == row->earliest_win_year && month < row->earliest_win_month)) {
            row->earliest_win_year = year;
            row->earliest_win_month = month;
            row->have_win = true;
        }
    } else {
        row->losses += 1;
    }
    if (!row->have_score || score > row->best_score) {
        row->best_score = score;
        row->have_score = true;
        row->seed = seed;
        row->have_seed = true;
    } else if (!row->have_seed) {
        row->seed = seed;
        row->have_seed = true;
    }
}

inline bool scorebook_record_file(const std::string &dir, int id, bool won, int score, int year, int month, int seed)
{
    std::vector<ScenarioScore> book = load_scorebook(dir);
    scorebook_record(book, id, won, score, year, month, seed);
    return save_scorebook(dir, book);
}

inline const char *scorebook_month_name(int month)
{
    static const char *names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 0 || month > 11) {
        return names[0];
    }
    return names[month];
}

inline std::string scorebook_summary(const ScenarioScore &row)
{
    if (row.id <= 0 || (!row.have_score && row.wins == 0 && row.losses == 0)) {
        return "Not played";
    }
    std::string text;
    if (row.have_score) {
        text = "Best " + std::to_string(row.best_score);
    }
    if (row.have_win) {
        if (!text.empty()) {
            text += "  ·  ";
        }
        text += std::string("Won ") + scorebook_month_name(row.earliest_win_month) + " " +
                std::to_string(row.earliest_win_year);
    } else if (row.losses > 0) {
        if (!text.empty()) {
            text += "  ·  ";
        }
        text += "No win yet";
    }
    if (row.have_seed) {
        if (!text.empty()) {
            text += "  ·  ";
        }
        text += "Seed " + std::to_string(row.seed);
    }
    return text;
}

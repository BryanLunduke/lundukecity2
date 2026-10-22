// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// The classic .cty writer clears the scenario id and the score clock.
// The clock lives in a sidecar next to the city file (name.cty.json) so
// loading a save can put the same scenario back.

#include <cstdlib>
#include <string>

struct ScenarioClock {
    bool present = false;
    int scenario = 0;
    int score_wait = 0;
    int score_type = 0;
    int outcome = 0;
    int month = 0;
    int year = 1900;
};

inline std::string scenario_sidecar_path(const std::string &city_path)
{
    return city_path + ".json";
}

inline std::string scenario_sidecar_text(const ScenarioClock &clock)
{
    std::string out = "{\n";
    out += "  \"scenario\": " + std::to_string(clock.scenario) + ",\n";
    out += "  \"score_wait\": " + std::to_string(clock.score_wait) + ",\n";
    out += "  \"score_type\": " + std::to_string(clock.score_type) + ",\n";
    out += "  \"outcome\": " + std::to_string(clock.outcome) + ",\n";
    out += "  \"month\": " + std::to_string(clock.month) + ",\n";
    out += "  \"year\": " + std::to_string(clock.year) + "\n";
    out += "}\n";
    return out;
}

inline int sidecar_json_int(const std::string &text, const char *key, bool &ok)
{
    const std::string token = std::string("\"") + key + "\"";
    const auto at = text.find(token);
    if (at == std::string::npos) {
        ok = false;
        return 0;
    }
    std::size_t i = at + token.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
        ++i;
    }
    if (i >= text.size() || text[i] != ':') {
        ok = false;
        return 0;
    }
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    ok = i < text.size() && (text[i] == '-' || (text[i] >= '0' && text[i] <= '9'));
    if (!ok) {
        return 0;
    }
    return std::atoi(text.c_str() + i);
}

inline bool scenario_sidecar_parse(const std::string &text, ScenarioClock &clock)
{
    if (text.find('{') != std::string::npos) {
        ScenarioClock parsed;
        bool scenario_ok = false;
        bool wait_ok = false;
        bool type_ok = false;
        bool outcome_ok = false;
        bool month_ok = false;
        bool year_ok = false;
        parsed.scenario = sidecar_json_int(text, "scenario", scenario_ok);
        parsed.score_wait = sidecar_json_int(text, "score_wait", wait_ok);
        parsed.score_type = sidecar_json_int(text, "score_type", type_ok);
        parsed.outcome = sidecar_json_int(text, "outcome", outcome_ok);
        parsed.month = sidecar_json_int(text, "month", month_ok);
        parsed.year = sidecar_json_int(text, "year", year_ok);
        if (!scenario_ok && !wait_ok && !type_ok && !outcome_ok) {
            return false;
        }
        (void)month_ok;
        (void)year_ok;
        parsed.present = true;
        clock = parsed;
        return true;
    }
    ScenarioClock parsed;
    bool any = false;
    std::string line;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const auto eq = line.find('=');
            if (eq != std::string::npos && eq > 0) {
                const std::string key = line.substr(0, eq);
                const int value = std::atoi(line.substr(eq + 1).c_str());
                if (key == "scenario") {
                    parsed.scenario = value;
                    any = true;
                } else if (key == "score_wait") {
                    parsed.score_wait = value;
                    any = true;
                } else if (key == "score_type") {
                    parsed.score_type = value;
                    any = true;
                } else if (key == "outcome") {
                    parsed.outcome = value;
                    any = true;
                } else if (key == "month") {
                    parsed.month = value;
                } else if (key == "year") {
                    parsed.year = value;
                }
            }
            line.clear();
        } else {
            line.push_back(text[i]);
        }
    }
    if (!any) {
        return false;
    }
    parsed.present = true;
    clock = parsed;
    return true;
}

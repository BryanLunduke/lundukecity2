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
    std::string out;
    out += "scenario=" + std::to_string(clock.scenario) + "\n";
    out += "score_wait=" + std::to_string(clock.score_wait) + "\n";
    out += "score_type=" + std::to_string(clock.score_type) + "\n";
    out += "outcome=" + std::to_string(clock.outcome) + "\n";
    out += "month=" + std::to_string(clock.month) + "\n";
    out += "year=" + std::to_string(clock.year) + "\n";
    return out;
}

inline bool scenario_sidecar_parse(const std::string &text, ScenarioClock &clock)
{
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

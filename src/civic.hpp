// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Civic ledger, evaluation advice, and the year-end card. Recognition only:
// nothing here changes prices, coverage, or which buildings can be bought.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace civic {

inline constexpr int kMilestones = 8;
inline constexpr int kProblems = 7;

enum MilestoneId {
    Powered = 0,
    Balanced,
    Traffic,
    Town,
    City,
    Capital,
    Metropolis,
    Megalopolis
};

struct MilestoneHit {
    bool earned = false;
    int year = 1900;
    int month = 0;
};

struct Ledger {
    MilestoneHit hit[kMilestones]{};
    bool have_prior = false;
    int prior_votes[kProblems]{};
    std::int64_t prior_population = 0;

    bool any() const
    {
        if (have_prior) {
            return true;
        }
        for (const MilestoneHit &one : hit) {
            if (one.earned) {
                return true;
            }
        }
        return false;
    }
};

struct Advice {
    const char *title = "";
    const char *body = "";
    const char *intention = "";
    const char *show = "";
    const char *alt = "";
    // CitySession::MapLayer ordinals. -1 when there is no map for it.
    int layer = -1;
    int alt_layer = -1;
};

struct YearCard {
    std::string opening_label;
    std::string opening_value;
    std::string closing_label;
    std::string closing_value;
    std::string population;
    std::string improvement;
    std::string problem;
    std::string intention;
    bool closing_when_collected = false;
    bool estimated = false;
};

inline const char *month_name(int month)
{
    static const char *names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 0 || month > 11) {
        return "Jan";
    }
    return names[month];
}

inline const char *milestone_title(int id)
{
    switch (id) {
    case Powered:
        return "A powered neighborhood";
    case Balanced:
        return "A balanced year";
    case Traffic:
        return "Traffic eased";
    case Town:
        return "Town";
    case City:
        return "City";
    case Capital:
        return "Capital";
    case Metropolis:
        return "Metropolis";
    case Megalopolis:
        return "Megalopolis";
    default:
        return "Milestone";
    }
}

inline const char *milestone_card(int id)
{
    switch (id) {
    case Powered:
        return "The first homes have power and people in them.";
    case Balanced:
        return "This January the books closed without a loss.";
    case Traffic:
        return "Traffic complaints are lower than last year. The poll does not name a cause.";
    case Town:
        return "The census calls the city a town.";
    case City:
        return "The census calls the city a city.";
    case Capital:
        return "The census calls the city a capital.";
    case Metropolis:
        return "The census calls the city a metropolis.";
    case Megalopolis:
        return "The census calls the city a megalopolis.";
    default:
        return "";
    }
}

inline std::string milestone_date(const MilestoneHit &hit)
{
    if (!hit.earned) {
        return "Not yet";
    }
    return std::string(month_name(hit.month)) + " " + std::to_string(hit.year);
}

inline Advice advice_for(int problem)
{
    Advice advice;
    switch (problem) {
    case 0:
        advice.title = "Crime";
        advice.body = "Crime is the complaint people raise most often. It tends to be higher where homes sit "
                      "outside police coverage, though the poll does not prove one cause. Look for occupied "
                      "blocks far from a station.";
        advice.intention = "Next year, compare the crime map with police coverage before changing the police budget.";
        advice.show = "Crime map";
        advice.alt = "Police coverage";
        advice.layer = 3;
        advice.alt_layer = 7;
        break;
    case 1:
        advice.title = "Pollution";
        advice.body = "Pollution is the largest share of the complaints. Industry, traffic, and power plants often "
                      "raise it, but this percentage is not a map of one smokestack. The pollution map shows where "
                      "the reading is high.";
        advice.intention = "Next year, look at the pollution map before siting more industry.";
        advice.show = "Pollution map";
        advice.layer = 2;
        break;
    case 2:
        advice.title = "Housing";
        advice.body = "Housing complaints usually mean more people want homes than there are places to live. The "
                      "poll is city-wide, not a verdict on one block. The residential map and the demand meter show "
                      "where more homes would matter.";
        advice.intention = "Next year, watch residential demand before zoning more housing.";
        advice.show = "Residential map";
        advice.layer = 11;
        break;
    case 3:
        advice.title = "Taxes";
        advice.body = "Tax complaints follow the rate people pay. A higher rate can slow a city. The poll does not "
                      "prove that this year's rate caused the score. The budget is where the rate is set.";
        advice.intention = "Next year, set the tax rate in the budget with the complaints in mind.";
        advice.show = "Budget";
        advice.layer = -1;
        break;
    case 4:
        advice.title = "Traffic";
        advice.body = "Traffic is the complaint people mention most. Busy roads show up on the traffic map. That "
                      "density is not a promise that one new road will clear it.";
        advice.intention = "Next year, look for long roads through developed blocks on the traffic map.";
        advice.show = "Traffic map";
        advice.layer = 5;
        break;
    case 5:
        advice.title = "Unemployment";
        advice.body = "Unemployment in the poll usually means jobs are short of the people who live here. It is a "
                      "city-wide reading, not a count on one block. Commercial and industrial zones are where work "
                      "appears.";
        advice.intention = "Next year, check commercial and industrial demand before zoning more jobs.";
        advice.show = "All zones";
        advice.layer = 10;
        break;
    case 6:
        advice.title = "Fire";
        advice.body = "Fire complaints point at risk, not a fire that is burning now. Look for developed blocks "
                      "outside fire-station coverage. Coverage is not a guarantee that a block is safe.";
        advice.intention = "Next year, compare developed blocks with fire coverage.";
        advice.show = "Fire coverage";
        advice.layer = 8;
        break;
    default:
        advice.title = "Complaints";
        advice.body = "The poll ranks complaints. It does not prove what caused them.";
        advice.intention = "Next year, read the evaluation again after the city has changed.";
        break;
    }
    return advice;
}

inline int population_milestone(std::int64_t population)
{
    if (population >= 500000) {
        return Megalopolis;
    }
    if (population >= 100000) {
        return Metropolis;
    }
    if (population >= 50000) {
        return Capital;
    }
    if (population >= 10000) {
        return City;
    }
    if (population >= 2000) {
        return Town;
    }
    return -1;
}

inline bool award(Ledger &ledger, int id, int year, int month, std::vector<int> *fresh)
{
    if (id < 0 || id >= kMilestones || ledger.hit[id].earned) {
        return false;
    }
    ledger.hit[id].earned = true;
    ledger.hit[id].year = year;
    ledger.hit[id].month = month < 0 ? 0 : (month > 11 ? 11 : month);
    if (fresh != nullptr) {
        fresh->push_back(id);
    }
    return true;
}

inline std::vector<int> note_population(Ledger &ledger, std::int64_t population, int year, int month, bool announce)
{
    std::vector<int> fresh;
    const int reached = population_milestone(population);
    if (reached < Town) {
        return fresh;
    }
    for (int id = Town; id <= reached; ++id) {
        award(ledger, id, year, month, announce ? &fresh : nullptr);
    }
    return fresh;
}

inline std::vector<int> note_powered(Ledger &ledger, bool powered, int year, int month, bool announce)
{
    std::vector<int> fresh;
    if (powered) {
        award(ledger, Powered, year, month, announce ? &fresh : nullptr);
    }
    return fresh;
}

inline std::vector<int> note_balanced(Ledger &ledger, std::int64_t cash_flow, int year, int month)
{
    std::vector<int> fresh;
    if (cash_flow >= 0) {
        award(ledger, Balanced, year, month, &fresh);
    }
    return fresh;
}

inline std::vector<int> note_traffic(Ledger &ledger, int votes, int year, int month)
{
    std::vector<int> fresh;
    if (ledger.have_prior && votes >= 0 && votes < ledger.prior_votes[4]) {
        award(ledger, Traffic, year, month, &fresh);
    }
    return fresh;
}

inline void remember_year(Ledger &ledger, const int votes[kProblems], std::int64_t population)
{
    ledger.have_prior = true;
    ledger.prior_population = population < 0 ? 0 : population;
    if (votes == nullptr) {
        return;
    }
    for (int i = 0; i < kProblems; ++i) {
        ledger.prior_votes[i] = votes[i] < 0 ? 0 : votes[i];
    }
}

inline std::string dollars(std::int64_t amount)
{
    const bool neg = amount < 0;
    unsigned long long n = static_cast<unsigned long long>(neg ? -amount : amount);
    std::string digits = std::to_string(n);
    std::string grouped;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            grouped.push_back(',');
        }
        grouped.push_back(digits[i]);
    }
    return (neg ? "-$" : "$") + grouped;
}

inline YearCard make_year_card(std::int64_t opening, std::int64_t closing, bool when_collected, bool estimated,
                               std::int64_t population, const Ledger &ledger, const int votes[kProblems],
                               const char *top_name, int top_votes, int top_kind, int score_delta, bool deltas_known)
{
    YearCard card;
    card.estimated = estimated;
    card.closing_when_collected = when_collected && !estimated;
    card.opening_label = estimated ? "Opening funds (estimate)" : "Opening funds";
    card.opening_value = dollars(opening);
    if (estimated) {
        card.closing_label = "Closing funds (estimate)";
    } else if (when_collected) {
        card.closing_label = "Closing funds, when collected";
    } else {
        card.closing_label = "Closing funds, collected";
    }
    card.closing_value = dollars(closing);
    auto plain = [](std::int64_t amount) {
        const std::string with = dollars(amount);
        if (!with.empty() && with[0] == '$') {
            return with.substr(1);
        }
        return with;
    };
    if (!ledger.have_prior) {
        card.population = "Population: " + plain(population) + ". No earlier January is stored.";
    } else {
        const std::int64_t delta = population - ledger.prior_population;
        std::string change = std::to_string(delta);
        if (delta > 0) {
            change = "+" + change;
        }
        card.population = "Population: " + plain(ledger.prior_population) + " \u2192 " + plain(population) + " (" +
                          change + ")";
    }
    int best = -1;
    int drop = 0;
    if (ledger.have_prior && votes != nullptr) {
        for (int i = 0; i < kProblems; ++i) {
            const int fell = ledger.prior_votes[i] - (votes[i] < 0 ? 0 : votes[i]);
            if (fell > drop) {
                drop = fell;
                best = i;
            }
        }
    }
    if (best >= 0) {
        const Advice which = advice_for(best);
        card.improvement = std::string(which.title) + " complaints fell from " +
                           std::to_string(ledger.prior_votes[best]) + "% to " + std::to_string(votes[best]) +
                           "%. That is a change in the poll, not a proven cause.";
    } else if (deltas_known && score_delta > 0) {
        card.improvement = "The score rose by " + std::to_string(score_delta) +
                           " since the last evaluation. The poll does not say why.";
    } else if (!ledger.have_prior) {
        card.improvement = "No earlier year to compare yet.";
    } else {
        card.improvement = "No complaint fell since last January.";
    }
    if (top_name == nullptr || top_name[0] == '\0' || top_votes < 0) {
        card.problem = "No complaint stands ahead of the others.";
        card.intention = "Next year, read the evaluation once people have settled.";
    } else {
        card.problem = std::string(top_name) + " is still the largest complaint, at " + std::to_string(top_votes) +
                       "% of the poll.";
        card.intention = advice_for(top_kind).intention;
    }
    return card;
}

struct DigestNotice {
    std::string text;
    int year = 1900;
    int month = 0;
    bool has_place = false;
    int tile_x = 0;
    int tile_y = 0;
};

struct DigestLine {
    std::string text;
    bool has_place = false;
    int tile_x = 0;
    int tile_y = 0;
};

struct YearDigest {
    YearCard card;
    std::vector<DigestLine> events;
    bool locate = false;
    int locate_x = 0;
    int locate_y = 0;
};

inline bool digest_in_year(int note_year, int note_month, int city_year)
{
    if (note_year == city_year - 1) {
        return true;
    }
    return note_year == city_year && note_month == 0;
}

inline bool digest_routine(const std::string &text)
{
    static const char *skip[] = {
        "Cannot build here.",
        "Bulldoze that area first.",
        "Not enough funds to build that.",
        "That would be off the map.",
        "Started a new city.",
        "Loaded a saved city.",
        "No place to anchor a bridge.",
        "No place to land a power line.",
        "Bridge: $50",
        "Rail bridge: $100",
        "Underwater wire: $25",
    };
    for (const char *one : skip) {
        if (text == one) {
            return true;
        }
    }
    return false;
}

inline bool digest_notable(const std::string &text)
{
    static const char *keys[] = {
        "Fire reported", "monster",    "tornado",  "earthquake", "crashed",    "wrecked",
        "explosion",     "Flooding",   "meltdown", "Riots",      "Firebombing", "reached",
        "Scenario won",  "Scenario lost", "Blackouts", "out of funds",
    };
    for (const char *key : keys) {
        if (text.find(key) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline YearDigest make_year_digest(std::int64_t opening, std::int64_t closing, bool when_collected, bool estimated,
                                   std::int64_t population, const Ledger &ledger, const int votes[kProblems],
                                   const char *top_name, int top_votes, int top_kind, int score_delta, bool deltas_known,
                                   int city_year, const std::vector<DigestNotice> &notices)
{
    YearDigest digest;
    digest.card = make_year_card(opening, closing, when_collected, estimated, population, ledger, votes, top_name,
                                 top_votes, top_kind, score_delta, deltas_known);
    struct Group {
        std::string text;
        int count = 0;
        bool notable = false;
        bool has_place = false;
        int tile_x = 0;
        int tile_y = 0;
    };
    std::vector<Group> groups;
    auto add_line = [&](const std::string &text, bool notable, bool has_place, int tile_x, int tile_y) {
        for (Group &group : groups) {
            if (group.text == text) {
                ++group.count;
                if (has_place && !group.has_place) {
                    group.has_place = true;
                    group.tile_x = tile_x;
                    group.tile_y = tile_y;
                }
                return;
            }
        }
        Group group;
        group.text = text;
        group.count = 1;
        group.notable = notable;
        group.has_place = has_place;
        group.tile_x = tile_x;
        group.tile_y = tile_y;
        groups.push_back(group);
    };
    for (int id = 0; id < kMilestones; ++id) {
        const MilestoneHit &hit = ledger.hit[id];
        if (!hit.earned || !digest_in_year(hit.year, hit.month, city_year)) {
            continue;
        }
        add_line(std::string("Milestone: ") + milestone_title(id) + " (" + milestone_date(hit) + ").", true, false, 0,
                 0);
    }
    for (const DigestNotice &notice : notices) {
        if (notice.text.empty() || digest_routine(notice.text) || !digest_in_year(notice.year, notice.month, city_year)) {
            continue;
        }
        add_line(notice.text, digest_notable(notice.text), notice.has_place, notice.tile_x, notice.tile_y);
    }
    auto emit = [&](const Group &group) {
        DigestLine line;
        line.text = group.text;
        if (group.count > 1) {
            line.text += " (" + std::to_string(group.count) + " times)";
        }
        line.has_place = group.has_place;
        line.tile_x = group.tile_x;
        line.tile_y = group.tile_y;
        if (!digest.locate && group.has_place) {
            digest.locate = true;
            digest.locate_x = group.tile_x;
            digest.locate_y = group.tile_y;
        }
        digest.events.push_back(line);
    };
    int notables = 0;
    for (const Group &group : groups) {
        if (!group.notable || notables >= 3) {
            continue;
        }
        emit(group);
        ++notables;
    }
    const Group *repeat = nullptr;
    for (const Group &group : groups) {
        if (group.notable) {
            continue;
        }
        if (repeat == nullptr || group.count > repeat->count) {
            repeat = &group;
        }
    }
    if (repeat != nullptr && static_cast<int>(digest.events.size()) < 4) {
        emit(*repeat);
    }
    if (digest.events.empty()) {
        DigestLine quiet;
        quiet.text = "No disaster or milestone was recorded this year.";
        digest.events.push_back(quiet);
    }
    return digest;
}

inline std::string encode_ledger(const Ledger &ledger)
{
    std::string out;
    for (int i = 0; i < kMilestones; ++i) {
        if (!ledger.hit[i].earned) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(',');
        }
        out += "m" + std::to_string(i) + "=" + std::to_string(ledger.hit[i].year) + "." +
               std::to_string(ledger.hit[i].month);
    }
    if (ledger.have_prior) {
        if (!out.empty()) {
            out.push_back(',');
        }
        out += "p=" + std::to_string(ledger.prior_population);
        for (int i = 0; i < kProblems; ++i) {
            out += "." + std::to_string(ledger.prior_votes[i]);
        }
    }
    return out;
}

inline bool decode_ledger(const std::string &text, Ledger &ledger)
{
    Ledger parsed;
    bool any = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t comma = text.find(',', i);
        const std::string part = text.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        i = comma == std::string::npos ? text.size() : comma + 1;
        if (part.size() < 3) {
            continue;
        }
        if (part[0] == 'm') {
            const std::size_t eq = part.find('=');
            const std::size_t dot = part.find('.');
            if (eq == std::string::npos || dot == std::string::npos || dot < eq) {
                continue;
            }
            const int id = std::atoi(part.c_str() + 1);
            const int year = std::atoi(part.c_str() + static_cast<int>(eq) + 1);
            const int month = std::atoi(part.c_str() + static_cast<int>(dot) + 1);
            if (id < 0 || id >= kMilestones) {
                continue;
            }
            parsed.hit[id].earned = true;
            parsed.hit[id].year = year;
            parsed.hit[id].month = month < 0 ? 0 : (month > 11 ? 11 : month);
            any = true;
        } else if (part[0] == 'p' && part[1] == '=') {
            parsed.have_prior = true;
            const char *cursor = part.c_str() + 2;
            parsed.prior_population = std::atoll(cursor);
            for (int v = 0; v < kProblems; ++v) {
                const char *dot = std::strchr(cursor, '.');
                if (dot == nullptr) {
                    break;
                }
                cursor = dot + 1;
                parsed.prior_votes[v] = std::atoi(cursor);
            }
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    ledger = parsed;
    return true;
}

} // namespace civic

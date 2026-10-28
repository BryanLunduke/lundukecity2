// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>

// New City seed field. Blank or "auto" takes a seed from the clock.
// Any other whole number, including 0, is the seed itself.
struct CitySeedParse {
    bool from_clock = true;
    int value = 0;
};

inline bool city_seed_equals_auto(const std::string &text)
{
    if (text.size() != 4) {
        return false;
    }
    const char word[] = {'a', 'u', 't', 'o'};
    for (int i = 0; i < 4; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[static_cast<std::size_t>(i)])) != word[i]) {
            return false;
        }
    }
    return true;
}

// Returns false when the field is neither blank, "auto", nor a whole number.
inline bool parse_city_seed(const std::string &raw, CitySeedParse &out)
{
    std::size_t begin = 0;
    while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    std::size_t end = raw.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
        --end;
    }
    const std::string text = raw.substr(begin, end - begin);
    if (text.empty() || city_seed_equals_auto(text)) {
        out.from_clock = true;
        out.value = 0;
        return true;
    }

    std::size_t index = 0;
    if (text[0] == '+') {
        index = 1;
        if (index >= text.size()) {
            return false;
        }
    }
    long value = 0;
    for (; index < text.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (!std::isdigit(ch)) {
            return false;
        }
        value = value * 10 + (ch - '0');
        if (value > 999999999L) {
            return false;
        }
    }
    out.from_clock = false;
    out.value = static_cast<int>(value);
    return true;
}

// Apply a seed field. A parse failure leaves seed and seed_was_set alone
// so a typo is not treated as "chosen from the clock".
inline bool take_city_seed(const std::string &raw, int &seed, bool &seed_was_set)
{
    CitySeedParse parsed;
    if (!parse_city_seed(raw, parsed)) {
        return false;
    }
    if (parsed.from_clock) {
        seed = 0;
        seed_was_set = false;
    } else {
        seed = parsed.value;
        seed_was_set = true;
    }
    return true;
}

// Preview key for the New City land page. The knobs use -1 for "random".
// Shifting that signed value is undefined. The seed field used to go through
// atoi, which is undefined above INT_MAX.
inline int preview_seed_from_field(const std::string &field)
{
    if (field.empty() || city_seed_equals_auto(field)) {
        return 1;
    }
    errno = 0;
    char *end = nullptr;
    const long long value = std::strtoll(field.c_str(), &end, 10);
    if (end == field.c_str()) {
        return 1;
    }
    if (errno == ERANGE || value > static_cast<long long>(INT_MAX)) {
        return INT_MAX;
    }
    if (value < static_cast<long long>(INT_MIN)) {
        return INT_MIN;
    }
    return static_cast<int>(value);
}

inline unsigned terrain_preview_key(int seed, int island, int rivers, int lakes, int trees)
{
    const auto bits = [](int value) { return static_cast<unsigned>(value); };
    return bits(seed) ^ (bits(island) << 8) ^ (bits(rivers) << 12) ^ (bits(lakes) << 16) ^ (bits(trees) << 20);
}

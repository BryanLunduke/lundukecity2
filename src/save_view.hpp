// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#pragma once

// The camera and the NEWS log, kept in a saved city's JSON sidecar so a
// load puts the player back where they were. Both ride in plain string
// values ("view" and "news"); a sidecar without them (an older save) loads
// with the default view and an empty log, and an older reader skips them.

#include <cstdlib>
#include <string>
#include <vector>

namespace save_view {

struct View {
    bool valid = false;
    int tile_size = 0;
    // Map fraction at the centre of the view, 0..1.
    double center_x = 0.5;
    double center_y = 0.5;
};

struct News {
    std::string text;
    int tile_x = 0;
    int tile_y = 0;
    int month = 0;
    int year = 1900;
    bool has_place = false;
};

inline constexpr int kMaxNews = 20;

// "t=32,x=5123,y=4012": tile pixels, then the centre in 1/10000ths so no
// locale's decimal comma is involved.
inline std::string encode_view(const View &view)
{
    if (!view.valid || view.tile_size <= 0) {
        return {};
    }
    auto part = [](double fraction) {
        const double clamped = fraction < 0.0 ? 0.0 : (fraction > 1.0 ? 1.0 : fraction);
        return std::to_string(static_cast<int>(clamped * 10000.0 + 0.5));
    };
    return "t=" + std::to_string(view.tile_size) + ",x=" + part(view.center_x) + ",y=" + part(view.center_y);
}

inline bool decode_view(const std::string &text, View &out)
{
    View view;
    int seen = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t comma = text.find(',', i);
        const std::string part = text.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        i = comma == std::string::npos ? text.size() : comma + 1;
        if (part.size() < 3 || part[1] != '=') {
            continue;
        }
        char *stop = nullptr;
        const long value = std::strtol(part.c_str() + 2, &stop, 10);
        if (stop == part.c_str() + 2 || *stop != '\0') {
            continue;
        }
        if (part[0] == 't' && value > 0 && value <= 256) {
            view.tile_size = static_cast<int>(value);
            seen |= 1;
        } else if (part[0] == 'x' && value >= 0 && value <= 10000) {
            view.center_x = value / 10000.0;
            seen |= 2;
        } else if (part[0] == 'y' && value >= 0 && value <= 10000) {
            view.center_y = value / 10000.0;
            seen |= 4;
        }
    }
    if (seen != 7) {
        return false;
    }
    view.valid = true;
    out = view;
    return true;
}

// Entries are "month|year|x|y|place|text", newest first, joined by ';'.
// The text is percent-escaped for '"', '\\', '%' and ';' so it cannot end
// the JSON string or the entry; control characters are dropped.
inline std::string encode_news(const std::vector<News> &news)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    int count = 0;
    for (const News &line : news) {
        if (++count > kMaxNews) {
            break;
        }
        std::string text;
        for (const char c : line.text) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u == 0x7f) {
                continue;
            }
            if (c == '"' || c == '\\' || c == '%' || c == ';') {
                text.push_back('%');
                text.push_back(hex[u >> 4]);
                text.push_back(hex[u & 15]);
            } else {
                text.push_back(c);
            }
            if (text.size() >= 240) {
                break;
            }
        }
        if (text.empty()) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(';');
        }
        out += std::to_string(line.month) + "|" + std::to_string(line.year) + "|" + std::to_string(line.tile_x) + "|" +
               std::to_string(line.tile_y) + "|" + (line.has_place ? "1" : "0") + "|" + text;
    }
    return out;
}

inline int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

inline std::vector<News> decode_news(const std::string &text)
{
    std::vector<News> out;
    std::size_t i = 0;
    while (i < text.size() && static_cast<int>(out.size()) < kMaxNews) {
        const std::size_t semi = text.find(';', i);
        const std::string entry = text.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
        i = semi == std::string::npos ? text.size() : semi + 1;
        long fields[5] = {0, 0, 0, 0, 0};
        std::size_t at = 0;
        bool ok = true;
        for (int f = 0; f < 5 && ok; ++f) {
            const std::size_t bar = entry.find('|', at);
            if (bar == std::string::npos || bar == at || bar - at > 11) {
                ok = false;
                break;
            }
            const std::string part = entry.substr(at, bar - at);
            char *stop = nullptr;
            fields[f] = std::strtol(part.c_str(), &stop, 10);
            ok = stop != part.c_str() && *stop == '\0';
            at = bar + 1;
        }
        if (!ok || at >= entry.size()) {
            continue;
        }
        News line;
        line.month = fields[0] >= 0 && fields[0] <= 11 ? static_cast<int>(fields[0]) : 0;
        line.year = static_cast<int>(fields[1]);
        line.tile_x = static_cast<int>(fields[2]);
        line.tile_y = static_cast<int>(fields[3]);
        line.has_place = fields[4] != 0;
        const std::string raw = entry.substr(at);
        for (std::size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] == '%' && k + 2 < raw.size()) {
                const int hi = hex_value(raw[k + 1]);
                const int lo = hex_value(raw[k + 2]);
                if (hi >= 0 && lo >= 0) {
                    line.text.push_back(static_cast<char>(hi * 16 + lo));
                    k += 2;
                    continue;
                }
            }
            line.text.push_back(raw[k]);
        }
        if (!line.text.empty()) {
            out.push_back(line);
        }
    }
    return out;
}

} // namespace save_view

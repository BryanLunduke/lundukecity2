// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "city_glance.hpp"

#include "micropolis.h"

#include <fstream>
#include <vector>

namespace {

int be_short(const unsigned char *p)
{
    return (static_cast<int>(p[0]) << 8) | static_cast<int>(p[1]);
}

std::int64_t be_long(const unsigned char *p)
{
    const unsigned int raw = (static_cast<unsigned int>(p[0]) << 24) | (static_cast<unsigned int>(p[1]) << 16) |
                             (static_cast<unsigned int>(p[2]) << 8) | static_cast<unsigned int>(p[3]);
    return static_cast<std::int32_t>(raw);
}

std::uint32_t pack(int r, int g, int b)
{
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

std::uint32_t tile_color(int raw)
{
    const int t = raw & 0x03ff;
    if (t >= RIVER && t <= WATER_HIGH) {
        return pack(15, 61, 199);
    }
    if (t >= TREEBASE && t <= WOODS5) {
        return pack(13, 122, 26);
    }
    if (t >= RESBASE && t < COMBASE) {
        return pack(41, 158, 41);
    }
    if (t >= COMBASE && t < INDBASE) {
        return pack(71, 107, 219);
    }
    if (t >= INDBASE && t < PORTBASE) {
        return pack(235, 194, 20);
    }
    if ((t >= ROADBASE && t <= BRWXXX7) || t == ROADVPOWERH) {
        return pack(41, 41, 46);
    }
    if (t >= RAILBASE && t <= LASTRAIL) {
        return pack(115, 71, 26);
    }
    if (t >= POWERBASE && t <= LASTPOWER) {
        return pack(255, 235, 13);
    }
    if (t >= FIREBASE && t <= LASTFIRE) {
        return pack(235, 46, 15);
    }
    return pack(219, 148, 66);
}

std::string grouped(std::int64_t value)
{
    const bool neg = value < 0;
    std::uint64_t mag = static_cast<std::uint64_t>(neg ? -value : value);
    std::string digits = std::to_string(mag);
    std::string out;
    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        if (count > 0 && count % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[static_cast<std::size_t>(i)]);
        ++count;
    }
    std::string text;
    if (neg) {
        text.push_back('-');
    }
    text.append(out.rbegin(), out.rend());
    return text;
}

} // namespace

CityGlance city_glance(const std::string &path)
{
    CityGlance glance;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return glance;
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr int kMisc = 2880;
    constexpr int kMap = 3120;
    constexpr int kClassic = 27120;
    if (static_cast<int>(data.size()) < kClassic) {
        return glance;
    }
    const int res = be_short(data.data() + kMisc + 4);
    const int com = be_short(data.data() + kMisc + 6);
    const int ind = be_short(data.data() + kMisc + 8);
    glance.population = static_cast<std::int64_t>(res + (com + ind) * 8) * 20;
    if (glance.population < 0) {
        glance.population = 0;
    }
    const std::int64_t time = be_long(data.data() + kMisc + 16);
    const int cycles = time > 0 ? static_cast<int>(time) : 0;
    glance.year = 1900 + cycles / 48;
    glance.month = (cycles % 48) / 4;
    if (glance.month < 0 || glance.month > 11) {
        glance.month = 0;
    }
    glance.funds = be_long(data.data() + kMisc + 100);
    for (std::size_t i = 27120; i + 16 <= data.size(); ++i) {
        if (data[i] == 'L' && data[i + 1] == 'C' && data[i + 2] == 'W' && data[i + 3] == '1' && data[i + 4] == 1 &&
            (data[i + 6] & 1u) != 0) {
            std::int64_t wide = 0;
            for (int b = 0; b < 8; ++b) {
                wide |= static_cast<std::int64_t>(data[i + 8 + b]) << (8 * b);
            }
            glance.funds = wide;
            break;
        }
    }
    glance.swatch.resize(static_cast<std::size_t>(120 * 100));
    for (int i = 0; i < 120 * 100; ++i) {
        const int raw = be_short(data.data() + kMap + i * 2);
        glance.swatch[static_cast<std::size_t>(i)] = tile_color(raw);
    }
    glance.summary = city_glance_month(glance.month) + std::string(" ") + std::to_string(glance.year) + "   Pop " +
                     grouped(glance.population) + "   $" + grouped(glance.funds);
    glance.ok = true;
    return glance;
}

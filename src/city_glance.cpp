// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "city_glance.hpp"

#include "city_tail.hpp"
#include "micropolis.h"

#include <fstream>
#include <sys/stat.h>
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

const char *class_name(std::int64_t people)
{
    if (people > 500000) {
        return "Megalopolis";
    }
    if (people > 100000) {
        return "Metropolis";
    }
    if (people > 50000) {
        return "Capital";
    }
    if (people > 10000) {
        return "City";
    }
    if (people > 2000) {
        return "Town";
    }
    return "Village";
}

int tile_of(const unsigned char *map, int x, int y)
{
    const unsigned char *p = map + (x * 100 + y) * 2;
    return (static_cast<int>(p[0]) << 8) | static_cast<int>(p[1]);
}

std::int64_t population_on_map(const unsigned char *map)
{
    constexpr int kW = 120;
    constexpr int kH = 100;
    int res = 0;
    int com = 0;
    int ind = 0;
    for (int x = 0; x < kW; ++x) {
        for (int y = 0; y < kH; ++y) {
            const int raw = tile_of(map, x, y);
            if ((raw & ZONEBIT) == 0) {
                continue;
            }
            const int tile = raw & LOMASK;
            if (tile == FREEZ) {
                int homes = 0;
                for (int nx = x - 1; nx <= x + 1; ++nx) {
                    for (int ny = y - 1; ny <= y + 1; ++ny) {
                        if (nx < 0 || ny < 0 || nx >= kW || ny >= kH) {
                            continue;
                        }
                        const int house = tile_of(map, nx, ny) & LOMASK;
                        if (house >= LHTHR && house <= HHTHR) {
                            ++homes;
                        }
                    }
                }
                res += homes;
            } else if (tile < HOSPITAL) {
                res += ((tile - RZB) / 9) % 4 * 8 + 16;
            } else if (tile >= COMBASE && tile < INDBASE) {
                if (tile != COMCLR) {
                    com += ((tile - CZB) / 9) % 5 + 1;
                }
            } else if (tile >= INDBASE && tile <= PORTBASE) {
                if (tile != INDCLR) {
                    ind += ((tile - IZB) / 9) % 4 + 1;
                }
            }
        }
    }
    const std::int64_t people = (static_cast<std::int64_t>(res) + (static_cast<std::int64_t>(com) + ind) * 8) * 20;
    return people > 0 ? people : 0;
}

} // namespace

CityGlance city_glance(const std::string &path)
{
    CityGlance glance;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return glance;
    }
    if (st.st_size > static_cast<off_t>(kMaxCityBytes)) {
        glance.summary = "Too large to be a city file";
        return glance;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return glance;
    }
    std::vector<unsigned char> data(static_cast<std::size_t>(st.st_size));
    if (!data.empty() && !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
        glance.summary = "Not a valid city";
        return glance;
    }
    constexpr int kMisc = 2880;
    constexpr int kMap = 3120;
    constexpr int kClassic = 27120;
    if (static_cast<int>(data.size()) < kClassic) {
        glance.summary = "Not a valid city";
        return glance;
    }
    bool zero = true;
    for (int i = 0; i < kClassic; ++i) {
        if (data[static_cast<std::size_t>(i)] != 0) {
            zero = false;
            break;
        }
    }
    if (zero) {
        glance.summary = "Not a valid city";
        return glance;
    }
    int unknown = 0;
    int centres = 0;
    int bad_centres = 0;
    for (int i = 0; i < 120 * 100; ++i) {
        const int raw = be_short(data.data() + kMap + i * 2);
        const int tile = raw & LOMASK;
        const bool special_center = tile == SPECIAL_ZOO || tile == SPECIAL_PARK || tile == SPECIAL_LIBRARY;
        const bool special = special_center || tile == SPECIAL_BODY;
        if (tile > CHURCH7LAST && !special) {
            ++unknown;
        }
        if ((raw & ZONEBIT) != 0) {
            ++centres;
            if (tile > CHURCH7LAST && !special_center) {
                ++bad_centres;
            }
        }
    }
    if (unknown > 0 || bad_centres > 0 || centres > (120 * 100) / 9) {
        glance.summary = "Not a valid city";
        return glance;
    }
    const int res = be_short(data.data() + kMisc + 4);
    const int com = be_short(data.data() + kMisc + 6);
    const int ind = be_short(data.data() + kMisc + 8);
    std::int64_t stored = static_cast<std::int64_t>(res + (com + ind) * 8) * 20;
    if (stored < 0) {
        stored = 0;
    }
    // Wide census sits after the classic body. Use it only when the map
    // itself has no residents, so an empty file keeps a real saved count
    // and a built city does not keep a stale one.
    for (std::size_t i = 27120; i + 32 <= data.size(); ++i) {
        if (data[i] == 'L' && data[i + 1] == 'C' && data[i + 2] == 'W' && data[i + 3] == '1' && data[i + 4] == 1 &&
            data[i + 5] == 0) {
            const unsigned flags = data[i + 6] | (static_cast<unsigned>(data[i + 7]) << 8);
            std::size_t cursor = i + 8;
            if ((flags & 1u) != 0) {
                cursor += 8;
            }
            if ((flags & 2u) != 0 && cursor + 24 <= data.size()) {
                auto read_i64 = [&](std::size_t at) {
                    std::uint64_t bits = 0;
                    for (int b = 0; b < 8; ++b) {
                        bits |= static_cast<std::uint64_t>(data[at + static_cast<std::size_t>(b)]) << (8 * b);
                    }
                    return static_cast<std::int64_t>(bits);
                };
                const std::int64_t wide_res = read_i64(cursor);
                const std::int64_t wide_com = read_i64(cursor + 8);
                const std::int64_t wide_ind = read_i64(cursor + 16);
                stored = (wide_res + (wide_com + wide_ind) * 8) * 20;
                if (stored < 0) {
                    stored = 0;
                }
            }
            break;
        }
    }
    const std::int64_t on_map = population_on_map(data.data() + kMap);
    glance.population = on_map > 0 ? on_map : stored;
    const std::int64_t time = be_long(data.data() + kMisc + 16);
    // The engine clamps a loaded city to Jan 9999. The list used to show the raw word.
    constexpr std::int64_t kMaxTime = static_cast<std::int64_t>(9999 - 1900) * 48;
    std::int64_t cycles = time > 0 ? time : 0;
    if (cycles > kMaxTime) {
        cycles = kMaxTime;
    }
    glance.year = 1900 + static_cast<int>(cycles / 48);
    glance.month = static_cast<int>((cycles % 48) / 4);
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
                     grouped(glance.population) + " " + class_name(glance.population) + "   $" + grouped(glance.funds);
    glance.ok = true;
    return glance;
}

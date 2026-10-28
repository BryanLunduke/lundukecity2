#pragma once
// Classic Micropolis cities are exactly 27,120 bytes. City 2 appends three
// trailers: LCN1 (name), LCF1 (cash flow), LCW1 (wide funds). A file that
// stops in the middle of that tail still has a usable classic body, but the
// player should hear that part of the save is missing. An all-zero body is
// not a city.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

enum class CityFileKind {
    Unreadable,
    TooShort,
    AllZero,
    Classic,
    Complete,
    Incomplete,
};

inline constexpr std::size_t kClassicCityBytes = 27120;
// Largest real save is about 27 KB plus trailers. 4 MB is generous and
// keeps a truncated image or a huge file from being read into memory.
inline constexpr std::size_t kMaxCityBytes = 4u * 1024u * 1024u;

inline CityFileKind classify_city_bytes(const unsigned char *data, std::size_t size)
{
    if (data == nullptr || size < kClassicCityBytes) {
        return CityFileKind::TooShort;
    }
    bool zero = true;
    for (std::size_t i = 0; i < kClassicCityBytes; ++i) {
        if (data[i] != 0) {
            zero = false;
            break;
        }
    }
    if (zero) {
        return CityFileKind::AllZero;
    }
    if (size == kClassicCityBytes) {
        return CityFileKind::Classic;
    }

    std::size_t off = kClassicCityBytes;
    auto avail = [&]() { return size - off; };
    auto at = [&]() { return data + off; };

    if (avail() < 6 || at()[0] != 'L' || at()[1] != 'C' || at()[2] != 'N' || at()[3] != '1') {
        return CityFileKind::Incomplete;
    }
    const unsigned name_len = static_cast<unsigned>(at()[4]) | (static_cast<unsigned>(at()[5]) << 8);
    if (name_len > 200 || avail() < 6u + name_len) {
        return CityFileKind::Incomplete;
    }
    off += 6u + name_len;

    // HISTORY_LENGTH / 2 samples: 4 byte magic, 8 bytes each, then one flag byte each.
    constexpr std::size_t kFlowBytes = 4 + 240 * 8 + 240;
    if (avail() < kFlowBytes || at()[0] != 'L' || at()[1] != 'C' || at()[2] != 'F' || at()[3] != '1') {
        return CityFileKind::Incomplete;
    }
    off += kFlowBytes;

    if (avail() < 8 || at()[0] != 'L' || at()[1] != 'C' || at()[2] != 'W' || at()[3] != '1') {
        return CityFileKind::Incomplete;
    }
    const unsigned version = static_cast<unsigned>(at()[4]) | (static_cast<unsigned>(at()[5]) << 8);
    const unsigned flags = static_cast<unsigned>(at()[6]) | (static_cast<unsigned>(at()[7]) << 8);
    if (version != 1 || (flags & ~0x7u) != 0) {
        return CityFileKind::Incomplete;
    }
    std::size_t wide = 8;
    if ((flags & 1u) != 0) {
        wide += 8;
    }
    if ((flags & 2u) != 0) {
        wide += 24;
    }
    if ((flags & 4u) != 0) {
        wide += 32;
    }
    if (avail() < wide) {
        return CityFileKind::Incomplete;
    }
    off += wide;
    if (off != size) {
        return CityFileKind::Incomplete;
    }
    return CityFileKind::Complete;
}

inline CityFileKind classify_city_path(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return CityFileKind::Unreadable;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        return CityFileKind::Unreadable;
    }
    if (static_cast<unsigned long long>(end) > kMaxCityBytes) {
        return CityFileKind::Unreadable;
    }
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(end));
    if (!data.empty() && !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
        return CityFileKind::Unreadable;
    }
    return classify_city_bytes(data.empty() ? nullptr : data.data(), data.size());
}

// One bounded read. error is a sentence for the player when this returns false.
inline bool read_city_bounded(const std::string &path, std::vector<unsigned char> &data, std::string &error)
{
    data.clear();
    error.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "The file could not be opened.";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        error = "The file could not be read.";
        return false;
    }
    if (static_cast<unsigned long long>(end) > kMaxCityBytes) {
        error = "This file is too large to be a city save.";
        return false;
    }
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(end));
    if (!data.empty() && !in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
        data.clear();
        error = "The file could not be read.";
        return false;
    }
    return true;
}

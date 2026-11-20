#pragma once
// Classic Micropolis cities are exactly 27,120 bytes. City 2 appends three
// trailers: LCN1 (name), LCF1 (cash flow), LCW1 (wide funds), then the
// optional LCM1 (marina dock), LCD1 (lasting disasters), LCS1 (scan maps)
// and LCV1 (format). A file that stops in the middle of that tail still has
// a usable classic body, but the player should hear that part of the save
// is missing. An all-zero body is not a city.

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

// C1: the save-format marker, the last 12 bytes of a City 2 save from
// format 2 on ("LCV1", u16 version, u16 flags, u16 corner tile, u16 0).
// Mirrors Micropolis::readCityFormat for code that reads files directly.
inline constexpr std::size_t kCityFormatBytes = 12;
inline constexpr unsigned kCityFormatGuardTile = 1019;
inline bool city_format_marker(const unsigned char *data, std::size_t size, unsigned &version, unsigned &flags,
                               unsigned &corner)
{
    version = 1;
    flags = 0;
    corner = 0;
    if (data == nullptr || size < kClassicCityBytes + kCityFormatBytes) {
        return false;
    }
    const unsigned char *c = data + size - kCityFormatBytes;
    if (c[0] != 'L' || c[1] != 'C' || c[2] != 'V' || c[3] != '1') {
        return false;
    }
    version = static_cast<unsigned>(c[4]) | (static_cast<unsigned>(c[5]) << 8);
    flags = static_cast<unsigned>(c[6]) | (static_cast<unsigned>(c[7]) << 8);
    corner = static_cast<unsigned>(c[8]) | (static_cast<unsigned>(c[9]) << 8);
    return true;
}

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
    // Optional marina dock notes (LCM1): 8-byte header, then 6 bytes each.
    if (avail() >= 8 && at()[0] == 'L' && at()[1] == 'C' && at()[2] == 'M' && at()[3] == '1') {
        const std::size_t count = static_cast<std::size_t>(at()[6]) | (static_cast<std::size_t>(at()[7]) << 8);
        if (avail() < 8 + count * 6) {
            return CityFileKind::Incomplete;
        }
        off += 8 + count * 6;
    }
    // Optional lasting-disaster state (LCD1): 8-byte header, then the
    // payload length it names.
    if (avail() >= 8 && at()[0] == 'L' && at()[1] == 'C' && at()[2] == 'D' && at()[3] == '1') {
        const std::size_t payload = static_cast<std::size_t>(at()[6]) | (static_cast<std::size_t>(at()[7]) << 8);
        if (avail() < 8 + payload) {
            return CityFileKind::Incomplete;
        }
        off += 8 + payload;
    }
    // Optional land/crime/pollution/terrain maps (LCS1): 8-byte header,
    // then the payload length it names.
    if (avail() >= 8 && at()[0] == 'L' && at()[1] == 'C' && at()[2] == 'S' && at()[3] == '1') {
        const std::size_t payload = static_cast<std::size_t>(at()[6]) | (static_cast<std::size_t>(at()[7]) << 8);
        if (avail() < 8 + payload) {
            return CityFileKind::Incomplete;
        }
        off += 8 + payload;
    }
    // Optional save-format marker (LCV1), always last.
    if (avail() == kCityFormatBytes && at()[0] == 'L' && at()[1] == 'C' && at()[2] == 'V' && at()[3] == '1') {
        off += kCityFormatBytes;
    }
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

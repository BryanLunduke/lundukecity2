// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The release binary must not contain screenshot or demo hooks, and it must
// not contain a /workspace path. LUNDUKE_CITY_ASSETS is the documented
// developer override for the asset tree and is the only LUNDUKE_CITY_ string
// allowed in a loaded segment.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int fail(int code, const char *message)
{
    std::cerr << message << "\n";
    return code;
}

std::uint16_t read16(const std::vector<unsigned char> &bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t read32(const std::vector<unsigned char> &bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) |
                                     (bytes[offset + 3] << 24));
}

std::uint64_t read64(const std::vector<unsigned char> &bytes, std::size_t offset)
{
    return static_cast<std::uint64_t>(read32(bytes, offset)) |
           (static_cast<std::uint64_t>(read32(bytes, offset + 4)) << 32);
}

bool allowed_hook(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t end)
{
    const std::string allowed = "LUNDUKE_CITY_ASSETS";
    if (at + allowed.size() > end) {
        return false;
    }
    for (std::size_t i = 0; i < allowed.size(); ++i) {
        if (bytes[at + i] != static_cast<unsigned char>(allowed[i])) {
            return false;
        }
    }
    const std::size_t after = at + allowed.size();
    if (after < end) {
        const unsigned char next = bytes[after];
        if ((next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9') || next == '_') {
            return false;
        }
    }
    return true;
}

int scan(const std::vector<unsigned char> &bytes, std::size_t begin, std::size_t end)
{
    const std::string workspace = "/workspace";
    const std::string hook = "LUNDUKE_CITY_";
    for (std::size_t i = begin; i + workspace.size() <= end; ++i) {
        bool match = true;
        for (std::size_t n = 0; n < workspace.size(); ++n) {
            if (bytes[i + n] != static_cast<unsigned char>(workspace[n])) {
                match = false;
                break;
            }
        }
        if (match) {
            return 2;
        }
    }
    for (std::size_t i = begin; i + hook.size() <= end; ++i) {
        bool match = true;
        for (std::size_t n = 0; n < hook.size(); ++n) {
            if (bytes[i + n] != static_cast<unsigned char>(hook[n])) {
                match = false;
                break;
            }
        }
        if (match && !allowed_hook(bytes, i, end)) {
            std::string found;
            for (std::size_t n = i; n < end && n < i + 80 && bytes[n] >= 32 && bytes[n] < 127; ++n) {
                found.push_back(static_cast<char>(bytes[n]));
            }
            std::cerr << "hook string '" << found << "'\n";
            return 3;
        }
    }
    return 0;
}

// Byte ranges the loader maps: ELF PT_LOAD segments, or the PE sections
// that are not discardable (MinGW's .debug_* sections are).
bool load_ranges(const std::vector<unsigned char> &bytes, std::vector<std::pair<std::size_t, std::size_t>> &ranges,
                 const char *&error)
{
    if (bytes.size() >= 64 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F' &&
        bytes[4] == 2 && bytes[5] == 1) {
        const std::uint64_t phoff = read64(bytes, 32);
        const std::uint16_t phentsize = read16(bytes, 54);
        const std::uint16_t phnum = read16(bytes, 56);
        if (phentsize < 56 || phoff + static_cast<std::uint64_t>(phentsize) * phnum > bytes.size()) {
            error = "the city binary has a bad program header";
            return false;
        }
        for (std::uint16_t i = 0; i < phnum; ++i) {
            const std::size_t off = static_cast<std::size_t>(phoff + static_cast<std::uint64_t>(i) * phentsize);
            if (read32(bytes, off) != 1) {
                continue;
            }
            const std::uint64_t offset = read64(bytes, off + 8);
            const std::uint64_t filesz = read64(bytes, off + 32);
            if (offset + filesz > bytes.size()) {
                error = "a load segment runs past the end of the binary";
                return false;
            }
            ranges.emplace_back(static_cast<std::size_t>(offset), static_cast<std::size_t>(offset + filesz));
        }
        return true;
    }
    if (bytes.size() >= 64 && bytes[0] == 'M' && bytes[1] == 'Z') {
        const std::uint32_t pe = read32(bytes, 0x3c);
        if (pe + 24 > bytes.size() || bytes[pe] != 'P' || bytes[pe + 1] != 'E' || bytes[pe + 2] != 0 ||
            bytes[pe + 3] != 0) {
            error = "the city binary has a bad PE header";
            return false;
        }
        const std::uint16_t nsect = read16(bytes, pe + 6);
        const std::uint16_t optsize = read16(bytes, pe + 20);
        const std::size_t table = pe + 24 + optsize;
        if (table + static_cast<std::size_t>(nsect) * 40 > bytes.size()) {
            error = "the city binary has a bad section table";
            return false;
        }
        for (std::uint16_t i = 0; i < nsect; ++i) {
            const std::size_t sh = table + static_cast<std::size_t>(i) * 40;
            const std::uint32_t rawsize = read32(bytes, sh + 16);
            const std::uint32_t rawptr = read32(bytes, sh + 20);
            const std::uint32_t flags = read32(bytes, sh + 36);
            if ((flags & 0x02000000u) != 0 || rawsize == 0) {
                continue; // IMAGE_SCN_MEM_DISCARDABLE, or no file data
            }
            if (static_cast<std::uint64_t>(rawptr) + rawsize > bytes.size()) {
                error = "a section runs past the end of the binary";
                return false;
            }
            ranges.emplace_back(rawptr, static_cast<std::size_t>(rawptr) + rawsize);
        }
        return true;
    }
    error = "the city binary is not a little-endian ELF64 or a PE image";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        return fail(1, "usage: production_binary <lunduke-city>");
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    const char *error = nullptr;
    if (!load_ranges(bytes, ranges, error)) {
        return fail(1, error);
    }
    bool saw_load = false;
    bool saw_assets = false;
    for (const auto &range : ranges) {
        const std::size_t offset = range.first;
        const std::size_t end = range.second;
        saw_load = true;
        const int found = scan(bytes, offset, end);
        if (found == 2) {
            return fail(2, "the release binary contains /workspace");
        }
        if (found == 3) {
            return fail(3, "the release binary contains a screenshot or demo hook");
        }
        const std::string assets = "LUNDUKE_CITY_ASSETS";
        for (std::size_t at = offset; at + assets.size() <= end; ++at) {
            bool match = true;
            for (std::size_t n = 0; n < assets.size(); ++n) {
                if (bytes[at + n] != static_cast<unsigned char>(assets[n])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                saw_assets = true;
            }
        }
    }
    if (!saw_load) {
        return fail(1, "the city binary has no load segment");
    }
    if (!saw_assets) {
        return fail(4, "the documented LUNDUKE_CITY_ASSETS override is missing");
    }
    return 0;
}

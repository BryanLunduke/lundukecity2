// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

// The release binary must not contain a source-tree path, and the only
// LUNDUKE_CITY_ string in a loaded segment is the documented
// LUNDUKE_CITY_ASSETS override.
//
// LUNDUKE_CITY2_* harness variables ship in every build, including release.
// meson test (sdl_smoke, ui_input, the screenshot demos) and the Windows
// package smoke drive this same binary, so compiling the hooks out would
// make those tests fail. The allow-list below is that decision: a new
// LUNDUKE_CITY2_ name fails the test. LUNDUKE_CITY2_REVIEW is optional
// because release builds omit the review self-test.
//
// A DT_RPATH or DT_RUNPATH that mentions /workspace (SDL installed under a
// private prefix) is not a source path baked into the game.

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

bool identifier_continues(unsigned char next)
{
    return (next >= 'A' && next <= 'Z') || (next >= '0' && next <= '9') || next == '_';
}

bool exact_token(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t end, const std::string &token)
{
    if (at + token.size() > end) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (bytes[at + i] != static_cast<unsigned char>(token[i])) {
            return false;
        }
    }
    const std::size_t after = at + token.size();
    if (after < end && identifier_continues(bytes[after])) {
        return false;
    }
    return true;
}

bool allowed_city_hook(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t end)
{
    return exact_token(bytes, at, end, "LUNDUKE_CITY_ASSETS");
}

bool allowed_city2_hook(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t end)
{
    static const char *kAllowed[] = {
        "LUNDUKE_CITY2_DEMO", "LUNDUKE_CITY2_SHOT",     "LUNDUKE_CITY2_SAVE", "LUNDUKE_CITY2_SMOKE",
        "LUNDUKE_CITY2_UI_TEST", "LUNDUKE_CITY2_ZOOM", "LUNDUKE_CITY2_LOOK", "LUNDUKE_CITY2_SIZE",
        "LUNDUKE_CITY2_SCALE", "LUNDUKE_CITY2_REVIEW",
    };
    for (const char *token : kAllowed) {
        if (exact_token(bytes, at, end, token)) {
            return true;
        }
    }
    return false;
}

bool in_skip(const std::vector<std::pair<std::size_t, std::size_t>> &skip, std::size_t at)
{
    for (const auto &range : skip) {
        if (at >= range.first && at < range.second) {
            return true;
        }
    }
    return false;
}

int scan(const std::vector<unsigned char> &bytes, std::size_t begin, std::size_t end,
         const std::vector<std::pair<std::size_t, std::size_t>> &skip)
{
    const std::string workspace = "/workspace";
    const std::string hook = "LUNDUKE_CITY_";
    const std::string hook2 = "LUNDUKE_CITY2_";
    for (std::size_t i = begin; i + workspace.size() <= end; ++i) {
        if (in_skip(skip, i)) {
            continue;
        }
        // Compiler asserts record the file as "../../workspace/...".
        // That relative path is not an installed source prefix.
        if (i > begin && bytes[i - 1] == '.') {
            continue;
        }
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
    for (std::size_t i = begin; i + hook2.size() <= end; ++i) {
        bool match = true;
        for (std::size_t n = 0; n < hook2.size(); ++n) {
            if (bytes[i + n] != static_cast<unsigned char>(hook2[n])) {
                match = false;
                break;
            }
        }
        if (match && !allowed_city2_hook(bytes, i, end)) {
            std::string found;
            for (std::size_t n = i; n < end && n < i + 80 && bytes[n] >= 32 && bytes[n] < 127; ++n) {
                found.push_back(static_cast<char>(bytes[n]));
            }
            std::cerr << "hook string '" << found << "'\n";
            return 3;
        }
    }
    for (std::size_t i = begin; i + hook.size() <= end; ++i) {
        // LUNDUKE_CITY2_ is a different prefix (the character after Y is
        // '2', not '_'), so this scan does not see those names.
        bool match = true;
        for (std::size_t n = 0; n < hook.size(); ++n) {
            if (bytes[i + n] != static_cast<unsigned char>(hook[n])) {
                match = false;
                break;
            }
        }
        if (match && !allowed_city_hook(bytes, i, end)) {
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

std::size_t cstr_end(const std::vector<unsigned char> &bytes, std::size_t at, std::size_t end)
{
    std::size_t n = at;
    while (n < end && bytes[n] != 0) {
        ++n;
    }
    return n;
}

// File ranges of DT_RPATH and DT_RUNPATH strings. Other /workspace bytes
// still fail the scan.
void runpath_ranges(const std::vector<unsigned char> &bytes, std::vector<std::pair<std::size_t, std::size_t>> &skip)
{
    if (bytes.size() < 64 || bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' ||
        bytes[4] != 2 || bytes[5] != 1) {
        return;
    }
    const std::uint64_t phoff = read64(bytes, 32);
    const std::uint16_t phentsize = read16(bytes, 54);
    const std::uint16_t phnum = read16(bytes, 56);
    if (phentsize < 56 || phoff + static_cast<std::uint64_t>(phentsize) * phnum > bytes.size()) {
        return;
    }
    struct Load {
        std::uint64_t offset;
        std::uint64_t vaddr;
        std::uint64_t filesz;
    };
    std::vector<Load> loads;
    std::uint64_t dyn_off = 0;
    std::uint64_t dyn_size = 0;
    bool have_dyn = false;
    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::size_t off = static_cast<std::size_t>(phoff + static_cast<std::uint64_t>(i) * phentsize);
        const std::uint32_t type = read32(bytes, off);
        const std::uint64_t offset = read64(bytes, off + 8);
        const std::uint64_t vaddr = read64(bytes, off + 16);
        const std::uint64_t filesz = read64(bytes, off + 32);
        if (type == 1) {
            loads.push_back(Load{offset, vaddr, filesz});
        } else if (type == 2) {
            dyn_off = offset;
            dyn_size = filesz;
            have_dyn = true;
        }
    }
    if (!have_dyn || dyn_off + dyn_size > bytes.size()) {
        return;
    }
    auto vaddr_to_off = [&](std::uint64_t vaddr, std::size_t &file_off) {
        for (const Load &load : loads) {
            if (vaddr >= load.vaddr && vaddr < load.vaddr + load.filesz) {
                file_off = static_cast<std::size_t>(load.offset + (vaddr - load.vaddr));
                return file_off < bytes.size();
            }
        }
        return false;
    };
    std::uint64_t strtab = 0;
    bool have_str = false;
    std::vector<std::uint64_t> path_offs;
    for (std::uint64_t at = dyn_off; at + 16 <= dyn_off + dyn_size; at += 16) {
        const std::uint64_t tag = read64(bytes, static_cast<std::size_t>(at));
        const std::uint64_t val = read64(bytes, static_cast<std::size_t>(at + 8));
        if (tag == 0) {
            break;
        }
        if (tag == 5) {
            strtab = val;
            have_str = true;
        } else if (tag == 15 || tag == 29) {
            path_offs.push_back(val);
        }
    }
    if (!have_str) {
        return;
    }
    std::size_t str_file = 0;
    if (!vaddr_to_off(strtab, str_file)) {
        return;
    }
    for (std::uint64_t path_off : path_offs) {
        const std::size_t start = str_file + static_cast<std::size_t>(path_off);
        if (start >= bytes.size()) {
            continue;
        }
        const std::size_t stop = cstr_end(bytes, start, bytes.size());
        if (stop > start) {
            skip.emplace_back(start, stop);
        }
    }
}

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
                continue;
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

bool contains_token(const std::vector<unsigned char> &bytes, std::size_t begin, std::size_t end, const std::string &token)
{
    for (std::size_t at = begin; at + token.size() <= end; ++at) {
        if (exact_token(bytes, at, end, token)) {
            return true;
        }
    }
    return false;
}

int self_check()
{
    std::vector<std::pair<std::size_t, std::size_t>> empty;
    const std::string runpath = "lib:/workspace/sdl3-prefix/lib";
    const std::string baked = "built from /workspace/src";
    const std::string secret = "LUNDUKE_CITY2_SECRET";
    const std::string demo = "LUNDUKE_CITY2_DEMO";
    std::vector<unsigned char> bytes;
    const std::size_t run_at = bytes.size();
    bytes.insert(bytes.end(), runpath.begin(), runpath.end());
    bytes.push_back(0);
    const std::size_t baked_at = bytes.size();
    bytes.insert(bytes.end(), baked.begin(), baked.end());
    bytes.push_back(0);
    bytes.insert(bytes.end(), demo.begin(), demo.end());
    bytes.push_back(0);
    const std::size_t secret_at = bytes.size();
    bytes.insert(bytes.end(), secret.begin(), secret.end());
    bytes.push_back(0);
    std::vector<std::pair<std::size_t, std::size_t>> skip;
    skip.emplace_back(run_at, run_at + runpath.size());
    if (scan(bytes, run_at, run_at + runpath.size(), skip) != 0) {
        std::cerr << "a RUNPATH under /workspace was flagged\n";
        return 5;
    }
    if (scan(bytes, baked_at, baked_at + baked.size(), skip) != 2) {
        std::cerr << "a source path outside RUNPATH was allowed\n";
        return 5;
    }
    if (scan(bytes, secret_at, bytes.size(), skip) != 3) {
        std::cerr << "an unknown LUNDUKE_CITY2_ hook was allowed\n";
        return 5;
    }
    std::vector<unsigned char> only_demo(demo.begin(), demo.end());
    only_demo.push_back(0);
    if (scan(only_demo, 0, only_demo.size(), empty) != 0) {
        std::cerr << "the documented demo hook was flagged\n";
        return 5;
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (const int self = self_check()) {
        return self;
    }
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
    std::vector<std::pair<std::size_t, std::size_t>> skip;
    runpath_ranges(bytes, skip);
    bool saw_load = false;
    bool saw_assets = false;
    bool saw_demo = false;
    bool saw_smoke = false;
    bool saw_ui = false;
    bool saw_size = false;
    for (const auto &range : ranges) {
        const std::size_t offset = range.first;
        const std::size_t end = range.second;
        saw_load = true;
        const int found = scan(bytes, offset, end, skip);
        if (found == 2) {
            return fail(2, "the release binary contains /workspace");
        }
        if (found == 3) {
            return fail(3, "the release binary contains a screenshot or demo hook");
        }
        if (contains_token(bytes, offset, end, "LUNDUKE_CITY_ASSETS")) {
            saw_assets = true;
        }
        if (contains_token(bytes, offset, end, "LUNDUKE_CITY2_DEMO")) {
            saw_demo = true;
        }
        if (contains_token(bytes, offset, end, "LUNDUKE_CITY2_SMOKE")) {
            saw_smoke = true;
        }
        if (contains_token(bytes, offset, end, "LUNDUKE_CITY2_UI_TEST")) {
            saw_ui = true;
        }
        if (contains_token(bytes, offset, end, "LUNDUKE_CITY2_SIZE")) {
            saw_size = true;
        }
    }
    if (!saw_load) {
        return fail(1, "the city binary has no load segment");
    }
    if (!saw_assets) {
        return fail(4, "the documented LUNDUKE_CITY_ASSETS override is missing");
    }
    if (!saw_demo || !saw_smoke || !saw_ui || !saw_size) {
        return fail(4, "a documented LUNDUKE_CITY2 harness hook is missing");
    }
    return 0;
}

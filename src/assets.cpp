// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "assets.hpp"

#include "platform.hpp"

#include <cstdlib>
#include <fstream>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

bool is_asset_root(const std::string &path)
{
    const std::string marker = path + "/images/micropolisEngine/tiles.png";
    std::ifstream in(marker, std::ios::binary);
    if (in.good()) {
        return true;
    }
#ifdef _WIN32
    const std::string win = path + "\\images\\micropolisEngine\\tiles.png";
    std::ifstream alt(win, std::ios::binary);
    return alt.good();
#else
    return false;
#endif
}

std::string exe_dir()
{
#ifdef _WIN32
    char buf[4096];
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return {};
    }
    std::string path(buf, buf + n);
    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    buf[n] = '\0';
    std::string path(buf);
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
#endif
}

std::string parent_dir(std::string path)
{
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        const bool drive = path.size() == 3 && path[1] == ':';
        if (drive) {
            break;
        }
        path.pop_back();
    }
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return {};
    }
    if (slash == 0) {
        return "/";
    }
    if (slash == 2 && path[1] == ':') {
        return path.substr(0, slash + 1);
    }
    return path.substr(0, slash);
}

void add_layout(std::vector<std::string> &candidates, const std::string &dir, const char *relative)
{
    if (dir.empty()) {
        return;
    }
    const bool trailed = dir.back() == '/' || dir.back() == '\\';
    candidates.push_back(trailed ? dir + relative : dir + "/" + relative);
#ifdef _WIN32
    std::string win = relative;
    for (char &ch : win) {
        if (ch == '/') {
            ch = '\\';
        }
    }
    candidates.push_back(trailed ? dir + win : dir + "\\" + win);
#endif
}

} // namespace

std::vector<std::string> asset_candidates()
{
    std::vector<std::string> candidates;
    // Developer override for the asset tree when the binary is not
    // installed next to share/lunduke-city-2. This is not a test hook.
    if (const char *env = std::getenv("LUNDUKE_CITY_ASSETS")) {
        if (env[0] != '\0') {
            candidates.emplace_back(env);
        }
    }
#if defined(LUNDUKE_CITY_ASSET_DIR)
    // Set for unit tests only. The installed executable is not compiled
    // with a source-tree path, so a package cannot fall back into a build.
    candidates.emplace_back(LUNDUKE_CITY_ASSET_DIR);
#endif
#if defined(CITY_DATA_DIR)
    // Prefix data dir from the build (share/lunduke-city-2/micropolis-assets).
    // The macro name is not a LUNDUKE_CITY_* string; the value is the
    // install prefix, not the source tree.
    if (CITY_DATA_DIR[0] != '\0') {
        candidates.emplace_back(CITY_DATA_DIR);
    }
#endif
    // Install/dist: <root>/bin/<exe> beside <root>/share/lunduke-city-2/.
    // Source tree: <repo>/<any-build-dir>/src/<exe> beside
    // <repo>/third_party/micropolis-assets. Walk parents so a directory
    // named build is not required (build-x, build-sniper, build-windows/build).
    std::string dir = exe_dir();
    for (int level = 0; level < 6 && !dir.empty(); ++level) {
        add_layout(candidates, dir, "share/lunduke-city-2/micropolis-assets");
        add_layout(candidates, dir, "third_party/micropolis-assets");
        const std::string up = parent_dir(dir);
        if (up.empty() || up == dir) {
            break;
        }
        dir = up;
    }
    return candidates;
}

std::string asset_missing_text(const std::vector<std::string> &searched)
{
    std::string text = "Lunduke City 2 could not find its game data (images/micropolisEngine/tiles.png).\n\nLooked in:\n";
    if (searched.empty()) {
        text += "  (nowhere — the install path could not be determined)\n";
    }
    for (const std::string &path : searched) {
        text += "  " + path + "\n";
    }
    text += "\nReinstall the game, or set LUNDUKE_CITY_ASSETS to the micropolis-assets folder.";
    return text;
}

std::string asset_root()
{
    static const std::string cached = [] {
        for (const auto &candidate : asset_candidates()) {
            if (is_asset_root(candidate)) {
                return candidate;
            }
        }
        return std::string();
    }();
    return cached;
}

std::string asset_file(const std::string &relative)
{
    const std::string root = asset_root();
    if (root.empty()) {
        return {};
    }
    const std::string path = root + "/" + relative;
    std::ifstream in(path, std::ios::binary);
    if (in.good()) {
        return path;
    }
#ifdef _WIN32
    std::string win = relative;
    for (char &ch : win) {
        if (ch == '/') {
            ch = '\\';
        }
    }
    const std::string alt = root + "\\" + win;
    std::ifstream alt_in(alt, std::ios::binary);
    if (alt_in.good()) {
        return alt;
    }
#endif
    return {};
}

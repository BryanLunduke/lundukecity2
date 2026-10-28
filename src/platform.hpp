// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Paths for the Lunduke City 2 install. Distinct from Lunduke City so both
// can be installed on the same machine. Windows and POSIX each have a path.

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

inline bool path_is_absolute(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
#ifdef _WIN32
    if (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        return true;
    }
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

inline std::string user_home_dir()
{
#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
    if (home != nullptr && home[0] != '\0') {
        return home;
    }
    const char *drive = std::getenv("HOMEDRIVE");
    const char *path = std::getenv("HOMEPATH");
    if (drive != nullptr && path != nullptr) {
        return std::string(drive) + path;
    }
    return {};
#else
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] != '/') {
        return {};
    }
    return home;
#endif
}

// ~/.config/lunduke-city-2, used when XDG_CONFIG_HOME is unset and as the
// source of a one-time copy when that variable points somewhere new.
inline std::string legacy_config_dir()
{
    const std::string home = user_home_dir();
    if (home.empty()) {
        return {};
    }
#ifdef _WIN32
    return home + "\\AppData\\Roaming\\lunduke-city-2";
#else
    return home + "/.config/lunduke-city-2";
#endif
}

// Copy settings and recent files once, when an absolute XDG_CONFIG_HOME
// points at a directory that does not yet have any. Existing files in the
// new place are left alone.
inline void migrate_legacy_config(const std::string &dest)
{
    const std::string legacy = legacy_config_dir();
    if (legacy.empty() || dest.empty() || legacy == dest) {
        return;
    }
    if (legacy.find("/xfce") != std::string::npos || dest.find("/xfce") != std::string::npos) {
        return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(legacy, ec)) {
        return;
    }
    if (fs::is_directory(dest, ec)) {
        for (fs::directory_iterator it(dest, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                return;
            }
        }
    }
    fs::create_directories(dest, ec);
    if (ec) {
        return;
    }
    for (fs::directory_iterator it(legacy, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) {
            continue;
        }
        const fs::path to = fs::path(dest) / it->path().filename();
        if (fs::exists(to, file_ec)) {
            continue;
        }
        fs::copy_file(it->path(), to, file_ec);
    }
}

// Config lives here, never under an XFCE tree. On Windows this is
// %APPDATA%\lunduke-city-2. On POSIX an absolute XDG_CONFIG_HOME wins;
// otherwise the directory is ~/.config/lunduke-city-2. A relative
// XDG_CONFIG_HOME is ignored, matching the XDG base-directory spec.
// The first use of a new XDG location copies the old folder across.
inline std::string city_config_dir()
{
#ifdef _WIN32
    const char *appdata = std::getenv("APPDATA");
    if (appdata != nullptr && appdata[0] != '\0') {
        return std::string(appdata) + "\\lunduke-city-2";
    }
    const std::string home = user_home_dir();
    if (home.empty()) {
        return {};
    }
    return home + "\\AppData\\Roaming\\lunduke-city-2";
#else
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    std::string dir;
    if (xdg != nullptr && xdg[0] == '/') {
        dir = std::string(xdg) + "/lunduke-city-2";
        if (dir.find("/xfce") == std::string::npos) {
            migrate_legacy_config(dir);
        }
    } else {
        const std::string home = user_home_dir();
        if (home.empty()) {
            return {};
        }
        dir = home + "/.config/lunduke-city-2";
    }
    if (dir.find("/xfce") != std::string::npos) {
        return {};
    }
    return dir;
#endif
}

inline bool steam_deck_environment()
{
    const char *deck = std::getenv("SteamDeck");
    if (deck != nullptr && deck[0] == '1') {
        return true;
    }
    return false;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// Paths for the Lunduke City 2 install. Distinct from Lunduke City so both
// can be installed on the same machine. Windows and POSIX each have a path.

#include <cstdlib>
#include <string>

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

// Config lives here, never under an XFCE tree. On Windows this is
// %APPDATA%\lunduke-city-2. On POSIX an absolute XDG_CONFIG_HOME wins;
// otherwise the directory is ~/.config/lunduke-city-2. A relative
// XDG_CONFIG_HOME is ignored, matching the XDG base-directory spec.
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

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

#include "platform.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

// The native file dialog on Windows hands back backslash paths. A backslash
// is an ordinary file name character on POSIX, so only Windows splits on it.
#ifdef _WIN32
inline constexpr const char *kPathSeparators = "/\\";
#else
inline constexpr const char *kPathSeparators = "/";
#endif

inline bool is_path_separator(char ch)
{
#ifdef _WIN32
    return ch == '/' || ch == '\\';
#else
    return ch == '/';
#endif
}

// Drop a trailing slash, but keep the root. On Windows "C:\" is that root:
// stripping the slash leaves "C:", which is not an absolute path.
inline void strip_trailing_separators(std::string &path)
{
    while (path.size() > 1 && is_path_separator(path.back())) {
#ifdef _WIN32
        if (path.size() == 3 && path[1] == ':') {
            break;
        }
#endif
        path.pop_back();
    }
}

// True when the path already ends in .cty, ignoring case.
inline bool path_has_cty_suffix(const std::string &path)
{
    if (path.size() < 4) {
        return false;
    }
    const unsigned char dot = static_cast<unsigned char>(path[path.size() - 4]);
    const unsigned char a = static_cast<unsigned char>(path[path.size() - 3]);
    const unsigned char b = static_cast<unsigned char>(path[path.size() - 2]);
    const unsigned char c = static_cast<unsigned char>(path[path.size() - 1]);
    return dot == '.' && std::tolower(a) == 'c' && std::tolower(b) == 't' && std::tolower(c) == 'y';
}

// The name the player typed, without a trailing .cty and without surrounding
// spaces. Empty, ".cty", and "." are not a file stem.
inline std::string save_name_stem(const std::string &name)
{
    std::string stem = name;
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '\t' || stem.back() == '\r')) {
        stem.pop_back();
    }
    std::size_t start = 0;
    while (start < stem.size() && (stem[start] == ' ' || stem[start] == '\t')) {
        ++start;
    }
    if (start > 0) {
        stem.erase(0, start);
    }
    if (path_has_cty_suffix(stem)) {
        stem.resize(stem.size() - 4);
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '\t' || stem.back() == '.')) {
        stem.pop_back();
    }
    return stem;
}

inline bool save_name_has_stem(const std::string &name)
{
    const std::string stem = save_name_stem(name);
    return !stem.empty() && stem != "." && stem != "..";
}

// A leading dot writes a hidden file the Load list will not show.
inline bool save_name_starts_with_dot(const std::string &name)
{
    const std::string stem = save_name_stem(name);
    return !stem.empty() && stem[0] == '.';
}

// The path that will actually be opened. Append .cty before the chooser
// confirms a name that does not already have the suffix.
inline std::string with_cty_suffix(const std::string &path)
{
    if (path_has_cty_suffix(path)) {
        return path;
    }
    return path + ".cty";
}

// File name offered by Save City. A city that already has a file keeps that
// file's name. Otherwise the name is the city's name, with characters that
// are illegal in a file name replaced, and a .cty suffix.
inline std::string proposed_city_filename(const std::string &city_name, const std::string &current_path)
{
    if (!current_path.empty()) {
        const auto slash = current_path.find_last_of(kPathSeparators);
        std::string base = slash == std::string::npos ? current_path : current_path.substr(slash + 1);
        if (!base.empty() && base != "." && base != "..") {
            if (!path_has_cty_suffix(base)) {
                base += ".cty";
            }
            return base;
        }
    }

    std::string cleaned;
    cleaned.reserve(city_name.size());
    bool pending_space = false;
    for (unsigned char ch : city_name) {
        if (ch < 0x20 || ch == 0x7f) {
            continue;
        }
        const bool illegal = ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
                             ch == '<' || ch == '>' || ch == '|';
        if (illegal) {
            if (!cleaned.empty() && cleaned.back() != '_') {
                cleaned.push_back('_');
            }
            pending_space = false;
            continue;
        }
        if (ch == ' ' || ch == '\t') {
            if (!cleaned.empty()) {
                pending_space = true;
            }
            continue;
        }
        if (pending_space) {
            cleaned.push_back(' ');
            pending_space = false;
        }
        cleaned.push_back(static_cast<char>(ch));
    }
    while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '.')) {
        cleaned.pop_back();
    }
    std::size_t start = 0;
    while (start < cleaned.size() && (cleaned[start] == ' ' || cleaned[start] == '.')) {
        ++start;
    }
    if (start > 0) {
        cleaned.erase(0, start);
    }
    if (cleaned.empty()) {
        cleaned = "city";
    }
    if (!path_has_cty_suffix(cleaned)) {
        cleaned += ".cty";
    }
    return cleaned;
}

// city_config_dir() lives in platform.hpp. On POSIX an absolute
// XDG_CONFIG_HOME is honored; otherwise ~/.config/lunduke-city-2.
// Windows uses %APPDATA%\lunduke-city-2.

// Last folder used by Save or Load. Empty when none has been stored, or when
// the stored path is not an absolute directory.
inline std::string remembered_city_folder()
{
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return {};
    }
    std::ifstream in(dir + "/last-folder");
    std::string line;
    if (!std::getline(in, line)) {
        return {};
    }
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    strip_trailing_separators(line);
    if (line.empty() || !path_is_absolute(line) || line.find("/xfce") != std::string::npos) {
        return {};
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(line, ec)) {
        return {};
    }
    return line;
}

// Remember the directory of a city file, or a directory itself. Refuses an
// XFCE path and a relative path.
inline void remember_city_folder(const std::string &path)
{
    if (path.empty() || path.find("/xfce") != std::string::npos) {
        return;
    }
    std::string folder = path;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        const auto slash = folder.find_last_of(kPathSeparators);
        if (slash == std::string::npos) {
            return;
        }
        if (slash == 0) {
            folder = folder.substr(0, 1);
#ifdef _WIN32
        } else if (slash == 2 && folder[1] == ':') {
            // "C:\city.cty" lives in the drive root, not in "C:".
            folder.resize(3);
#endif
        } else {
            folder = folder.substr(0, slash);
        }
    }
    strip_trailing_separators(folder);
    if (folder.empty() || !path_is_absolute(folder) || folder.find("/xfce") != std::string::npos) {
        return;
    }
    const std::string dir = city_config_dir();
    if (dir.empty()) {
        return;
    }
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return;
    }
    std::ofstream out(dir + "/last-folder", std::ios::trunc);
    if (!out) {
        return;
    }
    out << folder << '\n';
}

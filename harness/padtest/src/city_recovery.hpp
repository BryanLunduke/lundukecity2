// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.
//
// C-1 (round 15): autosave and crash recovery.
//
// Each running City writes its own recovery file,
// $XDG_STATE_HOME/lunduke-city-2/recovery-<time>-<pid>.cty (else
// ~/.local/state/lunduke-city-2; on Windows, %LOCALAPPDATA%\lunduke-city-2),
// with a small ".info" sidecar naming the city,
// the file it came from, and when it was written. The city file is written
// by the engine's atomic save, so a crash mid-write keeps the previous
// autosave. A clean save, Discard, or a quit that answered the unsaved-city
// question removes this session's file. A crash, an OOM kill, or a logout
// (SIGTERM/SIGHUP) leaves it, and the next start offers it back.
//
// Header-only so every test that builds app_window.cpp picks it up.

#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace city_recovery {

struct Entry {
    std::string path;        // recovery .cty
    std::string city_name;   // as shown in the title
    std::string origin;      // file the city came from, may be empty
    std::string date_text;   // in-game date, e.g. "Mar 1923"
    long long written = 0;   // wall-clock seconds of the autosave
};

// A path is usable only when every existing component is a real directory
// owned by this user (no symlinks), like Paint's recovery folder.
// On Windows the folder just has to be a real directory (not a reparse point).
inline bool owned_real_dir(const std::string &path)
{
#ifdef _WIN32
    const DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    struct stat st;
    return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) &&
           st.st_uid == geteuid();
#endif
}

inline std::string state_base()
{
#ifdef _WIN32
    const char *local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && local[0] != '\0') {
        return local;
    }
    const char *home = std::getenv("USERPROFILE");
    if (home == nullptr || home[0] == '\0') {
        return {};
    }
    return std::string(home) + "\\AppData\\Local";
#else
    const char *xdg = std::getenv("XDG_STATE_HOME");
    if (xdg != nullptr && xdg[0] == '/') {
        return xdg;
    }
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] != '/') {
        return {};
    }
    return std::string(home) + "/.local/state";
#endif
}

// The recovery folder, created 0700 when missing. Empty when it cannot be
// used safely (a symlink, someone else's folder, or no home).
inline std::string dir(bool create = true)
{
    const std::string base = state_base();
    if (base.empty()) {
        return {};
    }
    const std::string out = base + "/lunduke-city-2";
    struct stat st;
    if (lstat(out.c_str(), &st) != 0) {
        if (!create) {
            return {};
        }
        // Create the parents with the usual 0700 for a state tree.
        std::string partial;
        for (std::string::size_type i = 1; i <= base.size(); ++i) {
            const bool boundary = i == base.size() || base[i] == '/' || base[i] == '\\';
            if (boundary) {
                partial = base.substr(0, i);
#ifdef _WIN32
                if (partial.size() == 2 && partial[1] == ':') {
                    continue;
                }
                if (_mkdir(partial.c_str()) != 0 && errno != EEXIST) {
                    return {};
                }
#else
                if (::mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST) {
                    return {};
                }
#endif
            }
        }
#ifdef _WIN32
        if (_mkdir(out.c_str()) != 0 && errno != EEXIST) {
            return {};
        }
#else
        if (::mkdir(out.c_str(), 0700) != 0 && errno != EEXIST) {
            return {};
        }
#endif
    }
    if (!owned_real_dir(out)) {
        return {};
    }
#ifndef _WIN32
    ::chmod(out.c_str(), 0700);
#endif
    return out;
}

inline std::string info_path(const std::string &recovery)
{
    return recovery + ".info";
}

// A fresh recovery file name for this process.
inline std::string new_recovery_path()
{
    const std::string d = dir(true);
    if (d.empty()) {
        return {};
    }
    char name[96];
#ifdef _WIN32
    std::snprintf(name, sizeof(name), "\\recovery-%lld-%d.cty", static_cast<long long>(std::time(nullptr)),
                  static_cast<int>(_getpid()));
#else
    std::snprintf(name, sizeof(name), "/recovery-%lld-%d.cty", static_cast<long long>(std::time(nullptr)),
                  static_cast<int>(::getpid()));
#endif
    return d + name;
}

inline bool is_recovery_name(const std::string &name)
{
    const std::string prefix = "recovery-";
    const std::string suffix = ".cty";
    return name.size() > prefix.size() + suffix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Strip control characters and keep a sidecar line short.
inline std::string clean_line(const std::string &text)
{
    std::string out;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            continue;
        }
        out.push_back(c);
    }
    if (out.size() > 4096) {
        out.resize(4096);
    }
    return out;
}

inline bool write_info(const std::string &recovery, const std::string &city_name, const std::string &origin,
                       const std::string &date_text)
{
    const std::string path = info_path(recovery);
    const std::string tmp = path + ".tmp";
    const std::string body = "name=" + clean_line(city_name) + "\norigin=" + clean_line(origin) +
                             "\ndate=" + clean_line(date_text) + "\nwritten=" +
                             std::to_string(static_cast<long long>(std::time(nullptr))) + "\n";
#ifdef _WIN32
    FILE *out = std::fopen(tmp.c_str(), "wb");
    if (out == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(body.data(), 1, body.size(), out) == body.size();
    std::fclose(out);
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
#endif
}

inline Entry read_entry(const std::string &recovery)
{
    Entry e;
    e.path = recovery;
#ifdef _WIN32
    const std::string info = info_path(recovery);
    WIN32_FILE_ATTRIBUTE_DATA info_attr;
    if (!GetFileAttributesExA(info.c_str(), GetFileExInfoStandard, &info_attr) ||
        (info_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (info_attr.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return e;
    }
    const unsigned long long info_size =
        (static_cast<unsigned long long>(info_attr.nFileSizeHigh) << 32) | info_attr.nFileSizeLow;
    if (info_size > 16384) {
        return e;
    }
#else
    struct stat st;
    if (lstat(recovery.c_str(), &st) == 0) {
        e.written = static_cast<long long>(st.st_mtime);
    }
    struct stat ist;
    const std::string info = info_path(recovery);
    if (lstat(info.c_str(), &ist) != 0 || !S_ISREG(ist.st_mode) || ist.st_size > 16384) {
        return e;
    }
#endif
    std::ifstream in(info);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = clean_line(line.substr(eq + 1));
        if (key == "name") {
            e.city_name = value;
        } else if (key == "origin") {
            e.origin = value;
        } else if (key == "date") {
            e.date_text = value;
        } else if (key == "written") {
            e.written = std::atoll(value.c_str());
        }
    }
    return e;
}

// Recovery files left by other runs, newest first. own_path (this run's
// file) is never listed.
inline std::vector<Entry> list(const std::string &own_path = std::string())
{
    std::vector<Entry> out;
    const std::string d = dir(false);
    if (d.empty()) {
        return out;
    }
#ifdef _WIN32
    const std::string pattern = d + "\\*";
    WIN32_FIND_DATAA found;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        const std::string name = found.cFileName;
        if (!is_recovery_name(name)) {
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }
        const std::string path = d + "\\" + name;
        if (path == own_path) {
            continue;
        }
        out.push_back(read_entry(path));
    } while (FindNextFileA(handle, &found));
    FindClose(handle);
#else
    DIR *dp = ::opendir(d.c_str());
    if (dp == nullptr) {
        return out;
    }
    while (dirent *ent = ::readdir(dp)) {
        const std::string name = ent->d_name;
        if (!is_recovery_name(name)) {
            continue;
        }
        const std::string path = d + "/" + name;
        if (path == own_path) {
            continue;
        }
        struct stat st;
        if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid()) {
            continue;
        }
        out.push_back(read_entry(path));
    }
    ::closedir(dp);
#endif
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
        if (a.written != b.written) {
            return a.written > b.written;
        }
        return a.path > b.path;
    });
    return out;
}

inline void remove(const std::string &recovery)
{
    if (recovery.empty()) {
        return;
    }
#ifdef _WIN32
    DeleteFileA(recovery.c_str());
    DeleteFileA(info_path(recovery).c_str());
#else
    ::unlink(recovery.c_str());
    ::unlink(info_path(recovery).c_str());
#endif
}

// "Harbor Town (Harbor.cty), Mar 1923 - autosaved Fri 16 Oct 14:05"
inline std::string describe(const Entry &e)
{
    std::string text = e.city_name.empty() ? std::string("Unnamed city") : e.city_name;
    if (!e.origin.empty()) {
        const auto slash = e.origin.find_last_of("/\\");
        text += " (" + (slash == std::string::npos ? e.origin : e.origin.substr(slash + 1)) + ")";
    } else {
        text += " (never saved)";
    }
    if (!e.date_text.empty()) {
        text += ", " + e.date_text;
    }
    if (e.written > 0) {
        const std::time_t t = static_cast<std::time_t>(e.written);
        struct tm tmv;
#ifdef _WIN32
        const bool have_tm = localtime_s(&tmv, &t) == 0;
#else
        const bool have_tm = localtime_r(&t, &tmv) != nullptr;
#endif
        if (have_tm) {
            char buf[64];
            if (std::strftime(buf, sizeof(buf), "%a %e %b %H:%M", &tmv) > 0) {
                std::string when = buf;
                // %e pads a one-digit day with a space.
                const auto dbl = when.find("  ");
                if (dbl != std::string::npos) {
                    when.erase(dbl, 1);
                }
                text += " \u2014 autosaved " + when;
            }
        }
    }
    return text;
}

} // namespace city_recovery

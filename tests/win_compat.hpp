// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.
//
// The few POSIX calls the tests use that MinGW does not provide. Nothing
// here is compiled outside Windows.
#pragma once

#ifdef _WIN32
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <random>

inline char *mkdtemp(char *tmpl)
{
    const size_t len = std::strlen(tmpl);
    if (len < 6 || std::strcmp(tmpl + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return nullptr;
    }
    static const char alnum[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (size_t i = len - 6; i < len; ++i) {
            tmpl[i] = alnum[rd() % (sizeof(alnum) - 1)];
        }
        if (_mkdir(tmpl) == 0) {
            return tmpl;
        }
        if (errno != EEXIST) {
            return nullptr;
        }
    }
    return nullptr;
}

#include <string>

// Tests isolate the config folder by pointing HOME at a temp directory. On
// Windows that folder comes from %APPDATA% and %LOCALAPPDATA%, so follow
// HOME there too; otherwise a test run writes into the real profile.
inline int setenv(const char *name, const char *value, int overwrite)
{
    if (!overwrite && std::getenv(name) != nullptr) {
        return 0;
    }
    if (std::strcmp(name, "HOME") == 0) {
        const std::string home = value;
        _putenv_s("USERPROFILE", value);
        _putenv_s("APPDATA", (home + "\\AppData\\Roaming").c_str());
        _putenv_s("LOCALAPPDATA", (home + "\\AppData\\Local").c_str());
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

inline int unsetenv(const char *name)
{
    return _putenv_s(name, "") == 0 ? 0 : -1;
}
#endif

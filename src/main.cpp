// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Bryan Lunduke
// See COPYING and NOTICE.

#include "shell.hpp"

#include <cstdio>
#include <cstring>

// On Windows this supplies WinMain and hands main() UTF-8 arguments.
#include <SDL3/SDL_main.h>

#ifdef _WIN32
#include <cstdlib>
#endif

#ifndef LUNDUKE_CITY_PACKAGE_VERSION
#define LUNDUKE_CITY_PACKAGE_VERSION "2.0.0"
#endif

int main(int argc, char **argv)
{
    // Release nov24 #19: --version and --help print and exit before any
    // window opens. Other dash arguments are still ignored.
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-V") == 0) {
            std::printf("Lunduke City 2 %s\n", LUNDUKE_CITY_PACKAGE_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: lunduke-city-2 [OPTION] [CITY.cty]\n"
                        "Lunduke City 2, a city-building game. Opens CITY.cty when given.\n\n"
                        "  -h, --help     show this help and exit\n"
                        "  -V, --version  show the version and exit\n");
            return 0;
        }
    }
#ifdef _WIN32
    // Render text through fontconfig + the fonts shipped next to the exe
    // (etc/fonts, share/fonts) so Windows matches the Linux look and metrics.
    // Setting PANGOCAIRO_BACKEND=win32 beforehand restores native fonts.
    if (std::getenv("PANGOCAIRO_BACKEND") == nullptr) {
        _putenv_s("PANGOCAIRO_BACKEND", "fc");
    }
#endif
    return run_shell(argc, argv);
}

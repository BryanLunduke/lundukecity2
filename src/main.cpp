// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#include "shell.hpp"

// On Windows this supplies WinMain and hands main() UTF-8 arguments.
#include <SDL3/SDL_main.h>

#ifdef _WIN32
#include <cstdlib>
#endif

int main(int argc, char **argv)
{
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

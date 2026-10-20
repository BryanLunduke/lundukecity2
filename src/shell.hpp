// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Lunduke City authors
// See COPYING and NOTICE.

#pragma once

// SDL3 front end. Returns 0 on a normal exit, 1 if the window cannot be
// created, and 2 when a headless smoke check fails.
int run_shell(int argc, char **argv);

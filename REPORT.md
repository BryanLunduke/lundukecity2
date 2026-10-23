# Lunduke City 2: Windows x86_64 cross-build and Wine smoke test

Base: master `bd4feb5` (PR #3 merged), fresh clone at `/workspace/city2-win`.
Nothing committed or pushed. All source changes are in `windows.patch`.
Times are America/Chicago (CDT), Tue 2026-10-20.

## Outcome

* `lunduke-city-2.exe` cross-compiles from the box (Debian mingw-w64 GCC 14 posix, meson 1.7.0).
  It runs under Wine 10.0, both headless and on X11.
* Self-contained zip: `dist/lunduke-city-2-windows-x86_64.zip`, **13,282,584 bytes (12.7 MiB)**,
  sha256 `c4d6c253…75fe34b0`. Unpacked it is 28 MB in 213 files.
* Wine test results:
  * `meson test` (exe_wrapper = wine): **19 OK, 0 fail, 1 skip** (round11 needs a full disk, same as on Linux).
    Four POSIX-only tests are not built for Windows.
  * Smoke, UI-test and all 10 demo/screenshot modes pass, with no Pango criticals.
  * A live 1280x800 window works: menus, dialogs, the native Save dialog, a save to a UTF-8 path,
    opening from argv, and the crash-recovery prompt.
* The same patch keeps Linux green: native build 0 warnings, `meson test` **23 OK / 1 skip**.
* Reproducible with one script: `packaging/windows/build-windows.sh` ran from scratch in about 80 s here
  (sysroot, build, Wine tests and zip). A CI workflow is included in the patch.

## Toolchain and sysroot

* **Host compiler.** Debian `mingw-w64` posix-thread compilers (`x86_64-w64-mingw32-g{cc,++}-posix` 14).
  Their CRT is msvcrt.
* **Wine.** `wine64` 10.0 runs it, with a private `WINEPREFIX=/workspace/city2-win/wineprefix`.
* **Sysroot source.** MSYS2 **mingw64** (msvcrt) packages. I did not use ucrt64, so the CRT matches the Debian compilers.
* **Signature check.** `mingw64.db.sig` is checked with gpg the way pacman does it:
  * A throwaway local key lsigns the MSYS2 master keys listed in `msys2-trusted` from MSYS2-keyring.
  * Those master keys certify the packager key.
  * Result: signed by `5F944B027F7FE2091985AA2EFA11531AA0AA7F57` (Christoph Reiter), `TRUST_FULLY`.
  * The signer is also checked against `msys2-revoked`.
* **Package checks.** Each package's sha256 is checked against the signed db before unpacking.
* **Package set.** The dependency closure of `sdl3 cairo pango ttf-dejavu` is 31 packages; gcc and python are skipped.
  Key versions: sdl3 3.4.18, cairo 1.18.6, pango 1.58.2, harfbuzz 14.6.0, glib2 2.90.1, fontconfig 2.18.3,
  freetype 2.15.0 and libstdc++/libgcc 16.2.0. The full list is in `sysroot/packages.json`, which is also shipped as
  `share/licenses/msys2-packages.json`.
* **pkg-config.** `.pc` prefixes are rewritten to the sysroot. The host's `pkg-config` is used with `pkg_config_libdir`
  pointing only into the sysroot, so host libraries can't leak in.
* **C++ runtime.** MSYS2's DLLs come from GCC 16, our compiler is GCC 14. The exe links `-static-libgcc -static-libstdc++`,
  so its own C++ runtime never mixes with theirs. MSYS2's `libstdc++-6.dll` / `libgcc_s_seh-1.dll` ship only because
  cairo, harfbuzz and graphite2 import them.
* **Meson.** It uses the repo's `cross/mingw-w64-x86_64.txt` plus a generated overlay (`sysroot.ini`) that sets
  the posix compilers, pkg-config, sys_root and `exe_wrapper`. The repo file itself is unchanged.

## Patch summary (`/workspace/city2-win/windows.patch`, 35 files, +962/−201, `git apply` checked on a pristine tree)

**Engine (third_party/micropolis)**

* `stdafx.h` / `micropolis.h`: `NOMINMAX`, and the min/max templates are guarded by `_MSC_VER` instead of `_WIN32`,
  because MinGW has no `std::min` macros. CRLF line endings are preserved.
* `data_types.h`: **`Quad` is now `std::int64_t`** (it was `long`).
  * A real Windows bug: `long` is 32 bits on Windows (LLP64).
  * A treasury above $2,147,483,647 wrapped to debt in memory.
  * Test round5 caught it under Wine.
  * Linux is unchanged because `int64_t` is `long` there.
* `fileio.cpp`: a Windows branch for the atomic save.
  * Uses `_wopen(O_CREAT|O_EXCL|O_BINARY|O_NOINHERIT)` with random temp names, `_commit` and `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`.
  * UTF-8 to UTF-16 path conversion.
  * Stale-temp cleanup through `FindFirstFileW`.
  * The POSIX path is untouched.

**App (src/)**

* `city_session.*`, `graph_legend.hpp`, `chart.cpp`, `dialog_copy.hpp`, `shell_uitest.cpp`: money and population types
  change from `long` to `std::int64_t`, for the same LLP64 reason. The types are identical on Linux.
* `save_path.hpp`: on Windows, paths now split on `\` as well as `/`.
  * Without this, the folder of a city picked in the native dialog was never remembered, and Save As offered the wrong file name.
  * Only `/` counts on POSIX, where a backslash is a legal file-name character.
* `city_recovery.hpp`:
  * Uses `::stat` instead of `lstat` on Windows.
  * The strftime format avoids `%e`, which msvcrt lacks and which can hit its invalid-parameter handler.
  * Verified: the recovery prompt shows "Tue 20 Oct 20:59".
* `main.cpp`:
  * `#include <SDL3/SDL_main.h>` gives a GUI-subsystem `WinMain` that passes UTF-8 argv.
  * On Windows `PANGOCAIRO_BACKEND=fc` is set (unless the user already set it), so text uses the shipped fontconfig and DejaVu.
    Pango's win32 backend rendered garbage glyphs under Wine: see `shots/00-before-fontfix-win32-backend-garbled.png`.
* `src/meson.build`, Windows only:
  * `win_subsystem: 'windows'` and static libgcc/libstdc++.
  * `windows.compile_resources()` for the new `data/windows/lunduke-city-2.rc`: icon (`.ico` made from the 256 px PNG),
    manifest and VERSIONINFO.
  * The manifest sets activeCodePage UTF-8 (narrow `fopen`/`stat` take UTF-8), PerMonitorV2 DPI awareness, longPathAware,
    asInvoker, and Win10/11 supportedOS.

**Tests**

* New `tests/win_compat.hpp` provides `mkdtemp`, `setenv` and `unsetenv` for MinGW.
  `setenv("HOME")` also redirects `APPDATA`, `LOCALAPPDATA` and `USERPROFILE`. Without that, round7 wrote `last-folder`
  into the real profile, which I saw happen in the Wine prefix.
* round7, 8, 9, 10, 13 and 14 include that shim.
* round14 skips its POSIX mode-bit asserts on Windows. The temp-file and leftover checks still run.
* round7 skips its XFCE-home step on Windows.
* `production_binary` now parses PE sections (non-discardable) as well as ELF PT_LOAD.
  So the "no `/workspace`, no hook strings" release check now covers the exe.
* `engine_fixes`: the `__GLIBC_PREREQ` test is nested so non-glibc preprocessors don't choke. MinGW skips the mallinfo heap check.
* round4, 5 and 6 print 64-bit values with `PRId64`. There are now 0 warnings on both Windows and Linux.
* `tests/meson.build`: `regressions`, `round3`, `argv_open` and `round15` are built only when `host_machine.system() != 'windows'`.
  They need symlinks, chown, `DIR*`, rlimits, fork/wait and the X window tree.

**Reproducibility, new files**

* `packaging/windows/mingw64-sysroot.py`, `package.sh`, `build-windows.sh` and `fonts.conf`.
* `.github/workflows/windows.yml`.

## Package layout and DLLs

```
lunduke-city-2/
  bin/lunduke-city-2.exe (1.9 MB stripped, release) + 31 DLLs
  etc/fonts/fonts.conf          only share/fonts, Sans/sans-serif -> DejaVu Sans, cache in %LOCALAPPDATA%\fontconfig
  share/fonts/                  DejaVu Sans (4 styles) + Sans Mono (2)
  share/lunduke-city-2/micropolis-assets/   (found by the existing exe/../share/... lookup)
  share/licenses/               COPYING, NOTICE, README + every shipped MSYS2 package's license, msys2-packages.json
  README-windows.txt
```

The DLL closure comes from walking `objdump -p` imports recursively over the sysroot. There are 31 DLLs (19.9 MB):

* SDL3 (4.0 MB)
* libstdc++-6 (2.7 MB)
* libgio-2.0-0 (1.9 MB)
* libharfbuzz-0, libglib-2.0-0, libcairo-2, libiconv-2 (1.1–1.6 MB each)
* freetype, pixman, pcre2, fontconfig, pango, gobject, intl, png, expat, graphite2, fribidi, libgcc_s_seh,
  brotlicommon/dec, zlib1, bz2, pangoft2, pangowin32, pangocairo, thai, winpthread, brotli, ffi, datrie, gmodule

Everything else is a stock Windows system DLL: KERNEL32, USER32, GDI32, msvcrt, ole32, OLEAUT32, SHELL32, SHLWAPI,
ADVAPI32, DWrite, USP10, MSIMG32, IMM32, WINMM, SETUPAPI, VERSION, WS2_32, DNSAPI, IPHLPAPI and RPCRT4.
vulkan-loader is not shipped: SDL loads it only if a Vulkan renderer is requested and otherwise uses D3D11/12.

## Wine results (Xvfb :47, 1920x1200, private prefix)

* **Build-tree tests.** `meson test` (debug build and release build): 19 OK / 1 SKIP / 0 FAIL in about 11 s.
  * Before the fixes it was 16/3. round5 failed on the 32-bit `long` funds bug, production_binary was ELF-only, and round14 failed on umask.
  * The tests include engine_smoke, features, round4–14, menu_model, dialog_copy, hotspots, metainfo, and `ui_input` and
    `sdl_smoke` run on the real exe.
* **Packaged zip.** Unzipped to `…/wine-test/Spiele Städte/` (a space and non-ASCII in the path), with no `WINEPATH` and no
  `LUNDUKE_CITY_ASSETS`:
  * `LUNDUKE_CITY2_SMOKE=1` exits 0.
  * `LUNDUKE_CITY2_UI_TEST=1` exits 0.
  * All `LUNDUKE_CITY2_DEMO` modes write 1280x800 shots to `shots/demo-*.png`: window, menu, budget, evaluation, graphs,
    options, query, osk, gamepad, unsaved.
* **Live X11 run (winex11 + wined3d/llvmpipe), window 1280x800:**
  * The main window, the System and Windows menus, and the Budget, "Save changes?" and "Recover unsaved city?" dialogs all render correctly.
  * The native Wine IFileDialog opens for both Load and Save As. The game's "browse in the window" fallback panel shows behind it.
  * Saving to `C:\users\box\Documents\Täst City.cty` worked: the atomic `MoveFileExW` path and UTF-8 names both behave.
    `%APPDATA%\lunduke-city-2\last-folder` = `C:\users\box\Documents`, which confirms the backslash fix.
  * Relaunching with that path as argv opened the saved map.
  * An autosave recovery file appears in `%LOCALAPPDATA%\lunduke-city-2\`.
* **Screenshots** (`shots/`):
  * Best three: `02-wine-main-1280x800.png`, `03-wine-menu-system.png`, `04-wine-dialog-budget.png`.
  * Also: `05-wine-native-save-dialog.png`, `05-wine-native-open-dialog.png`, `06-wine-dialog-save-changes.png`,
    `07-wine-dialog-recover-unsaved.png`, `09-wine-open-utf8-argv.png`, `demo-*.png`, and
    `00-before-fontfix-win32-backend-garbled.png` (the "before" image).
* **Load.** Rendering uses roughly 4–5 cores of llvmpipe on Xvfb because there's no GPU. Real Windows would use D3D.

## Windows gaps (not fixed here)

1. **File dialogs.**
   * SDL3's native dialog works. Without a window manager it opens unparented at the top-left, which is fine on real Windows.
   * Save As does not pre-fill the proposed `<City>.cty` name. SDL3's API only takes a folder, so a pre-filled name needs
     `default_location` = folder + name. That is a cheap follow-up.
   * The window title stays "New City" after Save As. It's the same on Linux, but worth checking.
2. **%APPDATA% config.** Config goes to `%APPDATA%\lunduke-city-2` and recovery to `%LOCALAPPDATA%\lunduke-city-2`,
   both verified.
   * The paths come from `getenv` (ANSI). That is UTF-8 only because of the activeCodePage manifest, which needs Windows 10 1903+.
   * On older Windows, a non-ASCII user name would break them. Switching to `SHGetKnownFolderPath` plus `_wgetenv` would make them robust.
   * `std::filesystem` with narrow strings has the same caveat.
3. **Fonts.**
   * Bundled DejaVu goes through fontconfig, so first launch writes a small cache to `%LOCALAPPDATA%\fontconfig`.
   * Windows system fonts are deliberately not scanned. That keeps the look identical and the start fast, but it means
     there is no fallback for CJK or emoji in city names.
   * `side_widgets.cpp` uses the cairo toy API (`cairo_select_font_face("Sans")`) for the R/C/I labels. On Windows that goes
     through cairo's win32 font backend, not fontconfig. It renders acceptably under Wine, but it's the one text path that isn't pinned to DejaVu.
4. **Gamepad, XInput and Steam Input.**
   * SDL3 handles XInput, RawInput, WGI and HIDAPI. The `demo-gamepad` virtual pad path passes, but there's no physical pad
     in this box, so real-device testing is still needed.
   * Steam Input on Windows works through SDL's Steam virtual gamepad handling. It needs a test in the Steam client
     (Big Picture, and the Deck's Proton path).
5. **High DPI.**
   * The manifest declares PerMonitorV2, and SDL3 is DPI-aware on Windows regardless. Window coordinates are therefore physical pixels.
   * The game never calls `SDL_GetWindowDisplayScale()`, so at 150–200% scaling the UI will be small but sharp.
   * The fix is to scale the cairo/UI layer by the display scale and handle `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`.
     I couldn't verify this under Wine at 96 DPI.
6. **Tests not built on Windows:** regressions, round3, round15 (symlink, ownership and group-keep save semantics) and argv_open.
   The Windows save path deserves its own tests: replace a read-only file, a file open in another process
   (sharing violation on MoveFileExW), and a OneDrive-redirected Documents folder.
7. **Not done.**
   * No code signing or installer (zip only).
   * No crash-dump handling.
   * Not tested on real Windows. The CI `windows-smoke` job is meant to cover that.
   * MSVC is not supported; the build is MinGW only.

## Reproducibility

* **Script:** `packaging/windows/build-windows.sh [WORK_DIR]` (in the patch). It does four things:
  1. builds the signed sysroot (`mingw64-sysroot.py`)
  2. runs `meson setup --cross-file cross/mingw-w64-x86_64.txt --cross-file WORK/sysroot.ini --buildtype=release` and ninja (`JOBS`, default 4)
  3. runs `meson test` under Wine, inside `xvfb-run` if there's no DISPLAY
  4. runs `package.sh`, which produces the dist folder and zip
* **Verified end to end** into `/workspace/city2-win/repro/`: 31 packages verified, 19/1/0 tests, zip produced.
  The deliverable `dist/` is packaged from that release build.
* **Host packages:** `g++-mingw-w64-x86-64-posix`, `gcc-mingw-w64-x86-64-posix`, `meson`, `ninja-build`, `pkg-config`,
  `python3`, `gnupg`, `zstd`, `zip` (optional; it falls back to Python), `wine64` and `xvfb`.
* **CI:** `.github/workflows/windows.yml`.
  * The `cross` job (ubuntu-24.04) installs those packages, caches MSYS2 package downloads, runs the script and uploads the zip.
  * The `windows-smoke` job (windows-2022) unzips it and runs `LUNDUKE_CITY2_SMOKE` and `LUNDUKE_CITY2_UI_TEST` on real Windows.
  * It has not been run, since nothing was pushed.
* **Pinning follow-up.** The script resolves against the *current* signed mingw64 db, so a later run may pick newer packages.
  For bit-stable releases, commit a lock file (the `packages.json` file names plus sha256) and have the script fetch those
  exact files. Old versions stay on repo.msys2.org.

## Housekeeping

* About 2 GB under `/workspace/city2-win` is scratch: `wineprefix/` 780 MB, `repro/` 1 GB (including its own prefix),
  `build-win/`, `build-linux/`, `wine-test/`, `sysroot/` and `msys2/`.
* Local ignores for those are in `.git/info/exclude`.
* `/workspace/city2-verify` and `/workspace/lcos-*` were not touched.
* Processes I started (Xvfb :47, the Wine apps, the wineservers for my two prefixes) were stopped by exact PID or by
  `wineserver -k` on my own prefix. None are left running.

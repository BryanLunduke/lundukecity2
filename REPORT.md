# Lunduke City 2 verification report (master @ d203d8c, v2.0.0)

Date: Tue Oct 20 2026, 7:20–7:40 PM CDT. Box: Debian 13, gcc 14.2, SDL 3.2.10, cairomm 1.14.5, pango 1.56.3.
Clone: `/workspace/city2-verify` (read-only use: no commits, pushes or PRs; the git tree is clean except for untracked verification dirs).

## 1. Host build and tests

* The build deps were already installed, so nothing was added.
* `meson setup build && ninja -C build -j4` succeeded (128/128 targets, about 13 s).
  * One warning: `tests/round15.cpp:104` `-Wrange-loop-construct`. The loop copies `const std::string d`; it should use `const auto &`.
* `meson test`: **19 OK, 0 fail, 1 skipped (round11, exit 77)**, out of 20 tests including `sdl_smoke`. `appstreamcli` is not installed, so the `metainfo` test was not registered.
* `LUNDUKE_CITY2_SMOKE=1` with the dummy video driver exits 0. The four `LUNDUKE_CITY2_DEMO` modes (window, menu, budget, gamepad) all wrote PNGs and exited 0.

## 2. Screenshots (`shots/`)

The real windows were captured from a private Xvfb (:47 with no WM for windowed runs, :48 at 1280x800 with xfwm4 for fullscreen) using ffmpeg x11grab. The city is Dullsville, saved from the smoke run (`shots/dullsville.cty`).

| File | What | Notes |
|---|---|---|
| 01-main-1280x800.png | Main window, Dullsville loaded | Looks good. The welcome line wraps to 2 lines at 1280 wide. |
| 02-menu-system-open.png | System menu open | **BUG A**: the drop-down is painted *under* the welcome bar, the palette and the map. Only slivers show at the left. |
| 02b-menu-hover-over-palette.png | Same, with the mouse over the hidden items | |
| 03-click-menu-item-over-map.png | Clicked "Evaluation" in the Windows menu, where it sits over the map | **BUG A**: the click only closed the menu and no dialog opened. |
| 04-dialog-graphs.png | Graphs | **BUG C**: the legend overflows. Rows run under the Close button and past the bottom of the dialog. |
| 05-dialog-evaluation.png | Evaluation | **BUG E**: labels and sections from 0.9.5 are missing. |
| 06-dialog-power-overlay.png | Power overlay | OK |
| 07-dialog-budget.png | Budget (opened from the menu) | **BUG E**: fields and labels are missing. **BUG D**: the title is clipped. |
| 08-palette-hover-query.png | Hover over the "?" tool | There is no tooltip popup. The name shows only in the bottom bar. |
| 09-dialog-query.png | Query a residential zone | **BUG B**: the Close button is drawn outside the 520 px panel. The first line still contains " — " (BUG H). |
| 10-resized-1600x900.png | Window resized to 1600x900 | Layout scales correctly. The RCI bars stretch very tall (cosmetic). |
| 11-resized-1024x640.png | 1024x640 | OK |
| 12-resized-1024x640-menu.png | Menu at 1024x640 | BUG A again |
| 13-resized-1024x640-graphs.png | Graphs at 1024x640 | BUG C, worse at this size |
| 14-steamdeck-fullscreen.png | `SteamDeck=1` under xfwm4 at 1280x800 | `_NET_WM_STATE_FULLSCREEN` is set and the size is 1280x800. The recovery dialog after the earlier SIGTERM works. |
| 15-pad-newcity-osk.png | New City opened from a virtual gamepad, with the on-screen keyboard | **BUG G**: OSK keys overlap and labels are truncated ("spac", "bksp"). |
| 16-pad-system-menu-cursor.png | Gamepad menu navigation | BUG A means the gamepad user cannot see the menu or its cursor at all. |
| 20–26-pad-*.png | Scripted virtual-gamepad session | See section 3 |
| 30-sniper-sdk-demo-window.png, 31-sniper-sdk-x11-menu.png | Built and run inside the sniper SDK (dummy driver, and X11 through Xvfb) | |
| 32-sniper-platform-deck-window.png | Bundle running inside the real SteamLinuxRuntime_sniper (pressure-vessel) with `SteamDeck=1` on X11 | Renders correctly with DejaVu Sans |
| demo-*.png | Built-in demo modes | demo-menu and demo-gamepad also show BUG A, so the PR's own screenshots already showed it. |

Comparison with the gtkmm look (`/workspace/xplat-eval/city-shots/gtk-full.png`):
* Tiles, palette art, minimap, RCI bars and menu bar match closely.
* Deliberate layout changes:
  * The status line (funds, name, speed, date) moved from the top info bar to a bottom status bar.
  * The welcome notice moved from the bottom to the top.
  * The real GTK scrollbars became thin, non-interactive scroll indicators (`kHScroll`/`kVScroll` are unused at `src/shell_draw.cpp:760-761`).
  * Dialogs are now in-window overlays instead of separate windows.

## 3. Gamepad (SDL virtual joystick)

There is no `/dev/uinput`, so the test used an in-process SDL virtual joystick.
* I wrote a **local-only** harness patch in `harness/padtest/` (a copy of the source with `LUNDUKE_CITY2_PADSCRIPT` added to `Shell::loop`). It is not for upstream.
* The scripts are `harness/pad1.txt` to `pad4.txt`.
* Note: SDL 3.2 button strings are `a/b/x/y`, not `south/east`.

Results:
* LB/RB cycles tools, left stick moves the cursor, Start opens the menu, D-pad moves the menu cursor, A activates the item (it opened Budget and New City), and B closes. **These work.**
* A on the map builds one tile (road −$10). The status bar shows the button hints.
* **BUG F1: no drag-building with the pad.** Holding A while moving the stick about 12 tiles cost only one more tile ($4,990 → $4,979). The cause is `src/shell.cpp:838-843`: A calls `map_.press()` and then `map_.release()` straight away. Roads, rails and wires need a hold-to-drag (press on A down, motion while the stick moves, release on A up).
* **BUG F2: budget sliders and the D-pad.** Right on a focused slider changes the value *and* moves focus to the next control (tax 7→8, then focus jumps from 8001 to 8002). In `src/shell.cpp:878-911`, the `break` only leaves the `for` loop, so `move_focus()` at line 911 still runs. Each press adjusts one step and then leaves the slider.
* **BUG F3 (keyboard): arrow keys can't adjust budget sliders.** `src/shell.cpp:730-733` maps Left/Right to `move_focus` only. GTK scales took the arrow keys.
* The cursor isn't limited to the map. Pressing A with the cursor over the palette or minimap sends off-map coordinates to `map_.press` instead of hitting the widget.
* The smoke test's gamepad check is lenient. If the virtual button does nothing, it pushes a synthetic event (`src/shell_actions.cpp:1346-1352`), so a broken pad path could still pass.
* Not tested: real Steam Deck hardware, Steam Input remapping, gyro or trackpads, and the Steam on-screen keyboard (`SDL_StartTextInput`). The box has no Deck session.

## 4. Steam Linux Runtime 3.0 (sniper)

* Image: `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest`, pulled with rootless podman. BUILD_ID 3.0.20260928.262393, gcc 10.3, meson 1.0.1. Containers ran with `--cpus=4 --memory=4g`.
* SDK has: SDL3 **3.4.16**, cairo 1.16.0, pango/pangocairo 1.46.2, fontconfig, freetype, libpng, DejaVu fonts.
* SDK lacks: **cairomm-1.0 and sigc++-2.0 dev files**. Configure fails as shipped (`meson.build:14`).
* The platform runtime (`/workspace/xplat-eval/steam/SteamLinuxRuntime_sniper`) also has **no libcairomm and no libsigc**.
* Workaround I tested:
  * Bullseye `libcairomm-1.0-dev 1.12.2` and `libsigc++-2.0-dev 2.10.4` debs were fetched over https from archive.debian.org. Port 80 is blocked here, so apt inside the container fails. They are kept in `harness/debs/`.
  * They are installed with `dpkg -i` inside the SDK. The script is `harness/sniper-build.sh` and the build goes to `build-sniper/`.
* Result:
  * `lunduke-city-2` **compiles and links** against SDL 3.4.16 and cairomm 1.12.
  * **`tests/engine_fixes.cpp:104` fails to compile**: `mallinfo2` needs glibc 2.33, and sniper has 2.31. Use `mallinfo()` or guard it with `__GLIBC_PREREQ(2,33)`.
  * The other 19 tests: 16 OK, 1 skip (round11), 2 failures caused by the environment:
    * `regressions` failed because the container runs as root and root ignores chmod 0555. It **passes** as uid 65534.
    * `argv_open` needs `DISPLAY` plus `dbus-run-session` and `xwininfo`, which the SDK lacks. Exit 127 comes from the test harness, not the game.
  * SDK runs: smoke exits 0, the demo shot exits 0, and an X11 run through Xvfb exits 0.
* Real platform (pressure-vessel `run --`):
  * The unbundled binary gives `error while loading shared libraries: libcairomm-1.0.so.1` (exit 127).
  * **The bundled build works.** `sniper-bundle/` holds the binary, `lib/libcairomm-1.0.so.1` and `lib/libsigc-2.0.so.0`, `share/lunduke-city-2/micropolis-assets`, and a `run.sh` that sets LD_LIBRARY_PATH and LUNDUKE_CITY_ASSETS.
  * With the bundle, the smoke run (dummy) exits 0 and the X11 run with `SteamDeck=1` exits 0 (shot 32).
  * Fonts come from the host through pressure-vessel (3698 fonts; "Sans" resolves to DejaVu Sans).
* What needs bundling or changing for Steam:
  1. Preferred: **drop cairomm** and use the plain cairo C API. There are only 59 `Cairo::` uses, mostly in `tool_palette.cpp` (20) and `side_widgets.cpp` (9). After that, sniper provides every dependency and nothing needs bundling.
  2. Otherwise, ship `libcairomm-1.0.so.1` and `libsigc-2.0.so.0` (both LGPL) in `lib/` with an `$ORIGIN/lib` rpath. Or build cairomm and sigc++ as static meson subprojects.
  3. Fonts: the game asks for "Sans 13/14" and "Sans Bold 18". To make the look the same on every host, bundle DejaVu Sans and load it through fontconfig (`FcConfigAppFontAddDir`). The Windows build needs this as well.
  4. ALSA warnings in the container are harmless (no sound card). Use SDL_AUDIODRIVER=dummy for headless CI.

## 5. Prioritized fixes for a follow-up cloud agent

**P0: menus are broken for mouse, touch and gamepad**
1. **BUG A: the drop-down menu is drawn and hit-tested beneath everything.**
   * `Shell::draw()` calls `draw_menu()` first (`src/shell_draw.cpp:706`), so the palette, minimap, map, status bar and welcome bar paint over the drop-down (lines 715-803).
   * `Shell::hit()` walks hotspots from last to first (`src/shell_actions.cpp:1224-1233`), so the map, palette and minimap hotspots (ids 1-3) beat the menu items. `on_hotspot` then just closes the menu (`src/shell_actions.cpp:1158-1161`).
   * Fix: split the function into `draw_menubar()` (keep it first) and `draw_menu_dropdown()`, and call the drop-down **last** in `draw()`, after the dialog and gamepad cursor. Its `push_hot` calls then come last too. Draw the Recent submenu the same way.
   * Add a test that renders with the menu open and checks a pixel, or asserts that `hit()` returns `kMenuItem+…` at a drop-down point over the map.

**P1: Deck usability and lost functionality**
2. **F1: gamepad drag-building.**
   * On A down: `map_.press`. While A is held: `map_.motion` as the cursor moves. On A up (handle `SDL_EVENT_GAMEPAD_BUTTON_UP`): `map_.release`.
   * Code: `src/shell.cpp:838-843`, plus the cursor update at `src/shell.cpp:441-447`.
3. **F2 and F3: slider adjustment.**
   * Gamepad: return after changing a slider value (`src/shell.cpp:878-911`).
   * Keyboard: Left/Right should adjust a focused slider and only Up/Down/Tab should move focus (`src/shell.cpp:730-733`).
4. **BUG E: Evaluation and Budget lost content compared with 0.9.5.**
   * Evaluation (`src/shell_draw.cpp:656-676`) is missing:
     * the "Score", "Public opinion", "Worst problems" and "Statistics" captions
     * "Is the mayor doing a good job?" with Yes/No %
     * "Change since the last evaluation"
     * net migration and assessed value
     * the "Category:" and "Difficulty:" labels
   * Budget (`src/shell_draw.cpp:593-636`) is missing previous funds, the "Estimated taxes / Taxes collected" wording, per-fund request vs. funded amounts, the closing note, and "Next year's tax rate".
   * Port the strings and fields from `git show f85446f:src/evaluation_window.cpp` (lines 77-203) and `f85446f:src/budget_window.cpp` (lines 129-333).
5. **BUG C: the Graphs legend overlaps the Close button** (`src/shell_draw.cpp:643-652`). Lay the legend out in 2 or 3 columns, or size the chart as `dh - 84 - legend_h - 64`.
6. **BUG B: the Query Close button sits outside the panel** (`src/shell_draw.cpp:695-699`). `buttons_at` uses `dw` (860) while the panel is `min(dw,520)`. Pass the panel width to `buttons_at` (make it a parameter), or centre the 520 panel and use its x and width.

**P2: polish**
7. **BUG G: OSK layout** (`src/shell_draw.cpp:187-205`).
   * "space" and "bksp" are drawn 1.4× wide but the next keys stay on the 10-column grid, so they overlap.
   * Rows are not QWERTY (z sits on the home row). There is no shift or capitals, and no `-` or `'`.
   * There is no visible focus highlight on keys (the `button(..., false)` call).
   * Keys are 32 px tall, which is small for a 7" Deck touchscreen. Aim for 48 px or more.
8. **BUG D: dialog titles are clipped.** "Sans Bold 18" is drawn at y+6 inside a 32 px band (`src/shell_draw.cpp:182-183`). Make the band 40 px, or centre the text using the measured height.
9. **BUG H: `query_body()` em-dash split** (`src/shell.cpp:194`). `" — "` is 5 bytes in UTF-8 but the code compares 3 and skips 3, so the first line keeps the dash. Use `std::string sep = " — "; text.compare(i, sep.size(), sep)` and skip `sep.size()`.
10. **Gamepad cursor:** when the cursor is outside the map, hit-test it against hotspots (palette, minimap, dialog buttons) instead of always calling `map_.press`.
11. **Sniper portability:**
    * Make `tests/engine_fixes.cpp:104` work on glibc 2.31.
    * `argv_open` should skip (77) when `dbus-run-session` or `xwininfo` is missing, instead of failing.
    * Remove the cairomm dependency, or vendor it as a static subproject (see section 4).
    * Bundle DejaVu Sans.
12. **Smoke test strictness:** don't fall back to a synthetic event when the virtual pad does nothing (`src/shell_actions.cpp:1346-1352`).

**P3**
13. Restore palette tooltips (popup near the cursor). Make the scroll indicators draggable, or restore real scrollbars on desktop.
14. Hotspot ids are duplicated as constants in 3 files (`shell.cpp:31`, `shell_draw.cpp:25`, `shell_actions.cpp:48`). Move them into `shell_state.hpp`.
15. `tests/round15.cpp:104` range-loop copy warning.
16. Install `appstreamcli` in CI so the `metainfo` test runs.

## 6. Artifacts and cleanup

* `build/` is the host build. `build-sniper/` and `build-sniper-stock/` are the SDK builds (the stock one is the failed configure).
* `sniper-bundle/` is a runnable Steam-style bundle.
* `harness/` holds the pad-script harness copy, scripts, debs, `sniper-build.sh` and the HOME dirs used for runs. `home/` is the isolated HOME for the GUI runs.
* `run.sh` and `shot.sh` are helper scripts.
* All Xvfb, xfwm4 and game processes I started were stopped by PID. No VMs were started.
* The podman image `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest` (about 2 GB) is left in rootless podman storage. Remove it with `podman rmi` if the space is needed.
* Not attempted: a Windows MinGW build or Wine smoke test (out of scope for this pass).

---

## PR #2 verification (branch `cursor/sdl-menu-input-sniper-577f` @ 54d112f)

Checked Tue Oct 20 2026, 8:10–8:45 PM CDT. The branch was fetched read-only into the local branch `pr2` (no commits, pushes or PR comments). The host build is in `build-pr2/` and the screenshots are in `shots-pr2/`.

### 1. Host build and tests: PASS
* `ninja -j4` built 131 targets with no warnings. The `round15` warning is gone.
* `meson test`: **21 OK, 1 skipped (round11), 0 fail** out of 22 tests.
  * The new tests are `dialog_copy` and `ui_input`.
  * `argv_open` failed in the first run (exit 4, "No available video device"). My shell still had `DISPLAY` pointing at an Xvfb I had already stopped, so this was my environment, not the code. It passes with a live display.

### 2. Original issues: what's fixed

| Issue | Status | Evidence |
|---|---|---|
| A: menu drawn and hit-tested under the UI | **Fixed** | 02-menu-system-open, 03a-menu-windows-open, 08-1024x640-menu, 12-steamdeck-options-menu. Clicking "Evaluation" where it sits over the map opens the dialog (03-menu-click-evaluation-over-map). |
| B: Query Close button outside the panel | **Fixed** | 06-dialog-query |
| C: Graphs legend overlap | **Fixed** (two-column legend) | 04-dialog-graphs, 09-1024x640-graphs |
| D: dialog titles clipped | **Fixed** | all dialog shots |
| E: Evaluation and Budget content | **Fixed.** Every 0.9.5 caption and field is back. | 03-…evaluation, 05-dialog-budget |
| F1: held-A drag-build | **Fixed.** Holding A and moving the stick laid a road plus a bridge ($4,990 → $4,583). | 20-pad-held-A-drag-road |
| F2: gamepad slider focus jump | **Fixed.** Left/Right change the value and focus stays put (road 100→95, focus stays 8002). | 21-pad-budget-sliders |
| F3: keyboard sliders | **Fixed.** Right×3 takes tax 7→10% with focus on the tax slider. | 13-keyboard-budget-right-x3 |
| G: OSK | **Fixed.** QWERTY rows, shift, `-`, `'`, a wide space key, 44 px keys, visible focus, and typing works. | 26-pad-newcity-osk, 27-pad-osk-after-keypress |
| H: query em-dash split | **Fixed** | 06-dialog-query |
| Resize (1600x900, 1024x640) and `SteamDeck=1` fullscreen | OK | 10-1600x900, 11-steamdeck-fullscreen (`_NET_WM_STATE_FULLSCREEN`, 1280x800) |
| Palette tooltip popup (P3) | Not addressed. The hint still shows only in the bottom bar. | 07-palette-tooltip |

### 3. Sniper SDK with no extra packages: PASS (2 test failures caused by the environment)

* This used the stock `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest`. Nothing was installed.
* Configure succeeds with SDL3 3.4.16, cairo, and pangocairo 1.46.2. **cairomm and sigc++ are gone.**
* `ninja -j4` built all 131 targets, including `engine_fixes`, so the mallinfo2 guard works.
* `ldd` shows only SDL3, cairo, pangocairo, pango, and pangoft2.
* Tests: 19 OK, 1 skip, 2 fail:
  * `regressions` fails as root, because root ignores chmod. Run as uid 65534 from `/tmp`, it **passes**. Running it as uid 65534 from the root-owned build directory also fails ("could not block the save temporary file"), so it's an ownership artifact.
  * `argv_open` exits 127 because **the SDK has no `dbus-run-session`** (`tests/argv_open.cpp:82` execs it unconditionally). This one is still open; see the remaining issues.

**Bare binary (no bundle) in the platform runtime** (`SteamLinuxRuntime_sniper/run --`, pressure-vessel):
* `ldd` inside the runtime resolves everything from the runtime, with nothing "not found".
* Headless smoke with the dummy driver exits 0, with and without `LUNDUKE_CITY_ASSETS`.
* X11 Deck mode (`SteamDeck=1` on Xvfb :91):
  * The menu demo exits 0 (30-sniper-platform-bare-deck-menu).
  * The live run with Dullsville renders at 1280x800 (31-sniper-platform-bare-deck-live). `timeout` stopped it after 12 s, which is the expected exit 124.
* **The cairomm bundle is no longer needed.** I couldn't reproduce the cloud agent's failure to build inside the SDK.

### 4. Remaining issues and regressions (for the next cloud-agent pass)

1. **P1: hotspot/focus id collisions.** These are visible in gamepad dialogs.
   * In `src/shell_draw.cpp:24-60`:
     * `kCancel=1` equals `kMapId`
     * `kOk=2` equals `kPaletteId`
     * `kDiscard=3` equals `kMinimapId`
     * `kSave=4` equals `kWelcomeId`
     * `kClose=10` equals `kMenuTitle` (System)
   * `button()` (`src/shell_draw.cpp:143-147`) highlights when `hover_id_ == id || focus_id_ == id`, and pushes every button into the focus order, including the welcome "Dismiss" button drawn behind modals.
   * Symptoms:
     * When the gamepad cursor or pointer is over the map, the **Cancel** button in dialogs lights up (hover id 1 = map). See 22-pad-newcity-osk and 27-pad-osk-after-keypress.
     * In "Save changes?", the welcome-bar **Dismiss** lights up together with Save, and the D-pad focus cycle includes it (focus ids 4→1→3→4). See 22/24.
   * Fix: give dialog buttons their own id range (for example 100+). Clear `hover_id_` when a dialog opens. Skip `push_focus` for non-dialog widgets while `dialog_ != None`.
2. **P2: radio items in the Options menu draw stray diagonal lines.**
   * Each radio circle is joined to the previous row's text. `cairo_arc` at `src/shell_draw.cpp:362` / `:365` continues the current point Pango left behind.
   * Add `cairo_new_sub_path(cr)` (or `cairo_new_path`) before each `cairo_arc`.
   * This code was already in master but hidden by bug A. See 12-steamdeck-options-menu.
3. **P2: Budget slider layout is ambiguous.**
   * Each bar is drawn at the bottom of its slot (`bar_y = sy + slot - 24`, `src/shell_draw.cpp:688`). At 1280x800 there's about 60 px of white space under each caption, and the bar ends up about 6 px above the *next* caption, so it reads as belonging to the field below.
   * Put the bar right under the caption and detail (around `sy + 40`), or space the captions evenly above their bars. See 05-dialog-budget and 13-keyboard-budget-right-x3.
4. **P2: OSK D-pad navigation is linear.**
   * Down moves to the next key, not the key below (`move_focus`, `src/shell_actions.cpp:1243`, used for every direction). For example, focus went 6102 → 6105 after Down×2 + Right.
   * Add 2-D navigation for the key grid (and ideally for dialog button rows).
   * Also, the "auto" key extends about 10 px past the right edge of the keyboard and text field (27-pad-osk-after-keypress; `draw_osk`, `src/shell_draw.cpp:196`).
5. **P3: `tests/argv_open.cpp:82`** should exit 77 (skip) when `dbus-run-session` isn't on PATH, as in the sniper SDK, instead of failing with 127.
6. **P3:** palette tooltip popup is still missing. The scroll indicators are still not draggable.

### Housekeeping
* My first sniper pull (Oct 20 ~7:27 PM) landed in rootless podman storage under `/workspace/city2-verify/home/.local/share/containers` (3.7 GB). That happened because my shell had `HOME` set to the isolated game home. This pass pulled the image again into the normal `/home/box/.local/share/containers` (3.9 GB image). The copy under `city2-verify/home/` is redundant and can be deleted to free about 3.7 GB.
* All Xvfb (:47, :48, :91), xfwm4 and game processes I started were stopped by exact PID. No VMs were started.
* PR #2 artifacts:
  * `harness/padtest2/` is the PR #2 source plus the local pad-script patch (`harness/padharness.patch`).
  * The scripts are `harness/pad5.txt` and `harness/pad6.txt`.
  * `harness/pr2-src/` is the archive used for the sniper build (`build-sniper-pr2/`), with the script `harness/sniper-pr2.sh`.

# Lunduke City 2

Lunduke City 2 is a hard fork of [Lunduke City](https://github.com/BryanLunduke/LundukeCity) at tag `v0.9.5-2`. It is the same city builder, with the window system ported from gtkmm to SDL3 so it can run on LCOS, on the Steam Deck under the Steam Linux Runtime, and on Windows.

The simulation is the vendored Micropolis engine. Saved games stay `.cty` files and still load cities written by Lunduke City. The product name does not use the SimCity or Micropolis names. Licensing stays GPL-3.0-or-later together with the Electronic Arts additional terms in `NOTICE` (no use of the SimCity name, no implied EA affiliation).

Lunduke City 2 installs beside the original:

| | Lunduke City | Lunduke City 2 |
|---|---|---|
| Binary | `lunduke-city` | `lunduke-city-2` |
| Desktop id | `com.lunduke.LundukeCity` | `com.lunduke.LundukeCity2` |
| Config | `~/.config/lunduke-city` | `~/.config/lunduke-city-2` |
| Recovery | `~/.local/state/lunduke-city` | `~/.local/state/lunduke-city-2` |
| Version | 0.9.5-2 | 2.0.0 |

On Windows the config directory is `%APPDATA%\lunduke-city-2` and recovery is `%LOCALAPPDATA%\lunduke-city-2`.

The default window is **1280×800** (Steam Deck native). It is resizable. When `SteamDeck=1` is set, or the SDL hint `SteamDeck` is `1`, the window starts fullscreen.

## Build on LCOS / Debian 13

```sh
sudo apt install build-essential meson ninja-build pkg-config \
  libsdl3-dev libcairo2-dev libpango1.0-dev libpng-dev
meson setup build
meson compile -C build
meson test -C build
./build/src/lunduke-city-2
```

SDL 3.2 or newer is required (Debian 13 ships 3.2.10; the Steam Linux Runtime 3.0 "sniper" ships a newer 3.4). Ubuntu 24.04 has no `libsdl3-dev` package. On that release, build [SDL 3.2.10](https://github.com/libsdl-org/SDL/releases/tag/release-3.2.10) and install it where `pkg-config` can see `sdl3`.

Pango uses fontconfig. Install a UI font such as `fonts-dejavu-core` or Noto Sans so the menus are not missing glyphs.

## Steam Linux Runtime (sniper)

The sniper runtime has SDL3, cairo, and pango, and does not have gtkmm, which is why this port exists. It also does not ship cairomm or sigc++; the game draws with the cairo C API so a sniper SDK build needs no extra packages. Build inside the sniper SDK so the binary links a glibc the runtime can load:

```sh
# From the Steam Runtime SDK, after the sniper chroot is entered:
apt install build-essential meson ninja-build pkg-config \
  libsdl3-dev libcairo2-dev libpango1.0-dev libpng-dev
meson setup build --buildtype=release
meson compile -C build
```

Ship `lunduke-city-2` plus `share/lunduke-city-2/micropolis-assets` (or set `LUNDUKE_CITY_ASSETS` to that tree). Launch with `SteamDeck=1` for fullscreen. The system file portal is not available in the Deck session, so Load and Save use the in-window city list there.

## Windows

From Debian or Ubuntu, one script cross-builds the x86_64 exe, runs the test suite under Wine, and writes `build-windows/dist/lunduke-city-2-windows-x86_64.zip`:

```sh
sudo apt install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
  binutils-mingw-w64-x86-64 meson ninja-build pkg-config python3 \
  gnupg zstd zip wine wine64 xvfb
packaging/windows/build-windows.sh
```

The script builds a signature-checked MSYS2 sysroot (SDL3, Cairo, Pango, and DejaVu) and configures Meson with `cross/mingw-w64-x86_64.txt`. The zip is self-contained: unzip it and run `bin\lunduke-city-2.exe`. `JOBS` sets the compile parallelism (the default is 4). `LUNDUKE_WIN_TESTS=0` skips the Wine tests.

## Controls

Mouse and keyboard work as in Lunduke City.

| Input | Action |
|---|---|
| Left button / one-finger tap | Select a tool, or build on the map. A touch drag pans. Hold, then drag, to paint a line. |
| Two-finger drag | Pan the map |
| Pinch | Zoom |
| Wheel | Scroll the map. Ctrl+wheel zooms |
| Arrow keys | Pan |
| Space | Pause / resume |
| Ctrl+N / O / S / Shift+S / Q | New, Load, Save, Save As, Quit |
| Ctrl+P | Pause / resume the previous speed |
| Ctrl+1 / 2 / 3 | Slow, Medium, Fast |
| Ctrl+= / Ctrl++ / Ctrl+minus | Zoom. Keypad plus and minus too |
| Alt+letter, F10 | Open a menu. Arrows move. Enter activates. Esc closes |
| F11 or Alt+Enter | Fullscreen |

Gamepad (Xbox letters, or PlayStation symbols when the pad reports a PlayStation type or Cross button, including on a Deck). A Deck with an Xbox-style pad uses A/B/X/Y, L1/R1, and the menu glyph:

| Button | Action |
|---|---|
| Left stick | Move the cursor |
| Right stick | Pan |
| D-pad | One step, then it repeats: pan, move a highlight, or adjust a slider |
| A / Cross | Build, or confirm the highlighted control |
| B / Circle | Cancel, close a menu or dialog. In a text field, delete, then leave the field |
| X / Square | Query tool |
| Y / Triangle | Pause / resume. Hold to cycle Slow, Medium, and Fast |
| LB / RB | Previous / next tool. On a slider, a larger step |
| LT / RT | Zoom out / in |
| Start | Open or close the menu. In a dialog, confirm OK when the dialog has one |

The status line shows the glyphs for the connected pad. A focused slider draws a dark outer ring and a light or orange inner ring, so it stays visible on the dialog and on the slider fill.

City name and rename fields call `SDL_StartTextInput`. On the Steam Deck that raises the Steam keyboard. A gamepad, a Deck session, or a touchscreen also gets an on-screen key grid in those dialogs.

## Menus

The bar has the same rows as Lunduke City 0.9.5-2, plus the maps, messages, and looks added since, and a Milestones row. Dialogs (New City, Budget, Graphs, Evaluation, the map layers, scenarios, rename, query, About, confirmations) are drawn in the window. Classic keeps the Clearlooks-Phenix colours. SVGA and Spooky use flat VGA colours, a one-pixel edge, and a short dialog shadow. Tax-year budget, New City, scenarios, rename, About, and the confirmations pause the clock. Graphs, evaluation, map layers, query, milestones, and a budget opened from the menu do not.

The tool footprint shows whether the tile can be built, what it will cost, and why it cannot. A short note at the tile confirms the money spent. Query keeps a small card on the map; the full report still opens when you click. Evaluation's complaints can explain themselves and open the matching map without moving the view. A tax year opens with a short civic report before the budget sliders. Milestones record the first powered neighborhood, a balanced year, easier traffic, and the census thresholds. They do not lock any building. Auto-goto stays the preference it already was.

Recent cities are stored in `~/.config/lunduke-city-2/recent.txt` (ten paths). That list is not the GTK recent database, so it is not shared with Lunduke City. Load and Save try the SDL file dialog (the desktop portal on Linux, the Win32 dialog on Windows) and fall back to the in-window list, which is also what the Deck and a headless run use.

There is no D-Bus single instance. A second launch is a second process. Outside opens that arrive while a dialog is up wait until it closes.

## Headless checks

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LUNDUKE_CITY2_SMOKE=1 ./build/src/lunduke-city-2
```

That loads Dullsville, ticks, draws a frame, checks Ctrl+1, cycles a tool from a virtual gamepad, and accepts a synthetic tap. `LUNDUKE_CITY2_DEMO=window|menu|budget|gamepad` and `LUNDUKE_CITY2_SHOT=frame.png` write a PNG and exit. `LUNDUKE_CITY2_SAVE=city.cty` writes a save during the smoke run.

The dummy SDL audio driver is not used for playback. It corrupts its simplified stream when a mixer thread is attached, so sound is skipped for that driver only.

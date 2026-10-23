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
  libsdl3-dev libcairo2-dev libcairomm-1.0-dev libpango1.0-dev libpng-dev
meson setup build
meson compile -C build
meson test -C build
./build/src/lunduke-city-2
```

SDL 3.2 or newer is required (Debian 13 ships 3.2.10; the Steam Linux Runtime 3.0 "sniper" ships a newer 3.4). Ubuntu 24.04 has no `libsdl3-dev` package. On that release, build [SDL 3.2.10](https://github.com/libsdl-org/SDL/releases/tag/release-3.2.10) and install it where `pkg-config` can see `sdl3`.

Pango uses fontconfig. Install a UI font such as `fonts-dejavu-core` or Noto Sans so the menus are not missing glyphs.

## Steam Linux Runtime (sniper)

The sniper runtime has SDL3 and does not have gtkmm, which is why this port exists. Build inside the sniper SDK so the binary links a glibc the runtime can load:

```sh
# From the Steam Runtime SDK, after the sniper chroot is entered:
apt install build-essential meson ninja-build pkg-config \
  libsdl3-dev libcairo2-dev libcairomm-1.0-dev libpango1.0-dev libpng-dev
meson setup build --buildtype=release
meson compile -C build
```

Ship `lunduke-city-2` plus `share/lunduke-city-2/micropolis-assets` (or set `LUNDUKE_CITY_ASSETS` to that tree). Launch with `SteamDeck=1` for fullscreen. The system file portal is not available in the Deck session, so Load and Save use the in-window city list there.

## Windows cross-compile

`cross/mingw-w64-x86_64.txt` is a Meson cross file for MinGW-w64:

```sh
sudo apt install mingw-w64
meson setup build-win --cross-file cross/mingw-w64-x86_64.txt
meson compile -C build-win
```

The new code paths (config, recovery, asset lookup, no POSIX-only calls without a Windows branch) are in place. A finished Windows link also needs Cairo, Pango, and SDL3 built for MinGW. Those are not in the Debian/Ubuntu MinGW packages, so a Windows link was not produced here. Point `x86_64-w64-mingw32-pkg-config` at a sysroot that has them before expecting `meson compile` to finish.

## Controls

Mouse and keyboard work as in Lunduke City.

| Input | Action |
|---|---|
| Left button / one-finger tap | Select a tool, or build on the map. Drag paints a line. |
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

Gamepad (Xbox letters, PlayStation symbols when the pad reports them, Steam Deck uses the Xbox set):

| Button | Action |
|---|---|
| Left stick | Move the cursor |
| Right stick | Pan |
| D-pad | Pan the map, or move the highlight in a menu or dialog |
| A / Cross | Build, or confirm the highlighted control |
| B / Circle | Cancel, close a menu or dialog |
| X / Square | Query tool |
| Y / Triangle | Pause / resume |
| LB / RB | Previous / next tool |
| LT / RT | Zoom out / in |
| Start | Open or close the menu |

The status line shows the glyphs for the connected pad. A focused control draws an orange ring.

City name and rename fields call `SDL_StartTextInput`. On the Steam Deck that raises the Steam keyboard. A gamepad, a Deck session, or a touchscreen also gets an on-screen key grid in those dialogs.

## Menus

The bar has the same rows as Lunduke City 0.9.5-2: 35 actionable entries (System, Options, Disasters, Windows, Help), plus the hidden zoom accelerators. Dialogs (New City, Budget, Graphs, Evaluation, the six overlays, scenarios, rename, query, About, confirmations) are drawn in the window in Clearlooks-Phenix colours. Tax-year budget, New City, scenarios, rename, About, and the confirmations pause the clock. Graphs, evaluation, overlays, query, and a budget opened from the menu do not.

Recent cities are stored in `~/.config/lunduke-city-2/recent.txt` (ten paths). That list is not the GTK recent database, so it is not shared with Lunduke City. Load and Save try the SDL file dialog (the desktop portal on Linux, the Win32 dialog on Windows) and fall back to the in-window list, which is also what the Deck and a headless run use.

There is no D-Bus single instance. A second launch is a second process. Outside opens that arrive while a dialog is up wait until it closes.

## Headless checks

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LUNDUKE_CITY2_SMOKE=1 ./build/src/lunduke-city-2
```

That loads Dullsville, ticks, draws a frame, checks Ctrl+1, cycles a tool from a virtual gamepad, and accepts a synthetic tap. `LUNDUKE_CITY2_DEMO=window|menu|budget|gamepad` and `LUNDUKE_CITY2_SHOT=frame.png` write a PNG and exit. `LUNDUKE_CITY2_SAVE=city.cty` writes a save during the smoke run.

The dummy SDL audio driver is not used for playback. It corrupts its simplified stream when a mixer thread is attached, so sound is skipped for that driver only.

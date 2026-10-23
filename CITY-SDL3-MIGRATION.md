# Lunduke City: moving from gtkmm-3 to SDL3 (LCOS, SteamOS, Windows via Steam)

Date: 2026-10-20 (CDT). Source: https://github.com/BryanLunduke/LundukeCity master, tag `v0.9.5-2`, cloned read-only into
`/workspace/xplat-eval/city-sdl-eval` (nothing pushed or committed). Prototype: `/workspace/xplat-eval/city-sdl-proto/proto.cpp`.
Screenshots: `/workspace/xplat-eval/city-shots/`.

Legend: **[T]** tested or measured on the box today. **[C]** read from the City source. **[D]** from docs or web, not tested. **[E]** my estimate.

## 1. Verdict

**Yes. The move is feasible, and the look can stay very close to what LCOS users see now.** [T][C]

- About 2,900 of City's own ~10,900 lines are already toolkit-free (checked: no GTK/Glib includes): `city_session.*`, `tools.*`,
  `save_path.hpp`, `city_recovery.hpp`, `city_seed.hpp` and `messages.cpp`, plus the whole Micropolis engine. They move over as-is.
- All of the game's pictures are drawn by Cairo into `Gtk::DrawingArea`s. That covers the map, sprites, tool palette, minimap,
  R/C/I meter, graphs and overlay maps. The drawing code runs unchanged into a Cairo image surface that gets uploaded to an SDL3
  texture. **I proved this in the prototype**: the real `MapView::on_draw`, `ToolPalette::on_draw`, `MinimapView::on_draw` and
  `DemandView::on_draw` code renders inside an SDL3 window. [T]
- What gets rewritten is the GTK "chrome": the menu bar, status bar, message bar, 7 dialog-style windows, 11 message dialogs,
  the file chooser and Open Recent. SDL3 has **no in-window menu bar or widget API on Linux** (checked in the SDL 3.2.10 headers;
  see §3.4). So these become custom Cairo + Pango widgets. The prototype's menu bar is close to a pixel match for the
  Clearlooks-Phenix theme LCOS ships (§5).
- Effort: about **50–70 focused days (10–14 weeks)** [E] for parity on LCOS, Windows builds and basic Deck input. The biggest
  items are the small widget kit plus dialogs (~40%) and replacing the GTK-based test harness (§6).

## 2. What's there now: the GTK/gtkmm/Cairo inventory [C]

GTK class use, counted by grep over `src/`: Label 63, Box 26, RadioButton 17, MenuItem 17, Window 15, DrawingArea 14,
MessageDialog 11, Menu 11, CheckMenuItem 10, Scale 9, Button 9, ScrolledWindow 7, Dialog 7, RadioButtonGroup 6,
SeparatorMenuItem 4, ApplicationWindow 4, RadioMenuItem 3, Frame 3, EventBox 3, Entry 3, FileFilter 3, TreeView 1 (with
TreeModel/TreeViewColumn), Tooltip 2, SizeGroup 2, RecentManager 2, RecentChooserMenu 2, MenuBar 2, Grid 2,
FileChooserDialog 2, Stack 1. **No `Gtk::Toolbar`, no `Gio::Menu`, no GtkBuilder/UI XML, no right-click menus, no CSS provider.**

| Area | Now (file) | SDL3 replacement | Difficulty |
|---|---|---|---|
| Main window layout | `ApplicationWindow` + Box/EventBox/ScrolledWindow (`app_window.cpp` ~L515–598) | one SDL window; fixed layout code (menu 28 px, status row, side column, map, message bar, status line) | low |
| Menu bar + 5 menus | `Gtk::MenuBar`, built by hand in `build_menus()` (L599–760) | custom Cairo/Pango menu bar (prototyped) | medium (§3) |
| Tool palette ("toolbar") | custom `Gtk::DrawingArea`, all Cairo (`tool_palette.cpp`) | same Cairo code; GTK tooltip and cursor replaced | low (§4) |
| Map view | DrawingArea + Cairo, nearest-filter scaled tile surface, sprites, footprint preview, blink timer, edge scroll, `gdk_seat_grab` pointer grab (`map_view.cpp`) | same draw code. Events become SDL mouse/keys; the grab becomes `SDL_CaptureMouse`; timers move into the frame loop | low–medium |
| Minimap, R/C/I meter | DrawingArea + Cairo (`side_widgets.cpp`) | same code [T] | low |
| Graphs | Window + Cairo chart + legend + radio buttons (`graphs_window.cpp`) | chart code kept; legend and radios redone as custom widgets | medium |
| Overlay maps (6) | separate `Gtk::Window` each, with Cairo (`overlay_window.cpp`) | SDL3 multi-window (supported) or in-window panels | medium |
| Budget | Window + 9 `Gtk::Scale` + labels (`budget_window.cpp`) | custom slider widget | medium |
| Evaluation | Window + labels | custom panel | low |
| New City wizard | Dialog + Stack (3 pages), Entry, RadioButtons, seed Entry with a warning icon, map preview (`new_city_dialog.cpp`) | custom modal; needs a text field (SDL_StartTextInput) | medium–high |
| Scenario picker | Dialog + TreeView (`app_window.cpp` ~L1734) | custom list widget | medium |
| Message dialogs (11) | `Gtk::MessageDialog`: disaster confirms, save/discard, recovery, delete, load error, etc. | in-window Cairo modal (matches the theme) or `SDL_ShowMessageBox` (X11 fallback looks bad) | low each |
| Rename, Query | Dialog + Entry / labels | custom modal | low–medium |
| About | `about_dialog.cpp` | custom modal; links via `SDL_OpenURL` | low |
| Load/Save file chooser | `Gtk::FileChooserDialog` + FileFilter | `SDL_ShowOpenFileDialog` / `SDL_ShowSaveFileDialog`. LCOS ships `xdg-desktop-portal` + `xdg-desktop-portal-gtk` (`r095-09/.../desktop.list.chroot`), so on LCOS **the same GTK file chooser appears through the portal**. The SDL "zenity" backend is a fallback [C][D] | low on LCOS; Deck game mode needs an in-game list [E] |
| Open Recent | `RecentChooserMenu` from the desktop's shared recent list | own MRU list in config, or read/write `~/.local/share/recently-used.xbel` | low–medium |
| Single instance / open file | `Gtk::Application` with `HANDLES_OPEN` (D-Bus) | argv only, or own D-Bus/socket | low (or drop it) |
| Tooltips | only the palette (custom query-tooltip); Open Recent shows path tips | custom tooltip (prototyped) | low |
| Keyboard | accelerators (`Gtk::AccelGroup`), plus key handlers for Ctrl+P, zoom keys, Space, arrows on the map | own accelerator table (prototyped) | low |
| Theming | **no custom CSS.** Widgets follow the desktop GTK theme (Clearlooks-Phenix on LCOS, `xsettings.xml`); `render_background` in the palette and side widgets | fixed palette matched to Clearlooks-Phenix; can't follow a theme the user switches to (§5) | n/a |
| Fonts/text | GTK labels (Pango), "Sans 10" from xsettings | Pango-Cairo directly (prototype uses it) or FreeType+HarfBuzz | low |
| HiDPI | layout notes GDK_SCALE=2 support (`app_window.cpp` L203) | `SDL_GetWindowDisplayScale` + Cairo device scale | low–medium |
| Sound | already GTK-free: `libpulse-simple` via `dlopen` (`sound_player.cpp`) | `SDL_AudioStream` (also gives Windows) | low |
| Image loading | `Gdk::Pixbuf` for `tiles.png` and `obj*.png` | `cairo_image_surface_create_from_png` or SDL_image/stb_image | low |
| Timers | `Glib::signal_timeout` (17 in `app_window`, 2 in `map_view`, 1 in the wizard) | frame-loop scheduler / `SDL_AddTimer` | low |
| Clipboard, DnD, IME | none used (only plain Entry typing) | `SDL_StartTextInput` for the 3 text fields | low |
| Accessibility | GTK gives ATK for free | **lost.** SDL has no a11y tree | can't match without big work |
| Tests | `app_window.cpp` self-tests walk the GTK widget tree (`round8_find_item`, `r12_submenu`, accelerator checks) | rewrite against the new menu model and widgets | medium |

## 3. Menu bar in detail

### 3.1 What's there now [C]
Built by hand in C++ (`AppWindow::build_menus`, `app_window.cpp` L599–760): `Gtk::MenuBar` → `Gtk::MenuItem(label, mnemonic=true)` →
`set_submenu(Gtk::Menu)`. Accelerators are attached with `add_accelerator(..., accel_, key, mods, ACCEL_VISIBLE)` on one
`Gtk::AccelGroup`. **Not** Gio::Menu/GMenuModel and **not** UI XML. Every item has a mnemonic (the `_` letter).

| Menu | Items (mnemonic in _; accelerator) | Kind |
|---|---|---|
| **_System** | _New City (Ctrl+N) · _Load City... (Ctrl+O) · Open _Recent ▸ (submenu: `Gtk::RecentChooserMenu`, filter app "Lunduke City", local only, MRU, limit 10, tips on, missing files hidden) · Save _City (Ctrl+S) · Save City _As... (Shift+Ctrl+S) · Play _Scenario… · _Rename City… · ── · _Quit (Ctrl+Q) | normal + 1 submenu |
| **_Options** | Auto _budget · Auto b_ulldoze · Enable _disasters · Auto-_goto · _Mute sound · ── · _Pause (Ctrl+P, a toggle handled in `on_key_press_event` so a second press resumes) · Slo_w (Ctrl+1) · M_edium (Ctrl+2) · _Fast (Ctrl+3) · ── · Zoom _in (Ctrl+= shown; Ctrl+plus and Ctrl+KP_Add hidden) · Zoom _out (Ctrl+- shown; Ctrl+KP_Subtract hidden) | 5 check items, 4-item radio group, 2 normal |
| **_Disasters** | _Fire… · F_lood… · _Tornado… · _Earthquake… · M_onster… · _Meltdown… (each opens a confirm `MessageDialog`) | normal |
| **_Windows** | _Budget · _Graphs · _Evaluation · ── · _Power · _Water · P_ollution · _Crime · _Land value · _Traffic (6 overlay windows) | normal |
| **_Help** | _About Lunduke City | normal |

That's 5 top-level menus, 37 items, 1 dynamic submenu, 5 check items, 1 radio group of 4, 4 separators and 12 visible accelerators.

### 3.2 Under SDL3: what I built and how it behaves [T]
The prototype's `MenuBar` struct (~120 lines in `proto.cpp`) is a plain data model (`Menu{title, items}`, `Item{label, accel, kind
Normal/Check/Radio/Sep, on, key, mod, act}`) plus Cairo/Pango drawing and SDL event handling:
- **Look:** menu bar gradient, the blue selected title with its dark border, white dropdown with a grey border, check boxes,
  radio dots, right-aligned grey accelerator text and separators. All colours were sampled from the real GTK app running
  Clearlooks-Phenix (the theme deb from `r095-09/lcos-live/config/packages.chroot`, unpacked into `/workspace/xplat-eval/theme`
  only, not installed). Font is "Sans 10" via Pango, which gives Noto Sans, the same as GTK. See `compare-menu-zoom.png`.
- **Mouse:** click a title to open it. While open, moving across titles switches menus (GTK behaviour). Hover highlights items,
  clicking activates, and clicking outside closes.
- **Keyboard:** holding Alt shows mnemonic underlines. Alt+letter opens a menu. Up/Down move (skipping separators), Left/Right
  switch menus, Enter or the mnemonic letter activates, Esc closes, F10 opens the first menu. Check items toggle, and radio
  items clear their group.
- **Accelerators:** one table built from the same items replaces `Gtk::AccelGroup`. Tested headless: an injected Ctrl+1 changed
  speed from Paused to Slow (`paused=1 → 0`) [T]. Hidden alternates (KP_Add, shifted plus) are just extra rows.
- **Scripted check:** `PROTO_SCRIPT=menu` injects Alt, O, then Down×6. The result (`sdl-menu.png`) lands on "Slow" with
  underlines shown, matching what GTK draws after the same keyboard navigation (`gtk-scen-menu.png`).

### 3.3 What would look or behave differently
- **Dropdowns are clipped to the window.** GTK menus are separate X11 popup windows that can extend past the window edge. With
  City's 1100×740 window and menus under 400 px tall this never happens today. If needed, `SDL_CreatePopupWindow(...,
  SDL_WINDOW_POPUP_MENU)` (in SDL 3.2 [T: header]) can host dropdowns as real popups on X11/Wayland [D].
- No drop shadow or compositor fade (Clearlooks under XFCE barely shows one). Easy to add a 1–2 px shadow.
- No ATK/accessibility, and no global-menu export (XFCE's appmenu plugin isn't used on LCOS anyway [E]).
- Open Recent loses the shared desktop recent list unless we write the `.xbel` ourselves. The tooltip with the full path is a small addition.
- If the user changes the GTK theme, the SDL version doesn't follow it (§5).

### 3.4 Is there native SDL3 menu support? Checked
- SDL 3.2.10 headers on the box: no menu-bar API. `SDL_tray.h` gives **system-tray** icon menus (with checkbox and submenu
  items), which don't belong in a game's window. `SDL_WINDOW_POPUP_MENU` / `SDL_CreatePopupWindow` give you an empty popup
  surface that you draw yourself [T: headers].
- Upstream: "Native Menu Bars for Windows and MacOS" (https://github.com/libsdl-org/SDL/pull/13752) is **open, unreleased**,
  milestone 3.6.0, last updated 2026-03-22, and covers **Win32 and Cocoa only**. Related request: https://github.com/libsdl-org/SDL/issues/9590 [D].
  Even if it ships, it gives nothing on Linux/SteamOS, and a native Win32 menu bar would look different from LCOS. **Use the
  custom-drawn bar on all platforms.**
- Dear ImGui `BeginMainMenuBar` is a quick alternative (keyboard nav, shortcut labels), but it looks flat or ImGui-styled. You
  can theme the colours but not Clearlooks' gradients or Pango text without a lot of work. Fine for a debug menu, not for the
  "same look" goal.

### 3.5 Effort for the menu system [E]
Prototype: about 1 hour for drawing, model, mouse, keyboard and accelerators. To production: about **3–4 days**. That covers the
Open Recent submenu with hover-to-open and the submenu arrow, disabled items, keeping check/radio state in sync with session
state (`auto_budget_item_` etc.), HiDPI scale, gamepad/Deck navigation, popup windows (optional), and ports of the existing
menu self-tests (`round8_*`, `r12_*`).

## 4. Tool palette ("toolbar") in detail

### 4.1 What's there now [C]
- City has **no GtkToolbar**. The toolbar is `ToolPalette`, one custom `Gtk::DrawingArea` (`tool_palette.cpp`, 321 lines):
  a **2 × 8 grid of 16 tools**, 36 px cells, 4 px padding (80 × 296 px).
- **Icons:** none from the icon theme and no bundled PNGs. All 16 are **procedural Cairo rectangles** (`icon_bulldozer` …
  `icon_query`), described as "original drawings, not imported artwork". The engine's tool art isn't used.
- **State:** radio-like single selection (`selected_`, default `kDefaultToolIndex`). Pressed cells are drawn sunken with a
  yellow (1.0, .86, .15) face and a dark inner outline, with the icon shifted 1 px. Hover is a lighter face. The cursor is
  `Gdk::HAND2`.
- **Tooltips:** `signal_query_tooltip` shows `ToolDef::hint`, which includes the cost, e.g. "Roads: $10. Bridge: $50".
- **Cost display:** in the tooltip, plus the bottom status line repeats the selected or hovered tool's hint ("Power lines:
  $5. Underwater wire: $25"). Costs live in `tools.cpp` (`kTools[]`: Bulldozer $1, Roads $10, Rail $20, Power lines $5,
  Park $10, R/C/I $100, Police/Fire $500, Stadium $5,000, Seaport $3,000, Coal $3,000, Nuclear $5,000, Airport $10,000,
  Query $0).
- **Only GTK dependencies:** the `DrawingArea` base, `render_background` from the theme style context, the tooltip and the cursor.
- Below it, the minimap and the R/C/I `DemandView` are also custom Cairo DrawingAreas.

### 4.2 Under SDL3 [T]
- `on_draw` reused **unchanged**. The prototype renders the real `ToolPalette::on_draw` into the SDL window, and it matches GTK
  pixel for pixel apart from the background fill (`compare-palette-tooltip.png`).
- Changes: the background becomes a fixed `#EDECEB` (the Clearlooks window colour) instead of `render_background`. Button press
  and motion come from SDL events (same `index_at()` maths). The tooltip is custom-drawn (prototyped with the GTK tooltip
  colour `#F5F5B5`, add a ~500 ms delay). Cursor: `SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER)`.
- New for Deck/gamepad: a focus ring plus D-pad/shoulder-button tool cycling, and maybe larger cells at 1280×800 (`kCell`
  becomes a scale factor). [E]
- Effort: **about 0.5 day** for parity, plus about 1 day for gamepad focus and scaling. [E]

## 5. The "mostly the same look" question
- The prototype shows the main window can look almost the same on LCOS: `compare-full.png` (left is the real GTK 0.9.5-2,
  right is SDL3 on the same saved city) and `compare-menu-zoom.png`. **What I did not draw in the prototype:** the scrollbars
  around the map, the "Dismiss" button and the GTK frame bevels. These are easy custom widgets, but they're not shown yet.
- **Trade-off:** we would freeze a copy of Clearlooks-Phenix in our own constants. Today City follows whatever GTK theme the
  user picks (dark themes, larger fonts). Options: (a) a fixed LCOS look everywhere, the same on Deck and Windows (recommended);
  (b) read `gtk-theme-name` and font from xsettings and ship 2–3 palettes (light, dark) [E]. Matching an arbitrary GTK theme is
  not realistic.
- **File dialogs:** with SDL's portal backend, LCOS users still get the GTK file chooser, because LCOS ships
  `xdg-desktop-portal-gtk` [C]. On Windows they get the native Win32 dialog [D]. In Deck game mode there may be no portal UI,
  so add an in-game "My Cities" list [E].
- **Message boxes:** don't use `SDL_ShowMessageBox` on Linux; its X11 fallback is a plain Xlib box. Draw modal dialogs in-window
  in the same style.
- **Fonts:** Pango + fontconfig gives the same Noto Sans as GTK. On Windows and in the Steam Runtime, bundle the font and keep
  Pango (MSYS2 has pango/cairo [D]) or switch to FreeType+HarfBuzz.

## 6. Effort estimate (one developer, focused days) [E]

| Component | Days |
|---|---|
| SDL3 shell: window, event loop, Cairo→texture, HiDPI, timer scheduler, `SDL_CaptureMouse` for the map drag grab, edge scroll | 3–4 |
| Re-base 6 DrawingArea classes on a small `CairoWidget` interface (draw/press/motion/key); on_draw bodies unchanged | 3–4 |
| Mini widget kit in Cairo+Pango (label, button, check, radio, slider, text entry + IME basics, scrollbar, list, frame, layout boxes, tooltip, modal) | 8–12 |
| Menu bar + Open Recent + accelerators | 3–4 |
| Tool palette, status bar, message/welcome bar | 1.5 |
| Dialogs: 11 message boxes, New City wizard, Budget, Graphs, Evaluation, 6 overlays, Scenario list, Rename/Query/Recovery, About | 10–12 |
| File dialogs (portal) + in-game city list for Deck | 2–3 |
| Sound to SDL audio; Pixbuf to PNG loader; drop Gtk::Application | 2 |
| Replace the GTK-tree self-tests and screenshot hooks (`LUNDUKE_CITY_SHOT_*`) | 3–5 |
| Gamepad / Steam Deck navigation and 1280×800 legibility | 3–5 |
| Windows build (MinGW cross from this box or MSVC) + bundling cairo/pango/glib/freetype | 2–4 |
| Look matching, QA, regressions | 4–6 |
| **Total** | **≈ 45–65 days → about 10–14 weeks with slack** |

Using Dear ImGui for the dialogs instead (keeping custom Cairo for the menu bar, palette and status bar) would save about 8–10 days,
but the dialogs would clearly look like ImGui rather than Clearlooks.

## 7. Risks
1. **Dependency weight off LCOS.** Cairo + Pango + GLib + HarfBuzz + fontconfig must be bundled for Windows and checked inside the
   Steam Runtime sniper. Earlier I showed that gtkmm isn't in sniper; whether cairo/pango are is **unverified** (no big image pulls
   while QA runs). Fallback: Cairo + FreeType + HarfBuzz without Pango.
2. **Lost free behaviour:** accessibility (ATK), following the user's GTK theme, the shared recent list, single-instance file open, and IME.
3. **Regression surface:** many round-numbered fixes (C-4, C-6, P-4…) live in GTK glue, including window fitting (`window_fit.hpp`),
   ellipsizing status lines and zoom-key quirks. Each has to be carried over by hand, and the tests that guard them must be rewritten first.
4. **Ownership:** City is Phil's repo on the 0.9.x track. A toolkit swap should go on a long-lived branch or a new `shell-sdl/`
   frontend next to the GTK one, not into master during the release.
5. **Performance:** fine. Full-frame Cairo plus one texture upload is cheap at 1100×740 [T]. Use dirty-region redraw on the Deck to save power.

## 8. Recommended approach
1. Pull the six Cairo widgets off `Gtk::DrawingArea` onto a toolkit-neutral `CairoWidget` interface, keeping thin GTK adapters,
   so the **GTK build keeps working** during the move. This is low-risk and can land first.
2. Add an SDL3 frontend (`src/sdl/`) as a second Meson executable. Start with the main window, menu bar, palette and status
   bar (the prototype is the starting point), then the dialogs in order of use: message boxes, file dialogs, Budget, New City,
   Graphs/Evaluation/Overlays, Scenario list, About.
3. Draw everything in a fixed Clearlooks-Phenix-matched style (one `theme.hpp`), use Pango for text, use the portal for files
   on LCOS, and draw modals in-window.
4. Once the SDL frontend reaches parity and its tests pass, switch the LCOS package to it and retire the GTK frontend.

## 9. Licensing
City is GPL-3.0-or-later with Micropolis (GPLv3 plus EA's extra terms: no "SimCity" name or trademarks). SDL3 is zlib, Cairo is
LGPL-2.1/MPL-1.1 and Pango is LGPL. All are compatible. GPL is fine on Steam (Valve's terms don't conflict with shipping source),
and the EA terms keep applying on every platform [D, from the earlier reports]. The prototype has no new third-party code.

## 10. Prototype details and box hygiene [T]
- `proto.cpp` (~230 lines) links City's **existing build objects** (`city_session`, `map_view`, `side_widgets`, `tool_palette`,
  `tools`, `tile_atlas`, `sprite_art`, `assets`, `messages`, `sound_player`, `libmicropolis.a`) against SDL 3.2.10.
  **GTK is still linked**, but only because those classes still inherit `Gtk::DrawingArea`. They sit in an offscreen
  `Gtk::OffscreenWindow` to get a size, and are drawn by calling their protected `on_draw` through `using` subclasses. Step 1
  of §8 removes this.
- Build: a single `nice g++` (about 2 s), no meson rebuild. Ran under `xvfb-run` and a private `Xvfb :77` (1280×800). Peak use
  was tens of MB. No VMs, emulators or containers. No pkill/killall: my own stray `Xvfb :77` (PID 100670, started 12:21:32 CDT)
  was killed by exact PID. Other agents' Xvfb displays (:1, :4, :5, :12) were not touched. `/dev/kvm` and the
  lcos-095-09/lcos-iso/lcos-iso-qa directories were not touched. The LCOS theme .deb was only unpacked into `/workspace/xplat-eval/theme`.
- Files in `/workspace/xplat-eval/city-shots/`: `gtk-scen.png`, `gtk-scen-menu.png`, `gtk-tooltip.png`, `sdl-none.png`,
  `sdl-menu.png`, `sdl-tooltip.png`, `sdl-accel.png`, `compare-full.png`, `compare-menu.png`, `compare-menu-zoom.png`,
  `compare-palette-tooltip.png`, `proto.cty` (the saved Dullsville used by both). Note the GTK run shows "Medium Feb 1900": GTK resumed the loaded city at Medium speed
  (probably the speed stored in the save; I didn't look into it). The prototype forces Paused. The map is the same city.

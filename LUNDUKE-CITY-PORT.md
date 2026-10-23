# Lunduke City → iPhone/iPad: port viability (xtool-on-Linux + occasional Mac)

*Prepared Tue Oct 20, 2026, 11:30 AM CDT. Read-only review. I made no changes to the Lunduke City repo and sent no messages to other bots.*
Tags used below:
- **[FOUND]** means I read it in the source or the repo.
- **[INFERRED]** means it's my judgment.
- **[DOCS]** means an external source, linked.

## 0. Where the source is
- **Repo:** https://github.com/BryanLunduke/LundukeCity (public, GPL-3.0, C++, default branch `master`). The latest tag is **v0.9.5-2** (`59fc175c`), and the last push was Oct 18, 2026 10:30 PM CDT. [FOUND]
- **Box checkouts:**
  - `/workspace/review-1.0c/LundukeCity` is at v0.9.5-1 (`c39af18`). I analyzed this one. It is one PR behind master, and per the seed note PR #36 is GTK dialog/instance fixes only.
  - `/workspace/review-1.0b/…` is also at v0.9.5-1.
  - `/workspace/review-r15/…` is at v0.9.4-8.
  - `/workspace/LundukeCity` is old (v0.7.3).
  - The skill `/home/box/sand-data/workflows/lunduke-city-deb-seed/SKILL.md` refers to `/workspace/lunduke-city`. That path doesn't exist now. [FOUND]

## 1. What the game is
| Aspect | Finding |
|---|---|
| What it is | A SimCity-Classic-style city builder for LCOS. It wraps the **Micropolis** engine (EA's 1989 SimCity source, GPL-released for OLPC in 2008) in an original GTK UI. [FOUND: README, NOTICE] |
| Languages | **C++17** throughout. Build is Meson + Ninja. [FOUND] |
| Size (LOC) | UI and app code `src/` is **10,743** lines. Vendored engine `third_party/micropolis/MicropolisEngine` is **22,731** lines. Tests are **6,999** lines. [FOUND: wc] |
| UI toolkit | **gtkmm-3.0** (GTK 3). `app_window.cpp` (2,593 lines) is the main window, with menus, dialogs, and timers. The heaviest GTK users are `app_window.cpp` (247 GTK refs) and `new_city_dialog.cpp` (99). [FOUND] |
| Rendering | Cairo on a `Gtk::DrawingArea` (`map_view.cpp`). Tiles are blitted from a 16‑px atlas, `tiles.png` (256×960 = 960 tiles), and sprites from 32×32 `obj*.png` frames. Zoom changes the tile size. The minimap and R/C/I meters are custom-drawn. [FOUND] |
| Sim loop | A GTK timer every 100 ms (`Glib::signal_timeout` → `on_tick`) calls the engine step and then the engine's tile animation. Speed settings are Pause, Slow, Medium, and Fast. The engine is single-threaded, and its C-style callback hook (`callbackHook`) drives messages and sounds. [FOUND] |
| Input model | Mouse-first: left-click to place, click-drag for roads, rails, and wire (`drag_tool`), and a **hover footprint preview** of the selected tool under the pointer. There are keyboard shortcuts: Ctrl +/−/= zoom, arrow keys scroll, Space, and Ctrl-N/O/S/Q/P. The tool palette is 2×8 and click/drag driven. Hover has no touch equivalent. [FOUND] |
| Layout and resolution | Desktop-window layout: a menu bar (System, Options, Disasters, Windows), a left tool palette with minimap and demand meters, a scrolling map, a message line, and a status line. The default window is 1100×740, sized to fit 1280×720 and HiDPI. Budget, Graphs, Evaluation, overlay maps, the New City wizard, Query, and About are all separate windows or dialogs. The world is fixed at **120×100 tiles** (1920×1600 px at 16 px). [FOUND] |
| Assets | `third_party/micropolis-assets` is **2.6 MB**: 62 PNG files (tiles plus sprite frames, 336 KB), 49 WAV files (1.8 MB), the `hexa.*` tile resources, and 8 `snro.*` scenario files. All are **EA / Don Hopkins, GPL-3+**, vendored unmodified. The app icon (`data/lunduke-city.svg` plus PNGs) is original. [FOUND] |
| Save files | `.cty`, the native Micropolis city format (`fileio.cpp`). Byte swapping is driven by an unconditional `IS_INTEL` define, so the format is little-endian and **arm64 iOS will read and write the same files as amd64 Linux** [INFERRED from code]. Autosave and crash recovery live in `city_recovery.hpp` (POSIX dirent/fcntl), and the default save location is XDG-style `~/.config/lunduke-city`. [FOUND] |
| Sound | `sound_player.cpp` loads **PulseAudio** at runtime (`PulseApi`) and mixes WAVs on a worker thread. Without audio it falls back to silence. [FOUND] |
| Dependencies | gtkmm-3.0 (LGPL), cairomm (LGPL), threads, PulseAudio (LGPL, loaded at runtime). The engine itself needs **only libc/POSIX and the C++ STL**: no Tcl/Tk, Python, or SWIG. It has a Windows `#ifdef` in `stdafx.h` and otherwise plain POSIX headers. [FOUND] |
| License | Everything is **GPL-3.0-or-later**. The engine and assets carry **EA's GPL §7 additional terms**: no SimCity trademark, no claimed EA affiliation, modified versions must be marked, and an indemnity clause. Lunduke-original code is "© 2026 The Lunduke City authors, GPL-3+". [FOUND: NOTICE, COPYING] |

### What separates cleanly from GTK (reuse candidates)
GTK-free code, per include and grep checks [FOUND]:
- `city_session.cpp/.hpp` (~2,240 lines): the whole game façade over the engine. It covers new city (seed, difficulty, terrain), load and save, recovery, scenarios (all 8), tools (place and drag), budget book, sprites list, evaluation, and history. Its listener is a `std::function`.
- `messages.*`, `tools.*`, `assets.*`, `save_path.hpp`, `city_recovery.hpp`, `view_math.hpp`.
- The whole engine.

GTK or Cairo-bound code (must be rewritten for iOS):
- `app_window`, `map_view`, `tool_palette`, `side_widgets`, all `*_window`/`*_dialog` files, `window_fit`, `zoom_keys`.
- `tile_atlas` and `sprite_art`, which are lightly Cairo-bound.
- `sound_player`, which is Pulse-specific.

Roughly **~30%** of `src/` plus 100% of the engine is reusable as-is. [INFERRED]

## 2. The licensing blocker (read this first)
**Shipping Lunduke City on the App Store as it stands is very likely a GPLv3 violation. The parties who could object are the engine's copyright holders, Electronic Arts and Don Hopkins.**
- Don Hopkins, who got EA to release Micropolis under GPLv3, has said publicly that the engine *"is GPLv3. So you can't put it on the iPhone, because of the anti-TiVoization clause"* ([HAR 2009 transcript](https://donhopkins.medium.com/har-2009-lightning-talk-transcript-constructionist-educational-open-source-simcity-by-don-3a9e010bf305)). He has also called it *"legally impossible to port … to the iPad"* via the App Store ([HN, 2018](https://news.ycombinator.com/item?id=18761378)). [DOCS]
- Precedent: Apple pulled **VLC** (2011) and **GNU Go** (2010) after a GPL copyright holder complained ([Ars Technica](https://arstechnica.com/gadgets/2011/01/vlc-for-ios-vanishes-2-months-after-eruption-of-gpl-dispute/), [ZDNet](https://www.zdnet.com/article/no-gpl-apps-for-apples-app-store/)). [DOCS]
- **Lunduke owns only its own UI code.** It cannot relicense the engine or the tiles, sprites, and sounds, which are EA's. A **line-by-line Swift translation of the engine is still a derivative work** under the same GPL, so a straight rewrite doesn't escape this. [INFERRED; get a lawyer's opinion if this matters]
- Paths that are *not* App Store distribution:
  - Sideloading onto **our own devices** with xtool, a free or paid account, and 7-day or 1-year dev profiles. That is personal use, not conveying. [INFERRED]
  - A web/PWA build. The upstream [MicropolisCore](https://github.com/SimHacker/MicropolisCore) already compiles the engine to WebAssembly.
- TestFlight is Apple distribution too, so it carries the same risk. EU alternative marketplaces still require Apple notarization, and the anti-tivoization analysis is murky there. [INFERRED]

**What would actually be App Store-safe:**
- (i) A **clean-room city simulation** with **new art and sounds**, reusing only design ideas and Lunduke-original code, or
- (ii) **Written relicensing permission from EA**, which is unlikely, or
- (iii) **Accepting the takedown risk.** Not recommended.

## 3. Port paths
### (b) Keep the C++ engine and session, write a new native iOS front end (best *technical* fit)
- **How:**
  - SwiftPM package with a C++ target (`MicropolisEngine` + `CitySession`, `cxxLanguageStandard: .cxx17`) plus a Swift app target.
  - Use Swift–C++ interop, or a thin C/ObjC++ shim over `CitySession`.
  - Render with **SpriteKit** (`SKTileMapNode`, or a custom `SKNode` that blits from `tiles.png`) or Metal.
  - Make sound with AVAudioEngine.
  - UI in SwiftUI: a tool palette, budget sheet, graphs (Swift Charts), evaluation, the New City wizard, and the scenario picker.
- **xtool fit:** Good. It's SwiftPM, needs no storyboards, and uses loose PNG and WAV resources, which avoids the `actool`/asset-catalog gap.
  - The engine uses only POSIX and the STL, so it should cross-compile with xtool's Darwin SDK. This is **unverified** because the box has no SDK yet (see REPORT.md).
  - Swift/C++ interop and C++ targets through xtool's SwiftBuild path are also **unverified**.
  - Watch-outs: `-Wno-register` and the other flags from `third_party/micropolis/meson.build` must carry over (or use the `-std=c++14` era for the `register` keyword). The `_WIN32` and `dirent` code should compile fine on Darwin. [INFERRED]
- **Effort: M**, dominated by touch UI work rather than engine work:
  - Map view with pan, pinch-zoom, and tile-accurate tap and drag placement. **No hover**, so the footprint preview becomes a "ghost then confirm" or a long-press preview.
  - Tool palette sized for fingers, with roughly 16+ tools.
  - Turning six or more desktop windows into sheets or popovers.
  - iPhone portrait/landscape and iPad split-view layouts.
  - Saving through the app sandbox and Files (`UIDocumentPicker`) instead of `~/.config`, plus autosave on `sceneDidEnterBackground`.
  - Plumbing the engine callbacks for messages and sounds.
  - Rough guess: **4–8 focused weeks** for one developer to reach desktop feature parity. [INFERRED]
- **Reuse:** the whole engine, `city_session`, `messages`, `tools`, `save_path` logic, the recovery logic (with paths adjusted), and all assets. All of the gtkmm/Cairo UI gets rewritten.
- **Blocker:** §2. This route is fine for **personal sideloading** but **not for the App Store**.

### (a) Rewrite in Swift + SpriteKit
- **(a1) Translate the Micropolis engine into Swift:** **L** (~23k lines of dense 1989 C idioms). It **does not fix the GPL problem** (it's a derivative work) and makes behavior drift likely. **Not recommended.**
- **(a2) Clean-room new city sim, new art, new sounds, keeping only Lunduke's brand and UI ideas:** **L/XL**, effectively a new game. It is the **only route that's App Store-safe**. It fits xtool cleanly: pure Swift and SpriteKit, so the Mac is needed only for the usual one-off steps in REPORT.md. Effort is dominated by designing and tuning a simulation that's fun and by producing a full tile/sprite art set. [INFERRED]

### (c) Other routes
- **Godot (GDExtension wrapping the C++ engine):** iOS export from Linux only produces an Xcode project, so **the Mac is needed for every build** ([Godot docs](https://docs.godotengine.org/en/latest/tutorials/export/exporting_for_ios.html)). It has the same GPL problem and gives nothing over (b) for a 2D tile game. Not recommended.
- **SDL2/SDL3 with the existing C++ (replace gtkmm with SDL rendering and ImGui-style UI):** possible, but SDL cross-compiling through xtool is unverified, a touch UI still has to be redesigned, and the GPL problem remains. Worse than (b).
- **Web/PWA:** use the upstream WASM engine (MicropolisCore) or compile ours with Emscripten, and add a touch web UI. Launch it from Safari with "Add to Home Screen". **S–M** effort, **GPL-compatible** (no App Store), and no Mac needed. It's not in the App Store and offline behavior depends on PWA caching. [INFERRED/DOCS]

## 4. App Store review risks (beyond GPL)
| Risk | Assessment |
|---|---|
| **GPL / copyright complaint (EA or Don Hopkins)** | **High, a likely show-stopper.** See §2. |
| **5.2.1 intellectual property / 4.1 copycats** | Medium. The tiles, sprites, and sounds are SimCity Classic's artwork, and Apple can ask for proof of rights. EA's §7 terms forbid using "SimCity" or implying EA affiliation (the current About dialog already disclaims it). EA sells its own SimCity titles on iOS. [INFERRED] |
| **4.3 spam / saturated category** | Low to medium. City builders are crowded, so it needs clear originality. [INFERRED] |
| **4.2 minimum functionality** | Low. It's a full game with 8 scenarios, budget, graphs, and disasters. |
| **Name / branding** | "Lunduke City" is your own brand, so that's fine. Avoid "SimCity" and "Micropolis" in the title and keywords ("Micropolis" is a registered trademark of Micropolis GmbH per [MicropolisCore](https://github.com/SimHacker/MicropolisCore)). [DOCS] |
| **iPad multitasking / orientations** | It must support all iPad orientations or set `UIRequiresFullScreen`. xtool's default Info.plist is portrait-only on iPhone, so set orientations through `infoPath`. [FOUND in xtool source] |
| **Privacy** | No network or tracking, so a simple privacy manifest and the "Data Not Collected" label. |
| **Age rating** | Disasters and the monster are cartoon violence at most, so a low rating. [INFERRED] |
| **Store icon** | Needs an `Assets.car` AppIcon from Mac `actool` (see REPORT.md). The existing original SVG icon can be the source. |

## 5. Recommendation
1. **Don't try to put the current Micropolis-based Lunduke City on the App Store.** The engine and assets are EA's GPLv3 code, and one of its own copyright holders says it can't legally go there.
2. If the goal is **Lunduke City playable on our own iPhones and iPads now:** take **path (b)**, the C++ engine plus a new SwiftUI/SpriteKit touch front end built with xtool. Install it with dev profiles (paid account: 1-year profiles, up to 100 registered devices).
   - Effort: **M**.
   - Mac needed for: the one-time Xcode.xip and SDK, the device USB host, and the Simulator and Instruments if wanted.
   - Optionally publish the source (GPL) and point people to sideloading.
3. If the goal is a **real App Store title:** plan **path (a2)**, an original Lunduke City Mobile with a clean-room simulation and new art.
   - Effort: **L/XL**.
   - It's the cleanest fit for xtool, but it's essentially a new game. Phil's design and UI knowledge carries over; Micropolis code and assets don't.
4. **Cheap interim option:** a **web/PWA** build (path c, web) gives iPhone and iPad players a GPL-clean, no-Mac version quickly.
5. Before committing to (b) or (a2), **check with a lawyer** on the GPL §2 analysis. Also confirm that every author of the Lunduke-original code (the user's bots and contributors) agrees to relicense that part if (a2) reuses any of it.

## 6. What I verified vs. inferred
- **Verified on the box:**
  - The repo location and tags.
  - LOC counts.
  - Which files include GTK/Cairo.
  - The engine's include list (POSIX and STL only).
  - The fixed world size, 100 ms tick, mouse and keyboard handlers, default window size, and Pulse sound.
  - Asset counts, sizes, and dimensions.
  - The `.cty` endianness macro.
  - NOTICE and license terms.
- **Not done:** I didn't build Lunduke City for iOS. The box has no Darwin SDK because Xcode.xip is still needed, so C++-through-xtool compilation, interop, and runtime behavior are **unverified**.
- **Inferred:** the effort sizes, the share of reusable code, and the legal conclusions. The legal points are *not* legal advice.

# Lunduke City 2 — full code + play review

* Repo: https://github.com/BryanLunduke/lundukecity2 @ `1de16cb` (master, just after PR #8). Fresh clone in `repo/`, unmodified (`git status` clean; only `build-x/` locally excluded).
* Build: meson/ninja `-j2`, 0 warnings (`logs/build.log`). Tree identical to 91c34d9 (tests 28 pass / 0 fail / 1 skip).
* Play: Xvfb 1280x800 (normal + `SteamDeck=1`), 1920x1080 normal, resized 1280x720 and 800x500. Input via mouse/keyboard, injected SDL gamepad (A/B/X/Y/LB/RB/Start/D-pad) and touch (tap, two-finger gesture).
  Scenarios played: Detroit (normal), Boston/Rio (Deck). Exercised: all menus, Budget, Graphs, Evaluation, all six overlays, Query, Special buildings, Monster disaster, Save As / Load round trip, Settings, Controls, About, New City wizard, OSK, Quit confirm.
* Screenshots: `shots/` (`n-*` normal mode, `d-*` Deck mode, `w-*` 1920x1080).
* Items fixed by PR #5–#8 and listed in `../city2-review/REVIEW.md` / `../city2-pr8/VERIFY.md` are not repeated. Known leftovers that are still present are included (A3, G9, U6).

Severity: **High** = hurts the core experience or the art direction; **Medium** = clearly noticeable bug or gap; **Low** = polish.

| Area | High | Medium | Low | Total |
|---|---|---|---|---|
| Gameplay bugs | 0 | 4 | 6 | 10 |
| UI issues | 1 | 7 | 15 | 23 |
| Aesthetic | 1 | 1 | 2 | 4 |
| Missing features | 0 | 4 | 8 | 12 |
| **Total** | **2** | **16** | **31** | **49** |

---

## 1. Gameplay bugs

**G1 — Medium — A two-finger touch gesture builds under the first finger.**
Finger-down immediately runs the current tool, so starting a two-finger pan or pinch places a power line or zone and charges for it. In testing, Funds went from $20,000 to $19,995 after a pan gesture. There is also no one-finger pan.
Evidence: `shots/d-touch-pinch3.png` (funds $19,995 after the gesture; `d-touch-pinch2.png` shows $20,000 before). Code: `shell.cpp:1703-1712` (finger-down → `map_.press`), `map_view.cpp:353-367` (`press` calls `on_tool_down` at once).
Fix: wait to apply the tool until the finger moves past a small slop, or for about 120 ms with no second finger. When a second finger lands, cancel the pending stroke and switch to pan/zoom.

**G2 — Medium — Tapping the minimap with a finger does nothing.**
A mouse click on the minimap jumps the view, but a touch tap on the same spot is ignored. On a Deck this is the natural way to navigate.
Evidence: `shots/d-touch-minimap.png` (view unchanged) vs `shots/d-mouse-minimap.png` (view moved). Code: the finger path at `shell.cpp:1703-1715` has no minimap branch and falls through to `on_hotspot`. The mouse path handles it at `shell.cpp:1291`.
Fix: in the finger-down/motion handler, share the mouse minimap hit-test and drag code.

**G3 — Medium — Saving a scenario and loading it again silently drops the goal and timer.**
After Save As and Load of the Detroit scenario, the status bar no longer shows "years left", the goal banner is gone, and the city can no longer be won or lost. Nothing warns the player that this will happen.
Evidence: `shots/n-after-save.png` (scenario running) vs `shots/w-1080-start.png` (same file reloaded: "Detroit Paused Nov 1972", no timer or banner). Code: the `.cty` save/load in `city_session.cpp` stores no scenario id or start date, and no warning exists in `shell_actions.cpp` (the save paths around 640-700).
Fix: save the scenario id, start month and outcome in a small sidecar file (for example `name.cty.json`) or a trailing chunk, and restore them on load. Otherwise, warn "Saving ends the scenario" in the Save dialog.

**G4 — Medium — The first Evaluation after loading a scenario shows nonsense numbers.**
Detroit reports "Net migration (last year): 76,160" with a population of 78,280, and "Change since the last evaluation: +258".
Evidence: `shots/n-eval.png`.
Fix: when a scenario or city loads, seed the previous-population and previous-score values with the loaded values, and show "—" until a full year has passed.

**G5 — Low — LB/RB change the tool while a dialog is open.**
With the Evaluation window up, pressing RB three times changed the tool to Commercial behind the modal window. RB can also open the Special-building dropdown behind a dialog.
Evidence: `shots/d-rb-in-dialog.png` (Commercial highlighted, message bar "Commercial: $100"). Code: `shell.cpp:1554-1560` only checks `!menu_.open()`.
Fix: ignore the shoulder buttons when any dialog, overlay window or OSK is open, or route them to that dialog (for example tab switching).

**G6 — Low — Right and middle mouse buttons do nothing.**
There is no right-drag or middle-drag pan, and no right-click to cancel a drag or switch to Query. Players expect these on desktop.
Evidence: `shell.cpp:1271` returns for any non-left button.
Fix: use middle-drag (and optionally right-drag) to pan, and right-click to cancel an in-progress drag or bulldoze.

**G7 — Low — The default tool is Power lines.**
A new player's first click on the map lays wire and costs money. SimCity/Micropolis players expect a harmless default.
Evidence: `tools.cpp:33` (`kDefaultToolIndex = 3; // Power lines`). `shots/n-main.png` hint bar.
Fix: default to Query (or no tool), or to Bulldozer as in the original.

**G8 — Low — Graph glitches.**
The crime line drops to zero for one sample, and the cash-flow series shows $0 while the Budget estimates −$1,831.
Evidence: `shots/n-graphs.png`, `shots/n-budget.png`.
Fix: skip unfilled history samples (draw from the first valid index), and plot cash flow from the same value the Budget dialog uses.

**G9 — Low — Leftover: the explosion sprite cache leaks.**
`slot()` holds a function-static `std::vector<Frame>` whose cairo surfaces (obj7 explosion frames) are never destroyed. Leak checkers report them at exit.
Evidence: `sprite_art.cpp:24`.
Fix: own the frames in an object with a destructor that calls `cairo_surface_destroy`, or add `sprite_art_shutdown()` called from `Shell::~Shell`.

**G10 — Low (needs confirmation on hardware) — Fast speed feels slow.**
At Fast, only about 3 game months passed in 20 s on Xvfb. This may be software-rendering cost, but redraw throttling could be gating ticks.
Evidence: observed during the Detroit run (`shots/n-detroit-run.png`).
Fix: profile on a Deck. Decouple simulation ticks from frame presentation, so Fast runs at a fixed ticks-per-second rate even when frames drop.

---

## 2. UI issues

**U1 — High — Deck: pressing A on a scenario row does nothing.**
A gamepad player who highlights Boston or Rio and presses A gets no response. The only way to start is to D-pad past the last row onto Cancel and then press Right to reach Play. Rows also have no focus ring distinct from the selection highlight.
Evidence: `shots/d-sc-a3.png` (A on a row, nothing), `shots/d-sc-down-past.png`, `shots/d-sc-down-right.png`. Code: `shell_actions.cpp:1727-1744` (`activate_focus` has no list-row case).
Fix: make A on a list row equal Play (same as a double-click), and draw a clear focus ring. Apply the same treatment to the Load file list.

**U2 — Medium — Deck hint bar never changes.**
The status bar always shows "A build B cancel LB/RB tools Start menu", even inside menus, the Load and Scenario dialogs, and the Evaluation window. Those dialogs also lack the A/B hint row that Welcome and Quit have.
Evidence: `shots/d-start-right.png`, `shots/d-rb-in-dialog.png`, `shots/d-scenario.png`.
Fix: drive the hint text from the current focus context (map, menu, dialog, list, OSK), and add the A/B hint row to every modal dialog.

**U3 — Medium — Overlays are modal pop-up windows.**
Power, pollution, crime and the other maps open in a dialog that blocks the city. You can't build while looking at a power gap, which is the main reason to have overlays.
Evidence: `shots/n-ov-power.png`, `shots/n-ov-crime.png`.
Fix: draw the overlay as a semi-transparent layer on the main map and/or minimap (toggle from the Windows menu or a hotkey) while tools stay active.

**U4 — Medium — Overlay maps are stretched.**
The 120×100 city is drawn into an approximately 663×394 rectangle, so it is visibly squashed and tiles aren't square.
Evidence: `shots/n-ov-pollution.png`. Code: `overlay_map.cpp:214-215` scales x and y independently.
Fix: use `min(sx, sy)` (ideally an integer factor) and centre the map with letterboxing.

**U5 — Medium — R/C/I labels are clipped at 1280×720.**
At a 720-pixel-tall window, the R/C/I letters under the demand gauges are cut off by the status bar, and the gauges shrink to tiny stubs.
Evidence: `shots/n-1280x720-rci-3x.png`, `shots/n-resize-720.png`.
Fix: include the label height in the side-column layout (`chrome_fits`/`Shell::layout`). Shrink the minimap before the gauges, or put the labels beside the bars.

**U6 — Medium — Leftover: below UI scale 1, toolbar icons become tiny and blurry.**
At scale 0.5, or when a small window forces scale below 1 (800×500), icons drop to about 16–20 px and are smoothed. This is the open PR #8 item 5 failure.
Evidence: `shots/n-800x500-toolbar-3x.png`. Code: `ui_scale.hpp:129-132` (the 40 px sprite floor only applies when `scale >= 1.0f`).
Fix: always keep the 40+8 px floor, or use integer pixel-art scaling (20 px source at ×1 or ×2), and drop the smoothing filter for icons.

**U7 — Medium — Help → Controls is incomplete.**
It omits gamepad X/Y/LB/RB/Start and the sticks, all touch gestures, the mouse wheel, Ctrl+wheel and F11 / Alt+Enter.
Evidence: `shots/n-controls.png`.
Fix: generate the page from the real bindings, with separate Keyboard & Mouse, Gamepad and Touch sections. In Deck mode, show the gamepad section first.

**U8 — Medium — The on-screen keyboard (OSK) is hard to use.**
* There is no caret.
* "New City" is pre-selected, but nothing shows it, so the first key silently replaces the text.
* B cancels the whole dialog and throws away typed text, instead of acting as backspace.
* There are no hints for X = backspace, Y = space or Start = done.
* An unexplained "auto" key sits on the name page.

Evidence: `shots/d-after-b-a.png`, `shots/d-osk-a.png`, `shots/d-osk-b.png`, `shots/d-osk-bb.png`.
Fix:
* Draw a caret and a selection highlight.
* Make B backspace, and use B on an empty field to close.
* Add a hint row for the buttons.
* Label "auto" (for example "Random name").
* On Steam, prefer the Steam virtual keyboard (see M4).

**U9 — Low — Welcome shows "A Select B Back" in keyboard/mouse mode.**
Gamepad glyph hints appear even when no gamepad is in use.
Evidence: `shots/n-welcome.png`.
Fix: show hints for the last-used input device, for example "Enter / Esc".

**U10 — Low — Overlay legends use developer jargon.**
For example: "on the engine's crime map" and "The engine stores water as map tiles."
Evidence: `overlay_map.cpp:68-85`, `shots/n-ov-water.png`.
Fix: write player-facing legends, for example "Red = high crime. Build police stations nearby."

**U11 — Low — Overlays are scaled to their own peak.**
Each map is normalised to its own maximum, so a quiet city looks as alarming as a bad one. Low values also render darker than zero cells.
Evidence: `shots/n-ov-crime.png`, `shots/n-ov-landvalue.png`.
Fix: use fixed engine ranges (0–255) with a consistent ramp. Keep empty cells transparent or neutral.

**U12 — Low — The scenario goal banner is permanent and shows no progress.**
The yellow goal box always covers the top-left of the map. It repeats the status bar but never shows the current value (for example, crime now vs the target).
Evidence: `shots/d-rio.png`, `shots/n-detroit.png`. Code: `shell_draw.cpp:1514-1526`.
Fix: fade the banner after a few seconds or on the first action. Put "Crime 72 → goal < 60" in the status bar.

**U13 — Low — Scenario win/lose text is long and technical.**
Evidence: `city_session.cpp:1736-1737` (`scenario_outcome_text`).
Fix: use short SNES-style copy, for example "You did it! Detroit's crime is under control.", plus a one-line stat.

**U14 — Low — "Years left" rounds up.**
In Nov 1972 it shows "10 years left" for a 10-year scenario that started in Jan 1972.
Evidence: `city_session.cpp:1704`. `shots/n-detroit.png`.
Fix: show whole years remaining, rounded down, or "9 years, 2 months".

**U15 — Low — Fullscreen is hidden.**
Fullscreen only works with F11 or Alt+Enter. It is not in Options, Settings or Controls.
Evidence: `shots/n-menu-opt.png`, `shots/n-settings.png`.
Fix: add a "Fullscreen" checkbox to Options and Settings.

**U16 — Low — The Settings "UI scale: Auto (100%)" slider looks half-full.**
It reads like 50%.
Evidence: `shots/n-settings.png`.
Fix: use a separate Auto checkbox with the slider disabled, or a stepped control with labelled stops.

**U17 — Low — Budget sliders have no thumb.**
The Road, Police and Fire bars at 100% are solid blue with no knob, so they don't read as adjustable.
Evidence: `shots/n-budget.png`, `shots/n-720-budget.png`.
Fix: draw a thumb and focus outline, and show the funding amount next to the percentage.

**U18 — Low — New City wizard: hover looks like selection, and the copy is developer-ish.**
The hover style is the same as the selected style, so two options look chosen. Text such as "Default land is the usual generator" is internal wording.
Evidence: `shots/n-newcity-p1.png`, `shots/n-newcity-p1b.png`.
Fix: use a lighter hover style and plain-language descriptions.

**U19 — Low — Menu inconsistencies.**
"Load City..." and "Save City As..." use three dots, while "Scenario…" and "Rename…" use the ellipsis character. "Open Recent" has no submenu arrow.
Evidence: `menu_model.hpp`. `shots/n-menu-sys.png`, `shots/n-menu-sys2.png`.
Fix: use "…" everywhere and draw a ▸ arrow on submenu items.

**U20 — Low — File browser: path label clipped, and duplicate Up.**
The path label's descenders are cut off by the first list row. ".." and the "Up" button do the same thing.
Evidence: `shots/n-load-pathcrop2x.png`, `shots/n-load.png`.
Fix: add 2–4 px of padding under the path label, and drop ".." or the Up button.

**U21 — Low — The Query window is bare.**
It shows plain lowercase fields, doesn't highlight the queried tile, and doesn't show powered/unpowered status.
Evidence: `shots/n-query.png`.
Fix: use title-case labels, outline the tile on the map, and add Power, Pollution and Crime rows as in Micropolis.

**U22 — Low — The gamepad cursor crosshair doesn't scale.**
The crosshair arms are a fixed 10 px, so they're tiny at high zoom or UI scale.
Evidence: `shell_draw.cpp:1530-1538`.
Fix: scale the crosshair with the UI scale and tile size.

**U23 — Low — New cities start paused without saying so.**
The only clue is "Paused" in the status bar, so a new player may think the game is broken.
Evidence: `shots/n-main.png`.
Fix: show a one-time "Game paused — press Space / Start → Speed to run" notice, or start at Slow.

---

## 3. Aesthetic issues

**A1 — High — The map tiles are stock Micropolis art.**
Everything on the map (terrain, roads, zones, buildings) comes from `third_party/micropolis-assets`. The toolbar icons and special buildings are new original pixel art. The result is visually inconsistent and doesn't meet the "original pixel art, SNES-era clarity" direction.
Evidence: `shots/n-detroit.png`, `shots/w-1080-start.png`, `shots/d-rio.png` (the original icon column next to the 1989 tiles).
Fix: commission or produce an original 16×16 tile set in the SNES-style palette, starting with the most-seen tiles (terrain, roads, R/C/I zones). Keep the Micropolis set only as a fallback.

**A2 — Medium — The default zoom shrinks 16 px tiles to 12 px unevenly.**
The default tile size of 12 px uses NEAREST scaling from the 16 px atlas, which drops pixel rows and columns unevenly. Road dashes break up and the "R" zone letters are clipped. Most zoom steps (14, 18, 22…) are also non-integer.
Evidence: `shots/n-detroit-12px-crop4x.png`. Code: `map_view.hpp:78` (`tile_size_ = 12`), zoom step of 2.
Fix: default to 16 px (or 8 px from a hand-made half-size atlas), and limit zoom to integer multiples of 8/16, or pre-render a box-filtered mip.

**A3 — Low — Leftover: the PD and FD letters overlap the badge rim at 1×.**
The white "PD" and "FD" letters run over the gold border of the police and fire icons, which looks crude.
Evidence: `shots/n-pdfd-badge-4x.png`. Art: `data/tool-icons/police.png`, `fire.png` (loaded by `tool_palette.cpp:48-55`).
Fix: redraw the letters 1 px narrower (or drop them and use a pictogram), keeping a 1 px gap inside the rim.

**A4 — Low — Overlay colour ramps are muddy.**
Low values are near-black, the ramps differ between overlays, and the slider/dialog chrome around them is plain.
Evidence: `shots/n-ov-pollution.png`, `shots/n-ov-traffic.png`.
Fix: use one consistent, readable ramp (for example clear → yellow → red) over the actual map tiles (see U3, U11).

---

## 4. Missing features

**M1 — Medium — Several overlays are missing, and "Water" is pointless.**
There are no police coverage, fire coverage, population density, growth rate or rate-of-growth maps. Micropolis has all of these. The "Water" overlay only repeats terrain.
Evidence: `shots/n-menu-win.png`, `shots/n-ov-water.png`.
Fix: add the engine's police, fire, population and growth maps. Replace Water with "Transportation" or "All zones".

**M2 — Medium — There is no message history.**
Messages like "Fire reported", "Need more power" and "Traffic jams" flash in the message bar and are gone. You can't review them or jump to their location.
Evidence: `shots/n-detroit-run.png` (single message line).
Fix: add a Messages window or log (last ~50), where clicking or pressing A jumps to the location.

**M3 — Medium — Graphs lack basics.**
There are no axis values, no time ranges (10 / 120 years), no series toggles and no hover values.
Evidence: `shots/n-graphs.png`.
Fix: add range buttons, legend toggles and a value tooltip or cursor, as in Micropolis.

**M4 — Medium — There is no Steam integration.**
Missing: achievements (for example scenario wins, population milestones), Steam Cloud saves, Steam Input action glyphs, the Steam virtual keyboard for text entry, and the Deck "Verified" basics (correct glyphs, no keyboard needed).
Evidence: there is no Steamworks code in `src/` (`platform.hpp` only checks `SteamDeck=1`).
Fix: add an optional Steamworks layer (ISteamUserStats, ISteamRemoteStorage, `ShowFloatingGamepadTextInput`), and ship a Steam Input action manifest.

**M5 — Low — New City has no terrain preview or Regenerate.**
Players can't see or reroll the map before starting.
Evidence: `shots/n-newcity-p2.png`.
Fix: show a minimap preview with a "New terrain" button (and Y on gamepad).

**M6 — Low — The scenario list doesn't explain each scenario.**
Players don't see the year, the disaster or the challenge. The `summary` strings already exist but are never shown.
Evidence: `city_session.cpp:124-149`, `shots/n-scenario.png`.
Fix: show the summary and goal in the detail pane under the preview.

**M7 — Low — The Load browser shows bare filenames.**
There are no thumbnails, and no population, date or money.
Evidence: `shots/n-load.png`.
Fix: read the city header and show a mini-map thumbnail, population and game date.

**M8 — Low — There is no music.**
Only WAV sound effects exist (`sound_player.cpp`), with no background music or music volume control.
Fix: add optional looping tracks with a separate music volume in Settings.

**M9 — Low — There are no tool hotkeys.**
You can't press B for bulldozer, R for road and so on, as in most city builders.
Evidence: no tool key bindings in `shell.cpp`, and none in `shots/n-controls.png`.
Fix: add single-key tool shortcuts and show them in tooltips.

**M10 — Low — There is no undo.**
A mis-click (for example, a stray touch while panning, G1) is permanent and costs money.
Fix: add one-level undo of the last placement, with a refund, within the same game month.

**M11 — Low — Gifts are bought, not earned.**
SNES SimCity unlocks gifts (park, fountain, casino and so on) at population and score milestones. Here they are bought from the Special dropdown, which removes a progression hook.
Evidence: `shots/n-special-dd.png`.
Fix: grant gifts as milestone rewards with a celebratory message, and optionally keep buying for sandbox mode.

**M12 — Low — The mouse wheel can't zoom.**
The wheel scrolls, and zooming needs Ctrl+wheel. Most modern players expect the wheel to zoom at the cursor.
Evidence: `shots/n-controls.png`.
Fix: add a Settings option, "Mouse wheel: Zoom / Scroll".

---

### Process / cleanup notes
* Nothing in the repo was modified, committed or pushed. No stash was used.
* Apps (PIDs 733776, 729356), Xvfb :972 (722053) and its dbus helpers were killed by exact PID. Xvfb :971 (722051) was left running because platform Chrome (PID 722468 tree) attached to it.

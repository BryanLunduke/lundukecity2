# Lunduke City 2 — PR #10 verification

PR: https://github.com/BryanLunduke/lundukecity2/pull/10, branch `cursor/review-fixes-03b1`, head `cec30c9`
Date: 2026-10-21, CDT. Fresh clone: `/workspace/city2-pr10/repo` (git status clean). **Nothing was merged, committed or pushed.**
Test env: `env -u SteamDeck -u LUNDUKE_CITY2_SIZE -u LUNDUKE_CITY2_SCALE`, clean HOME. Xvfb 1280x800 and 1280x720, UI scales 0.5/1/2, normal and Deck (`SteamDeck=1`). Input was sent through an injected mouse/keyboard/gamepad/touch shim.

## Builds and tests
| Build | Result |
|---|---|
| Linux (build-x) | PASS: 0 warnings, meson test 29 OK / 0 Fail / 1 Skip |
| ASan/UBSan (build-asan) | PASS: 0 warnings, 29 OK / 0 Fail / 1 Skip. In a live run (load, Monster + Tornado at Fast, quit), LeakSanitizer reported only the suppressed fontconfig and Mesa leaks, with no app leaks |
| Windows cross (MinGW) + Wine tests | PASS: 0 warnings, Wine 24 OK / 0 Fail / 1 Skip |

## Per-item results
| ID | Result | Notes |
|---|---|---|
| G1 | PARTIAL | A two-finger pan or pinch no longer builds, and one-finger pan works. But if the first finger rests more than 120 ms before the second lands, the tool still fires (−$11 seen) |
| G2 | PASS | Tapping the minimap jumps the view |
| G3 | PASS | The scenario goal and timer survive save/load |
| G4 | PASS | The first Evaluation after loading is sane |
| G5 | PASS | LB/RB are ignored while a dialog is open |
| G6 | PASS | Right and middle mouse buttons work |
| G7 | PASS | The default tool is no longer Power lines |
| G8 | PARTIAL | Cash flow and crime still drop to the axis on some samples. The readout overlaps the legend, units are raw, and axis labels aren't grouped |
| G9 | PASS | `sprite_art_shutdown()` is called from `~Shell`. The ASan live run with sprites and explosions shows no leaks |
| G10 | PASS | Fast is noticeably faster on Xvfb |
| U1 | PASS | Deck A on a scenario row starts it (keyboard works too) |
| U2 | PARTIAL | Deck hints now change per context, but the wording is inconsistent and the OSK hint is wrong. Normal mode always shows "Enter confirms Esc closes" |
| U3 | PASS | Overlays are drawn on the map, not modal |
| U4 | PASS | Overlays are correctly proportioned and tile-aligned |
| U5 | PASS | R/C/I labels are not clipped at 1280x720 |
| U6 | PASS | Icons are crisp at scale 0.5 |
| U7 | FAIL | Controls page 1 overflows the dialog, has a duplicated line and omits some controls. At 1280x720 the list runs off the window |
| U8 | PARTIAL | The OSK is much better, but Start on a focused key types that key instead of confirming, wrapping focus re-selects all the text so the next key wipes the name, the hints are wrong, and Random can repeat |
| U9 | PASS | |
| U10 | PASS | Plain-language legends |
| U11 | PARTIAL | Most overlays are scaled to fixed ranges. Police/fire are scaled to MAX_POLICE_STATION_EFFECT (1000), so they never reach red |
| U12 | PASS | The goal banner is dismissable and shows progress ("Crime 122 → goal < 60") |
| U13 | PARTIAL | Shorter "You did it! / Time's up." copy, but it still appends a technical sentence ("Keep playing continues at the previous speed. Closing this window leaves the city paused.") and has no stat line (code check, city_session.cpp:1816) |
| U14 | PASS | |
| U15 | PASS | |
| U16 | PASS | |
| U17 | PASS | Budget sliders have thumbs (the Settings volume slider still has none, see bugs) |
| U18 | PASS | Minor overlap on page 2, see bugs |
| U19 | PASS | |
| U20 | PASS | |
| U21 | PASS | Richer Query window (its message bar overflows, see bugs) |
| U22 | PASS | The crosshair scales with zoom |
| U23 | PASS | |
| A4 | PASS | One shared clear→yellow→red heat ramp (growth uses blue for decline). Low values are clear, not black (overlay_map.cpp) |
| M1 | PASS | Police, Fire, Density/Population and Growth overlays added, and Water removed |
| M2 | PASS | Message history window (with issues, see bugs) |
| M3 | PASS | Graphs have ranges, a legend and a readout (see G8 for glitches) |
| M5 | PASS | Terrain preview and Regenerate |
| M6 | PASS | The scenario list explains each scenario |
| M7 | PASS | The Load browser shows city name, date and population |
| M9 | PASS | Tool hotkeys don't clash with existing shortcuts |
| M12 | PASS | The mouse wheel zooms |

Core flows (Welcome, New/Load/Scenario, every tool, special buildings, budget, all disasters, save/load, old saves, quit confirmation): no regressions found. Every Deck dialog can be driven with A/B/D-pad. The new overlays are on the map, non-blocking and correctly proportioned.

## New bugs found
| Severity | Bug |
|---|---|
| Medium | OSK: Start with a key focused types that key instead of confirming, contradicting the PR text and the Controls help |
| Medium | OSK: wrapping focus through the text field re-selects all the text invisibly, so the next key wipes the name |
| Medium | Controls help overflows (much worse at 1280x720), has a duplicate line, and is incomplete |
| Medium | Graphs: readout overlaps the legend, raw units, ungrouped labels, and dips to the axis |
| Medium | The scenario progress meter is stale after start or reload until the next update |
| Low/Med | G1 residual: a resting finger builds once the 120 ms arming fires before the second finger lands |
| Low/Med | Police/fire coverage is never red (scaled to 1000) |
| Low/Med | Messages: the window stays open over the jump destination. "Cannot build here." floods the log |
| Low/Med | Deck: Graphs legend rows and the Settings volume slider get focus with no visible ring |
| Low | OSK hint shows "A confirm B back" when a key is focused. Random can repeat a name |
| Low | Normal-mode hint is always "Enter confirms Esc closes". Escape doesn't close an overlay in keyboard mode |
| Low | Deck: the paused notice says "Press Space"; the message bar shows [Q]-style keyboard hotkeys; Query overlaps the hints; empty Messages shows "A open"; hint wording is inconsistent |
| Low | New City page 2 text overlaps the Difficulty label. Query message bar overflows. The Settings volume slider has no thumb. The sidecar `.json` file isn't JSON. Old saves show Pop 8,140/"Town" until the first census |

## Conflict check vs PR #9 (`cursor/crisp-zoom-badges-a2ad`)
- Against PR #9's current head `4d3c7ba`, `git merge-tree --write-tree` **conflicts in `src/shell.cpp`** (one hunk in `Shell::loop`). PR #9 removes `zoom_cool_`, while PR #10 adds touch arming next to it.
- The conflict is trivial: keep PR #10's touch-arming block and drop `zoom_cool_`. The resolved trial merge (`/workspace/city2-pr10/merge`, uncommitted) builds with 0 warnings and passes tests 29/0/1.
- Against PR #9's earlier head `ef9b806` it merged cleanly.

## Screenshots
Best picks: `/workspace/city2-pr10/shots/best/`
- d-scen-boston.png: Deck scenario picker
- n-ov-growth.png: Growth overlay on the map
- n-messages-2.png: Message history
- n-graphs.png: Graphs
- n-newcity-terrain2.png: New City terrain preview
- d-load-2.png: Load browser (Deck)
- d-query.png: Query (Deck)
- d-osk-typed.png: OSK rename (Deck)

All 190+ shots are in `/workspace/city2-pr10/shots/`. Logs are in `/workspace/city2-pr10/logs/`.

---

# v2 re-verify — head `86b2062` (includes master `308ff6d` / PR #9)

Date: 2026-10-21, about 8:30 PM CDT. Fresh clone `/workspace/city2-pr10/repo-v2`, branch at `86b2062`, `git status` clean. **Nothing was merged, committed or pushed.** Clean env for tests: `env -u SteamDeck -u LUNDUKE_CITY2_SIZE -u LUNDUKE_CITY2_SCALE`, clean HOME. Xvfb 1280x800, 1280x720 and 1280x1000, normal and Deck, scale 1 and 2. Input came through the same injected touch/gamepad shim plus xdotool.

## Builds and tests
| Build | Result |
|---|---|
| Linux (build-x) | PASS: 0 warnings, 29 OK / 0 Fail / 1 Skip |
| ASan/UBSan (build-asan) | PASS: 0 warnings, 29 OK / 0 Fail / 1 Skip |
| Windows cross + Wine | PASS: 0 warnings, Wine 24 OK / 0 Fail / 1 Skip, zip packaged (EXIT=0) |

## The 7 items that were partial or failing in v1
| ID | v2 | Notes |
|---|---|---|
| G1 | **FAIL (regression)** | **Fixed:** with the first finger resting 0.2 s, 0.5 s or 1 s before the second, a two-finger pan or pinch leaves funds unchanged. A drag-build works. A stroke armed by motion and then cancelled by a second finger is refunded.<br>**Broken:** (1) **a single tap no longer builds.** I tried an instant tap and 150 ms and 300 ms holds on verified dirt; funds didn't change and nothing was built. On FINGER_UP the finger is erased before the `fingers_.size()==1` branch, so the pending-tap `press/release` never runs. Touch-to-mouse is disabled, so nothing else builds it. The Controls page even says "A tap on the map builds when the finger lifts".<br>(2) **The provisional stroke is never committed after a drag** (see the Critical bug below) |
| G8 | PASS (notes) | Units are now "people" and "level", and labels are grouped. Crime no longer drops to the axis; cash flow only does so when it is really negative. The Deck legend shows each series' value. Low: the y-axis shows a population range, but each series is normalized on its own scale. Low: the normal-mode top readout is clipped at the right edge |
| U2 | PASS (Low notes) | A keyboard-only normal session shows "Click builds · Arrows pan · Space pauses" and "Enter confirms · Esc closes". Deck hints change with context. Low: after any touch, `touch_used_` switches normal mode to pad wording ("A builds", "B closes the map") for the rest of the session |
| U7 | PASS | Gamepad, Keyboard and Touch pages fit at 1280x800 and 1280x720, with no duplicate line and every tool key listed. At scale 2 the keyboard page scrolls with Down. Caveat: see the paused-repaint bug, and there is no scroll indicator (Low) |
| U8 | PASS | Start on a focused key confirms (rename → "Stone Ferry0"). Wrapping focus through the field doesn't select all ("q" → "qq"). Random gave 10 presses with no back-to-back repeat. The caret lands at the end. Hints read "A types · B deletes/closes · Start confirms" |
| U11 | PASS | Police coverage reaches red/orange next to stations. The legend says "Red means a police station covers this block" |
| U13 | PASS | "You did it! …" / "Time's up. …", plus a stat line ("Population 18100 → 100,000") and Stay paused / Scenario list / Keep playing. The technical sentence is gone. Low: the stat has no thousands separator |

## The v1 new-bug list, re-tested
| v1 bug | v2 |
|---|---|
| OSK: Start types a key / wrap select-all / Random repeats | FIXED |
| Controls overflow, duplicate line, incomplete | FIXED |
| Graphs readout, units, dips | Mostly fixed (Low notes under G8) |
| Stale scenario progress meter | FIXED. Crime 128 at start, then 124 → 121 → 112 → 108 while running. A legacy-sidecar save shows 120 straight after load |
| Messages window stays over the jump target | FIXED: the window closes on jump |
| "Cannot build here." floods the log | FIXED: one entry after a 33-sample drag across buildings |
| Deck focus rings on the Graphs legend and volume slider | FIXED |
| Escape doesn't close an overlay | **PARTIAL.** The state closes, but while paused the screen doesn't repaint until the mouse moves (`overlay_on_` isn't in `visual_stamp`). Two Escapes looked like they did nothing. Pad B repaints |
| Deck paused notice | FIXED: "Game paused. Press Start, then Speed, to run the city." Nit: there is no "Speed" menu item; it's Options → Slow/Medium/Fast |
| [Q]-style hotkeys in the Deck message bar | FIXED ("Bulldozer: $1", "Query") |
| Query overlaps the hints | FIXED |
| Empty Messages shows "A open" | FIXED (only "B closes") |
| New City page 2 overlap | FIXED at 1280x720 |
| Query message bar overflow | FIXED (ellipsized) |
| Settings volume slider has no thumb | FIXED: the thumb moves (100% → 85%) |
| `.json` sidecar isn't JSON | FIXED. A fresh Save As of a scenario city gives valid JSON (`python3 -m json.tool`) and reloads with its goal. A legacy key=value sidecar still loads |
| Old saves show Pop 8,140 / "Town" | FIXED (the Load browser shows OldMaster-Detroit at Pop 71,740 Capital) |
| Police/fire never red | FIXED |

## Integration with PR #9 (master 308ff6d)
All PASS (1280x1000 window, tiles found against the atlas):
- **Zoom steps:** keyboard 8 → 16 → 32 → 48 → 64. The centre tile stays at (60,49) at every step.
- **No drift from 8px:** 64 → 8, then in/out cycles, then back to 16 keeps (60,49).
- **Ctrl+wheel is anchored:** (41,38) stays put on 16 → 32, and (57,51) stays put on 32 → 16.
- **Pinch:**
  - A 2× spread at (900,700) goes 16 → 48 with (57,51) kept.
  - A 1.17× spread does nothing (hysteresis).
  - A 0.5× pinch-in at (400,400) goes 48 → 32 with (46,45) kept.
  - Funds are unchanged.
- **Triggers:** RT held for 1.2 s moves one step. Two LT presses move two steps.
- **Gamepad at 8px:** the target is tile-aligned and outlined, and A builds there (−$11 for the tree clear plus road).
- Low, pre-existing (the master binary at 7bf171d does the same): in a 1280x1000 window the 8px cursor can rest in the right grey band at the window edge, and goes off-screen at the far bottom-right.

## Regression pass
No regressions outside touch. Checked:
- Welcome (Deck)
- New City: OSK name, land page with live preview, Generate. The paused notice appears after Generate.
- Scenario picker and start (Detroit)
- Load browser
- Save, Save As
- Recovery prompt
- Budget
- Disasters → Fire: Cancel is the default and Start ignites it, with auto-goto
- Quit → "Save changes?"
- Messages, Graphs, Settings, Rename, Query
- Tool hotkeys, LB/RB
- Minimap tap jump (G2)
- Sim speeds
- The other previously passing items spot-checked fine

## New bugs (v2)
| Severity | Bug |
|---|---|
| **Critical** | **Stale touch "provisional stroke" reverts or corrupts the city.**<br>**Cause:** on the last finger-up of a touch drag, the code takes the `else` branch (fingers_ is already empty). So `commit_provisional_stroke()` is never called and `stroke_active_` stays true. `begin_provisional_stroke()` then no-ops on every later stroke, and nothing (including Load) clears it.<br>**Effect:** the next time a touch stroke arms (more than 8 px of motion) and a second finger lands, which is a normal pinch or pan, `refund_provisional_stroke()` restores the snapshot from the **first** touch drag of the session.<br>**Repro 1:** touch-drag a road (−$80), mouse-drag a road (−$80), then start a touch stroke and add a second finger. Both earlier roads vanish and funds return to $15,920 (`g1-refund-funds.png`, `g1-refund-reverts-earlier-strokes.png`).<br>**Repro 2:** touch-drag, then load ScenJSON (Sep 1972, $20,000), then stroke plus a second finger. The **previous city's map and funds ($15,920, the Feb-1974 layout) overwrite the loaded city**, while the date and scenario stay Sep 1972 (`g1-stale-refund-before-ScenJSON-loaded.png` → `g1-stale-refund-after-old-map-restored.png`) |
| **High** | A single tap on the map no longer builds (G1 above) |
| Medium | The same finger-up `else` path also never resets `touch_armed_`/`touch_pending_`/`touch_minimap_`. After a minimap tap, the next one-finger drag on the map jumped the view instead of building (seen once; matches the code) |
| Medium | Paused-redraw gaps: keyboard Escape closing an overlay, Controls page/scroll changes, and (pre-existing) A on a New City land option don't repaint until another input. `visual_stamp` lacks `overlay_on_`, `controls_page_`/`controls_scroll_` and the new-city option fields |
| Low | After any touch, normal mode keeps pad-style hints (`touch_used_`) |
| Low | Graphs: the y-axis range label is misleading for normalized series. The normal-mode readout is clipped on the right |
| Low | Paused-notice wording ("Speed"). The OSK hint says "A types" on Random. The win/lose stat has no thousands separator. There is no scroll indicator on Controls |
| Low (pre-existing) | The 8px gamepad cursor can sit in the right grey band (1280x1000) |

## Screenshots (v2)
Best: `/workspace/city2-pr10/shots-v2/best/`
- 01-deck-scenario-picker.png
- 02-police-coverage-overlay.png (on-map overlay)
- 03-load-browser.png
- 04-deck-messages.png
- 05-deck-graphs.png
- 06-deck-controls-help.png
- 07-deck-osk.png
- 08-deck-terrain-preview-720.png
- 09-scenario-won.png
- 10-scenario-lost.png

Bug evidence: `shots-v2/g1-refund-funds.png`, `g1-refund-reverts-earlier-strokes.png`, `g1-stale-refund-before-ScenJSON-loaded.png`, `g1-stale-refund-after-old-map-restored.png`, `e1–e9` (Escape/overlay repaint), `k-ctl2*` (Controls repaint), `k-terr*` (New City repaint), `gp*` (8px gamepad). All other shots are in `shots-v2/`, and logs are in `logs-v2/`.

---

# v3 re-verify — head `208f4c2` (branch `cursor/review-fixes-03b1`)

Verified 21 Oct 2026, 20:46–21:20 CDT. Not merged, committed or pushed. repo-v2 was detached at 208f4c2 with a clean status (no stash). Earlier PR #9/#10 build dirs were already gone; only the current repo-v2 build-x, build-asan and build-windows remain. Disk: 53 GB free.

## Builds and tests

| Build | Result |
|---|---|
| Linux (build-x) | PASS: build 0 warnings; meson test 29 OK / 0 Fail / 1 Skip |
| ASan/UBSan (build-asan) | PASS: build 0 warnings; 29 OK / 0 Fail / 1 Skip, no sanitizer reports |
| Windows cross + Wine | PASS: 0 warnings; Wine 24 OK / 0 Fail / 1 Skip; zip built |

Logs: `logs-v3/build-x.log`, `build-asan.log`, `test-x.log`, `test-asan.log`, `windows.log`.

## Touch (snapshot/restore removed)

Driven through the LD_PRELOAD injector with real SDL finger events, at 1280x800 with the sim paused. Funds were read from each screenshot.

| Check | Result |
|---|---|
| Single tap builds exactly once (instant, 300 ms and 1 s holds) | PASS. $15,920 → 910 → 900 → 890; each tap changed one tile |
| Two-finger pan after a 0.2, 0.5 or 1 s rest | PASS. Nothing built; funds stayed $15,890 |
| Pinch after a 0.2, 0.5 or 1 s rest | PASS. Nothing built; funds stayed $15,890; zoom went to 64 |
| Drag-build builds as it goes | PASS. A mid-drag shot already shows the tiles (`c1mid`); an 8-tile stroke cost $80 |
| Second finger during a drag | PASS. The stroke ended at $15,660 (−$60) and nothing was refunded. Its tiles, an earlier touch stroke and an earlier mouse stroke all stayed (`c3mid` vs `c3`). The second finger then panned |
| Load a different city after touch, then pinch | PASS. Loaded ScenJSON by touch (Discard → list → Open), $20,000. After the pinch, funds were $20,000 and the map was identical when compared aligned (`d3` vs `e2`) |
| Minimap tap, then a one-finger map drag | PASS. The drag built a road (−$90) and the view did not jump (`g0`/`g1`) |
| Toolbar touch | PASS. Residential tool selected |
| Menu and dialog touch | PASS. Windows menu → Graphs, the 120 year button, Close, Save-changes Discard, Load list and Open, Help → Controls |
| Touch list drag-scroll (Scenario list) | PASS. It scrolls. This existed before this PR: the selection follows the scroll (see notes) |
| Partial lift during a pinch | PASS. Nothing built (funds unchanged in every pinch run) |

By design (code review): if the first finger moves more than 8 px before the second finger lands, the stroke has already armed and keeps what it built. The Controls Touch page says so.

## Paused redraws

| Check | Result |
|---|---|
| Escape closes the Crime overlay with no mouse motion | PASS. The overlay is gone in the next shot (`i1`/`i2`) |
| Controls page Left/Right | PASS. Each key press repainted (`j1`–`j5`) |
| Controls scroll (scale 2, Down × 5) | PASS. Content and thumb moved (`s3`/`s4`) |
| New City land option with A (Deck) | PASS. Trees Wooded, then None, each repainted immediately along with the preview (`l6`/`l7`) |

## Low items

| Item | Result |
|---|---|
| Hints switch back after mouse or keyboard input | PASS. After touch, mouse motion and keyboard input both bring back "Enter confirms / Esc closes" |
| Graph axis and readout | PASS. With several series the axis reads "high"/"low". The readout uses Pop/Res/Com/Ind/Cash and fits at both edges (`h4`, `h5`) |
| Win/lose thousands separator | PASS for commas, but **FAIL** for the text: the population shows a "$" (new bug 1) |
| Pause notice | PASS. Desktop: "Game paused. Press Space, or Start then Options, to run the city." Deck: "Game paused. Press Start, then Options, to run the city." |
| Random hint | PASS. "A picks a name  B closes  Start confirms" (`best/07-deck-osk.png`) |
| Controls scroll thumb | PASS at scale 2 (`demo/controlss2.png`, `best/11-…`) |
| 8px cursor clamp at 1280x1000 | PASS. Rio at 8px; the map ends at x=1175. Holding the stick right, the tile cursor stops on the last column (x 1166–1174), inside the map (`cursor-clamp-1280x1000.png`) |

## Regression (PR #9 zoom and core flows)

- **Ctrl+wheel zoom:** PASS. 16→48→16 three times; 1 px vertical settle on the first cycle only, then 0 px drift on later cycles.
- **Pinch anchor:** PASS. 16→48, the point under the fingers stayed within 3 px.
- **Trigger zoom:** PASS. RT/LT step one level each (16↔32).
- **Gamepad:** PASS. LB/RB pick tools; D-pad plus A builds (−$10).
- **Core flows:** PASS. Welcome, New City (name, Random, land, Generate), Load (touch), Scenario (Rio, Dullsville), Budget, Fire disaster, Save As (V3Save.cty and its .json written), and Quit (save prompt with Discard; a clean quit after saving).

## New bugs (v3)

1. **Medium: population shown as money.** `with_commas()` in city_session.cpp adds a "$". The new `scenario_meter()` and `scenario_outcome_text()` calls therefore show "Population $17,940 → 100,000". This appears in the status bar of every population-goal scenario (seen in Dullsville and Hamburg) and in the Won/Lost text. The non-scenario fallback would also read "score $N". Fix: a comma-only helper, or strip the "$" for these calls. Seen in `best/09-scenario-won.png`, `best/10-scenario-lost.png` and `best/05-deck-graphs.png` (status bar).

Notes (not regressions from this PR):
- Touch-dragging the Scenario/Load list moves the selection with the scroll, because finger-down selects the row under the finger and `nudge_file_list` moves it. A drag can therefore leave a different scenario selected (Boston → Rio). Low; this behaviour is older than this PR.
- Holding the stick down at 8px lets the gamepad pointer leave the map into the hint bar. Bringing it back takes the stick; the D-pad does not. Low/nit.
- The Hamburg "lost" demo still has 110,300 people, which is a demo fixture.

## Screenshots (v3)

Reused from v2 (unchanged): `shots-v2/best/01`–`04`, `06`, `08`.

Re-shot in `shots-v3/best/`:
- `05-deck-graphs.png` (high/low axis and short readout)
- `07-deck-osk.png` (Random hint)
- `09-scenario-won.png` and `10-scenario-lost.png` (commas, with the "$" bug visible)
- `11-controls-scroll-thumb-scale2.png` (new)

Supporting shots:
- Pause notice: `shots-v3/demo/paused.png`, `pauseddeck.png`
- Cursor clamp: `shots-v3/cursor-clamp-1280x1000.png`
- Raw step shots for each check: `shots-v3/` (`b*`, `p*`, `z*`, `c*`, `d*`, `e*`, `g*`, `h*`, `i*`, `j*`, `k*`, `l*`, `o*`, `s*`, `w*`, `pz*`, `cb`, `cf*`, `sv*`)

---

## v4: PR #10 at 530de82011343cc82ec75f37e154a4e1600f835e (verified 2026-10-21, CDT)

Repo: repo-v2, detached at 530de82, clean. No stash, commits, pushes, comments or merges.
Env: SteamDeck and LUNDUKE_CITY2_SIZE unset for builds and tests.

| # | Item | Result |
|---|------|--------|
| 1 | Builds and tests: Linux, ASan, Windows/Wine, 0 warnings | PASS |
| 2 | Population/score have no "$"; money keeps "$" | PASS |
| 3 | List touch: drag scrolls, tap selects, tap on selected opens | PASS, with 1 Medium bug |
| 4 | Gamepad clamp at 8px and 32px; D-pad | PASS |
| 5 | Regression: touch, zoom, gamepad build, core flows | PASS |

### 1. Builds and tests
- build-x: clean rebuild, BX=0, 0 warnings. Tests 29 OK / 0 Fail / 1 Skip, round6 OK (logs-v4/build-x.log, test-x.log).
- build-asan: clean rebuild, BA=0, 0 warnings. Tests 29 OK / 0 Fail / 1 Skip, no ASan reports (logs-v4/build-asan.log, test-asan.log).
- Windows cross-build plus Wine: 24 OK / 0 Fail / 1 Skip, EXIT=0, 0 warnings, zip built (logs-v4/windows.log).

### 2. Population and score
- Harness (scratch-v4/outc.txt): all 8 scenarios. The meter and the won/lost text have no "$" ("Population 18,100 → 100,000", "Score 686 → goal > 500"). Funds_text is "Funds: $5,000".
- Dullsville won demo and Hamburg lost demo: no "$" on population or score; Funds keeps "$".
- Live status bar: "Population 18,100 → 100,000" next to "Funds: $5,000".
- Money still shows "$" in Budget (taxes, cash flow, funds, fund requests), the tool cost line "Roads: $10. Bridge: $50", and the Load list "Capital $…".

### 3. List touch
- Scenario list:
  - Touch drag scrolls and keeps the highlight (a1). Tap selects Hamburg (a2).
  - Drag starting on the selected row keeps it.
  - Keyboard Down, D-pad and mouse click all select.
  - An offscreen selection plus pad A opens Dullsville.
- Load list (13 files):
  - Drag keeps City01. Tap selects City04 (c1). Tapping City04 again opens it: "Loaded a saved city" (d1).
  - Mouse scrollbar works.
- Messages:
  - Touch drag scrolls with no selection change (m2).
  - A tap selects the row and jumps when the notice has a place (the plane crash row closed the dialog, m5). A notice with no place ("Crime is very high") only highlights, as designed by jump_notice/has_place.

**BUG (Medium): list_dragging_ stays set after a touch on the list scrollbar.**
- Cause: list_dragging_ is cleared only on mouse button up (shell.cpp ~1463). Finger up and reset_touch_state() do not clear it.
- After any touch on the Scenario or Load scrollbar:
  - later touch drags on content take the nudge path and move the selection (City04 → City10, c2);
  - plain mouse motion with no button pressed drags the list and the selection (ScenJSON selected, c3).
- Impact: this breaks the new "drag only scrolls" guarantee, and Open/Enter can then load the wrong city.
- The clearing code is the same in 86b2062 and 208f4c2, so the cause is older, but this commit's guarantee now depends on it.
- Fix: clear list_dragging_ on SDL_EVENT_FINGER_UP/CANCELED and in reset_touch_state().

**Low: touch drag on the scrollbar thumb runs inverted.** It uses nudge(-rows) content semantics, so moving the finger down moves the list up (b3). This is older behaviour, but the comment says "scrollbar still tracks the finger".

**Observation (older): a mouse drag on the scrollbar moves the selection** (b2). The keyboard behaves the same way.

### 4. Gamepad clamp
- 32px, 1280x800: stick held down 8 s, then right 8 s. The cursor stops on the last map row and column, at the bottom-right inside the map, never in the hint bar (g1, g2).
- 8px, 1280x800: same result, at the bottom-right of the map (e2).
- 8px, 1280x1000, letterboxed below the map:
  - Stick down: the cursor stops on the bottom map row (q4).
  - Then right: it stops in the bottom-right map corner (q5), not in the grey band.
  - A brief stick up moves it back (q7).
- D-pad still works:
  - It pans the map at 32px (g3) and at 8px/800 (e3).
  - In the Start menu it moves the highlight (q8).
  - At 8px/1000 the whole map is visible, so a D-pad pan has no effect, as expected.
- Not tested: D-pad cursor stepping while A is held (it needs real button state; the injector only sends events).

### 5. Regression
- Touch tap build: −$10. Touch drag build: −$70. A second finger mid-drag pans and does not build more.
- Two-finger pan: shifted the view, funds unchanged. Pinch: 16→48px, funds unchanged (t1–t5).
- Ctrl+wheel: 16→32→16→32.
- Triggers: RT 32→48; LT steps down to 8.
- Gamepad: LB/RB cycle Roads↔Rail. A on water gives "No place to anchor a bridge". A on land builds a road, −$10 (p8).
- Core flows:
  - Load and scenario: covered in item 3.
  - Save As V4Save.cty: file written.
  - Budget: opens.
  - Fire: "Fire reported".
  - New City (Save changes? → Discard → name → terrain → Generate): V4Town, $20,000, Jan 1900.
  - System → Quit: exits cleanly.

### Best screenshots (shots-v4/best/)
- 01-dullsville-won.png, 02-hamburg-lost.png
- 03-scenario-touch-drag-keeps-highlight.png, 04-scenario-tap-selects-hamburg.png
- 05-load-tap-selects-city04.png, 06-load-tap-selected-row-opens.png
- 07-BUG-after-scrollbar-touch-drag-moves-selection.png, 08-BUG-after-scrollbar-touch-mouse-hover-drags-list.png
- 09-messages-touch-drag-scrolls-only.png
- 10-clamp-32px-stick-down-right.png, 11-clamp-8px-1280x1000-stick-down.png, 12-clamp-8px-1280x1000-stick-down-right.png
- 13-touch-build-pan-pinch.png, 14-gamepad-build-minus10.png, 15-budget-dollars.png, 16-new-city.png

---

## v5: PR #10 at ae57e19f730abb28ef0ff345fc91633230e96326 (verified 2026-10-21, CDT)

Repo: repo-v2, detached at ae57e19, clean. No stash, commits, pushes, comments or merges.
Env: SteamDeck and LUNDUKE_CITY2_SIZE unset.

| # | Item | Result |
|---|------|--------|
| 1 | Builds and tests: Linux, ASan, Windows/Wine (clean), 0 warnings | PASS |
| 2 | v4 Medium repro and list behaviour | PASS (the v4 Medium and Low bugs are fixed) |
| 3 | Regression: population/score, clamp, touch, zoom, core flows | PASS |

### 1. Builds and tests
- build-x: clean, 153 compiles, BX=0. Tests 29 OK / 0 Fail / 1 Skip. round6, review and review_ui all OK.
- build-asan: clean, 153 compiles, BA=0. Tests 29 OK / 0 Fail / 1 Skip, no ASan reports.
- Windows: `ninja clean` on build-windows/build (CLEAN=0), then a full rebuild (150 steps). Wine 24 OK / 0 Fail / 1 Skip, EXIT=0, zip built at 22:13.
- No "warning" in any log in logs-v5/.

### 2. List scrollbar fix (Load list has 12 files)
- **Load list:**
  - Tap City04, then touch the scrollbar thumb and lift, then drag the list body up 210px. The list scrolls and the highlight does not move (L2, L3).
  - The selection, now offscreen, is kept: keyboard Down moves it to the next row and reveals it (L5).
  - Mouse hover with no button after the touch: no scroll and no selection change (L4).
- **Thumb direction:** a touch drag of the thumb from y 200 to 550 scrolls down and selects City11, following the finger (L6). Lift and hover after that: no change (L8).
- **Body drag after a thumb drag:** scrolls only (L9).
- **Mouse thumb drag up:** goes to the top and selects City02 (L10). Hover after release: no change (L11).
- **Scenario list:**
  - Tap Hamburg, touch the scrollbar and lift, which selects Bern by track position. A body drag then scrolls and keeps Bern (S3); hover gives no change (S4).
  - Touch and mouse thumb drags give the same index for the same finger path: Boston at y=580 for both (S5/S8), Rio and scrolled for both at y=680 (S9/S10).
  - Mouse thumb drag up reaches Dullsville (S6). D-pad down moves to San Francisco (S7).
  - Tap Tokyo selects (S11); tapping it again plays Tokyo (S12). Its status bar reads "Score 624 → goal > 500" with no "$".
- **Load, other input:** mouse click City05 (F1), keyboard Down to City06 (F2), D-pad down to City07 (F3), tap City08 (F4), tap City08 again opens it: "Loaded a saved city" (F5).
- **Not driven live:** FINGER_CANCELED (the injector cannot send it). The new review_ui check covers the lift and buttonless-motion paths, and it passes.
- **Observation (by design, same as the mouse):** a tap on the scrollbar track sets the selection to the row at that track position.

### 3. Regression
- **Text:** the won demo (Dullsville) and lost demo (Hamburg) show no "$" on population (shots-v5/demo/). Budget shows "$" everywhere (c2), as do "Roads: $10" and the Load list "Capital $15,920".
- **Touch:**
  - Tap build −$10. Drag build −$70. A second finger mid-drag stops the build.
  - Two-finger pan moves the view with funds unchanged. Pinch goes 16→48px with funds unchanged (t1–t5).
- **Zoom:** Ctrl+wheel 48→32→16→32. RT 32→48, LT 48→32.
- **Gamepad:**
  - LB/RB cycle Roads↔Rail. A builds a road, −$10 (p3).
  - Clamp at 32px: stick down then right ends in the bottom-right corner inside the map, not in the hint bar (g1). The D-pad then pans the map up (g2).
  - Clamp at 8px (1280x800): bottom-right corner of the map (g3). The clamp code is unchanged since v4; the 1280x1000 case was not re-run.
- **Core flows:** Save As V5Save.cty (file written), Budget, Fire ("Fire reported"), New City (Discard, then V5Town, $20,000, Jan 1900), System → Quit (exits cleanly). Load and scenario are covered in item 2.

### Bugs
- None found.
- The v4 Medium bug (list_dragging_ stuck after a scrollbar touch) is fixed.
- The v4 Low bug (inverted touch thumb) is fixed.

### Best screenshots (shots-v5/best/)
- 01-dullsville-won-no-dollar.png, 02-hamburg-lost-no-dollar.png
- 03-load-after-scrollbar-touch-body-drag-scrolls-only.png, 04-load-hover-no-change.png, 05-load-hidden-selection-kept-keyboard-down.png
- 06-load-touch-thumb-drag-down-follows-finger.png, 07-load-mouse-thumb-drag-up.png
- 08-scenario-after-scrollbar-touch-body-drag-keeps-bern.png, 09-scenario-hover-no-change.png
- 10-scenario-touch-thumb-to-bottom.png, 11-scenario-mouse-thumb-to-bottom-same.png
- 12-scenario-tap-selected-plays-tokyo.png, 13-load-tap-selected-opens.png
- 14-touch-build-pan-pinch.png, 15-clamp-32px.png, 16-clamp-8px.png, 17-budget-dollars.png, 18-new-city.png

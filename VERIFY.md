# PR #9 verification: crisp zoom + PD/FD badges

* PR: https://github.com/BryanLunduke/lundukecity2/pull/9, branch `cursor/crisp-zoom-badges-a2ad`, head **ef9b806**. Base: master 1de16cb.
* Fresh clone in `repo/` (branch `pr9` = FETCH_HEAD). No changes, no stash, no push, no merge.
* The PR changes 9 files:
  * `src/map_fit.hpp`: adds `snap_map_tile`, `step_map_tile` and `snapped_map_origin`.
  * `map_view.{hpp,cpp}`: default tile size 12 → 16, and `set_tile_size` snaps.
  * `shell_actions.cpp`: `zoom_by` uses `step_map_tile`.
  * `shell_draw.cpp`: the map origin is floored to a device pixel.
  * `data/tool-icons/{police,fire}.png`: art changes.
  * Two new tests.

## Results

| # | Item | Result |
|---|---|---|
| 0 | Tile art unchanged | **PASS.** `git diff origin/master..ef9b806 -- third_party data/special-tiles` is empty. sha256 values match master: `tiles.png` e80506a7…, `zoo.png` 57c2756a…, `big-park.png` 487d07c6…, `library.png` c7773ed3…. The Windows zip's `tiles.png` also hashes e80506a7…. The only changed art is `tool-icons/police.png` and `tool-icons/fire.png`. |
| 1 | Linux build + tests + ASan | **PASS.** `build-x`: 0 warnings, 28 OK / 0 Fail / 1 Skip (round11, as before). `build-asan` (address,undefined): 0 warnings, 28 / 0 / 1. **Live ASan session** (1280x800): Ctrl+wheel zoom 16→64→16, wheel scroll, minimap click, road click and drag, Ctrl+= / Ctrl+-, a two-finger pinch, gamepad RT zoom, right-stick pan, sim run into Dec 1972, then Quit → Discard. No ASan, UBSan or LSan reports; the log has only the standard fontconfig/GLX suppressions (`logs/asan-live.asan.764990`). |
| 2 | Windows cross build + Wine tests | **PASS.** `packaging/windows/build-windows.sh` (JOBS=2): EXIT=0, 0 warnings, Wine tests 23 OK / 0 Fail / 1 Skip. The zip's police/fire PNGs are byte-identical to `data/`. Wine headless smoke run exits 0 (`logs/windows.log`, `logs/wine-smoke.log`). |
| 3a | Default zoom is crisp (normal + Deck, 1280x800) | **PASS.** Default is 16 px in both modes. Pixel check (`scratch/crisp.py`): **100%** of on-screen 16 px cells are uniform. **Every interior cell is a byte-exact copy of an atlas tile.** The ~2% of non-matching cells sit on the bottom and right edges under the scroll indicators. Road dashes are even and the R/C/I zone letters are intact. Evidence: `shots/normal1280-default.png`, `shots/deck1280-default.png`, `shots/default-12-vs-16-4x.png` (master 12 px vs PR 16 px, 4× crop). |
| 3b | Every zoom step, all inputs | **PASS.** Steps are exactly 16 / 32 / 48 / 64, one step per notch. 64 is the top and 16 the bottom; extra notches do nothing.<br>• **Mouse** Ctrl+wheel up: `zoom-16/32/48/64.png`. Down: `zoomout-48/32/16.png`, `zoomout-16b.png`.<br>• **Keyboard** Ctrl+= / Ctrl+-: `zoomout-key.png`.<br>• **Gamepad** LT/RT, one tap per step: `zoom-pad-out1..3.png` (48 → 32 → 16), `zoom-pad-in1..3.png` (32 → 48 → 64).<br>• **Pinch**, one gesture per step: `zoom-pinch-out1..4.png` (48 → 32 → 16 → 16), `zoom-pinch-in1..3.png` (32 → 48 → 64).<br>At every step, every interior cell is an exact N/16-scaled atlas tile with uniform pixel blocks. There are no seams or gaps. |
| 3c | No jitter while scrolling | **PASS.** Wheel scroll at 16 and 32 (`scroll-16-wheel.png`, `scroll-32-wheel.png`) stays fully crisp. Right-stick pan at fractional speed (0.37 / 0.23) was captured twice mid-motion at 64 px (`scroll-pad-64-moving.png`, `-moving2.png`). Both frames are 100% uniform, apart from the gamepad crosshair cells, and so is the stopped frame. The floor in `snapped_map_origin` is doing its job. |
| 3d | Clicks and builds land on the right tile | **PASS.**<br>• **Mouse road placement** at 16, 32, 48 and 64 px: two clicks per zoom, one 1 px inside a cell's top-left corner and one 2 px inside the bottom-right corner of another cell. Both target cells became road at every zoom; the other changed cells were tree-edge smoothing from auto-bulldoze. Files: `ztest-{16,32,48,64}-{before,after}.png`. The 32/48/64 runs came after minimap jumps, so the scroll was fractional.<br>• **Gamepad A** at 64 px: the cursor tile outline and the built wire match the crosshair's cell (`pad-a-64-land.png`, `pad-a-64-land-crop2x.png`).<br>• **Touch tap** at 64 px: the tapped tile is outlined and built (`tap-hover.png`). |
| 3e | Minimap viewport rectangle | **PASS.** The rectangle shrinks in proportion: inner white box 60×34 → 30×16 → 18×10 → 14×8 px at 16/32/48/64, against an expected 63×37 → 31×18 → 21×12 → 16×9 (minimap ≈ 0.87 px per tile, minus the 2 px frame). After a minimap click at 64 it sits at the clicked corner (`ztest-64-before.png`). |
| 3f | Zoom-out still usable at 1280x800 | **PASS, with a trade-off.** The map view is 1162×684 px. At **16 px** it shows 72.6 × 42.75 tiles: 60% of the width, 43% of the height, **≈26% of the 120×100 city**. Master's 12 px default showed 96.8 × 57 tiles (≈46%), and master could zoom out to 10 px (the cover limit), showing ≈66%. 16 px is now the furthest-out zoom, so you can never see more than about a quarter of the city. The minimap works for navigation, but the overview is gone. |
| 4 | PD/FD legible at 1x | **PASS.** The letters now keep a 1 px gap inside the rim; the new `tool_icons` test enforces this, and I confirmed it by pixel diff. The letters were redrawn small: PD is 9×7 px, and FD is 9×6 px on a dark red disc (master: 22×14 over the rim). At 1x both read clearly, and the shield/cross silhouettes carry the meaning (`shots/pdfd-1x-3x.png`: master vs PR at 1x and 3x). Nit: FD is quite small for a 7-inch Deck screen. |

## Notes (not blockers)

1. **Overview lost:** see 3f. Options:
   * An 8 px "overview" step using a 2:1 box-filtered or hand-made half atlas (still pixel-exact).
   * Or allow zooming out further at large windows only.
2. **Trigger zoom is now very fast when held.** The 180 ms trigger repeat is unchanged, but each step is now ×2/×1.5 instead of +2 px. Holding RT for about 0.5 s goes straight from 16 to 64 (`zoom-pad-1.png`). Consider a longer repeat delay (for example 350 ms) or one step per press.
3. **Pinch is coarse.** An 8% finger-distance change now jumps a whole 16 px multiple (16 → 32 doubles the size). It works, but it feels snappy rather than smooth.
4. **The `review_ui` and `argv_open` tests depend on the environment.** They fail if `SteamDeck`/`LUNDUKE_CITY2_SIZE` are set or no display is present. That is not a PR issue: they pass 28/0/1 in a clean environment (HOME=scratch/thome, DISPLAY=Xvfb). Worth hardening by unsetting these variables inside the test definitions.
5. **Pre-existing, not PR #9:**
   * The touch two-finger gesture still builds under the first finger: funds dropped during the pinch tests, as REVIEW G1 predicts.
   * The hover outline stays on the map after the pointer leaves it.

## Cleanup
* I killed by exact PID: the app instances (752611, 757879, 764379, 764990), Xvfb :973 (749830), the dbus-launch/daemon the app autolaunched (751866/751867), and the Windows script's gpg-agent (757723).
* I ran `wineserver -k` for this repo's prefix.
* No platform Chrome was attached to :973.
* `git status` is clean. No merge, push, comment or stash.

---

# v2: re-verification at head `4d3c7ba` (2026-10-21, ~7:15 PM CDT)

Fresh clone: `/workspace/city2-pr9/repo-v2`, branch `pr9` = `4d3c7ba` (`git status` clean). **Not merged, no commits, pushes or comments.**
Clean env: `env -u SteamDeck -u LUNDUKE_CITY2_SIZE -u LUNDUKE_CITY2_SCALE`, HOME=`scratch-v2/thome`, Xvfb 1280x800. Logs: `logs-v2/`. Shots: `shots-v2/`.

| # | Item | Result |
|---|---|---|
| 1a | Linux build + tests | **PASS.** 0 warnings, 28 OK / 0 Fail / 1 Skip (round11, as before). |
| 1b | ASan/UBSan build + tests | **PASS.** 0 warnings, 28 / 0 / 1. |
| 1c | Windows cross build + Wine | **PASS.** EXIT=0, 0 warnings, Wine 23 OK / 0 Fail / 1 Skip. |
| 1d | Test env hardening | **PASS.** With `SteamDeck=1 LUNDUKE_CITY2_SIZE=1920x1080` exported, the suite still passes 28/0/1, including `argv_open` and `review_ui` (`logs-v2/test-x-dirtyenv.log`). |
| 1e | Tile atlas unchanged | **PASS.** `git diff origin/master..HEAD -- third_party data/special-tiles` is empty. `tiles.png` sha256 is e80506a7… in master, PR and the Windows zip. The only changed art is `tool-icons/police.png` and `fire.png`. |
| 2a | 8px is crisp-ish and readable | **PASS.** Every on-screen 8px cell (9,909 of 9,912; the 3 misses are under the scroll edge) is a byte-exact copy of the 2×2 box-averaged atlas tile, drawn 1:1 with no scaling blur. R/C/I letters, FD/PD badges, roads, rail and water stay readable; road dashes are softened but visible (`zoom-8.png`, `zoom-8-crop3x.png`). |
| 2b | Whole city width at 1280x800 | **PASS.** 120 × 8 = 960 px inside a 1162 px view: the whole width is visible, centred with ~101 px letterbox on each side, and ~85 of 100 rows. Normal (`zoom-8.png`) and Deck (`deck-zoom-8.png`). |
| 2c | Reachable by every zoom input | **PASS.** The zoom-level detector confirmed each step from the pixels. Ctrl+wheel: 64→48→32→16→8→8, then 8→16→32. Ctrl+- / Ctrl+=: 16→8→8, then 8→16→32→48→64→64. LT/RT: down to 8 and back up. Pinch: 32→16→8 in one gesture. The menu Zoom In/Out actions use the same `zoom_by`. Default is 16 in normal and Deck (`deck1280-default.png`). |
| 2d | Clicks and builds land on the right tile at 8px | **PASS.** Mouse road clicks 1 px inside a cell's top-left and 2 px inside another cell's bottom-right built cells (100,60), (103,60) and (101,62), exactly the targeted ones (`z8-click-cmp.png`). Gamepad A built the wire in the 8px cell under the crosshair (`pad8-build-cmp.png`). The crosshair centre sits on a cell edge at this size (see note 2). |
| 2e | Minimap rectangle | **PASS.** At 8px the rectangle spans the full minimap width and ~85% of its height (`mm-8.png`). At 16 it is ~60% wide. |
| 3a | Trigger: one step per press | **PASS.** Holding LT for 2 s: 16→8 only. Holding RT for 2 s: 8→16 only. Hysteresis: press to 1.0, fall to 0.5, press to 1.0 gives no new step; falling to 0.3 and pressing again gives one step. Separate presses step 32→48→64, and 64 caps. |
| 3b | Pinch thresholds | **PASS.** Starting from a 0.20 finger spread: at 1.30× nothing happens; at 1.425× it steps 16→32 and re-baselines; at +23% after that, nothing; at +44%, 32→48. Pinching in to 0.733× does nothing; 0.667× steps 48→32. |
| 4 | PD/FD at 1x and 3x | **PASS (readable), with a cosmetic note on FD.** See the assessment below and `toolbar-pdfd-1x-3x.png`: the real toolbar at scale 1, a 3× nearest-neighbour zoom of it, and a real UI-scale-3 render. |

### FD assessment (honest)
- **Nothing sticks out past the cross outline.** The white letters span x 6–32, y 15–23 of the 40×40 icon. That keeps a 1 px red margin inside the gold rim at both ends of the horizontal arm. No white or light pixel reaches the rim or the background.
- **But the letters fill almost the whole horizontal arm.** They are 27 px wide on a 29 px arm interior, more than twice the width of the vertical arm. So the horizontal arm reads as a white band, not as letters on a red cross. At true 1:1 the icon looks like a red cross with a white stripe through it, and "FD" only resolves when you look closely. At 3x it reads clearly as FD.
- **The tops of F and D nearly merge.** Their top bars are separated by only a 1 px dark-red pixel, so the top edge looks like one continuous white bar. This adds to the "white plate" look.
- **The dark-red shading looks like smudges.** The darker red inside the letter counters looks muddy at 3x.
- **Overall:** not broken and legible, but slightly odd and heavier than PD. PD's letters sit inside the shield with clear blue around them and look clean.
- **Possible fix:** narrow FD to about 21 px so a red margin shows at the arm ends, widen the F–D gap to 2 px, and drop the dark shading behind the letters.

### New issues (not blockers)
1. **Low/Medium: zooming in from the 8px overview jumps off-centre to the right.** `zoom_by` computes the centre fraction as `(scroll_x + view_w/2) / map_w`. At 8px the map is letterboxed: scroll_x = 0 and view_w (1162) > map_w (960). That fraction comes out as 0.605 instead of the visible 0.5. Zooming back to 16 therefore lands about 13 tiles right of where you were looking; the minimap rectangle jumps to the right side (`mm-8to16.png`). The same happens vertically on windows tall enough to letterbox at 8px. **Fix:** subtract the letterbox, or use the visible centre `(view_w/2 - map_ox_ + scroll_x) / map_w`, in `zoom_by`.
2. **Low:** at 8px the 20 px gamepad crosshair covers about 3×3 tiles, and its centre falls on a tile edge. There's no target-cell outline before pressing A, so it is hard to tell which tile will be built. Consider outlining the target cell at small zooms.
3. **Low:** at 8px the gamepad cursor can rest on the grey letterbox beside the map. Pressing A there does nothing, which is harmless.
4. **Not from this PR:** a first-finger touch still runs the tool immediately (Query popped up when a pinch started), which is G1 from REVIEW. The hover outline lingers, as before.

### Screenshots (`/workspace/city2-pr9/shots-v2/`)
- `zoom-8.png`: 1280x800 normal, 8px.
- `deck-zoom-8.png`: Deck, 8px.
- `deck1280-default.png`: Deck default, 16px.
- `toolbar-pdfd-1x-3x.png`: crop of the real toolbar.
- Supporting: `zoom-8-crop3x.png`, `z8-click-cmp.png`, `pad8-build-cmp.png`, `mm-8.png`, `mm-8to16.png`, `kz-*`, `wz-*`, `dz-*`, `pz-*`, `pz8-*`.

### Cleanup
- Killed by exact PID only: the apps (820648, 820711, 833717), Xvfb :976/:977/:978 (819292, 819294, 833700), their autolaunched dbus-launch/dbus-daemon (819518/9, 820717/8, 833723/4), and this build's gpg-agent (819331).
- No wineserver was left running.

---

# v3: final check at head `7bf171d` (2026-10-21, ~7:50 PM CDT)

Fresh clone `/workspace/city2-pr9/repo-v3`, branch `pr9` = `7bf171d` (`git status` clean). **Not merged, no commits, pushes or comments.** Clean env (`env -u SteamDeck -u LUNDUKE_CITY2_SIZE -u LUNDUKE_CITY2_SCALE`, HOME=`scratch-v3/thome`). Logs: `logs-v3/`. Shots: `shots-v3/`.
How tiles were located: the whole 120×100 world was read tile by tile from an 8px shot in a 1280x1000 window (`scratch-v3/world.npy`). Each later screenshot was matched against it (97–100% of cells) to find the exact world tile under a given window pixel (`scratch-v3/loc.py`).

| # | Item | Result |
|---|---|---|
| 1 | Linux / ASan / Windows+Wine, 0 warnings; atlas identical | **PASS.**<br>• Linux: 0 warnings, 28 OK / 0 Fail / 1 Skip.<br>• ASan/UBSan: 0 warnings, 28 / 0 / 1.<br>• Windows: EXIT=0, 0 warnings, Wine 23 / 0 / 1.<br>• `tiles.png` sha256 e80506a7… in master, PR and the Windows zip. `git diff origin/master..HEAD -- third_party data/special-tiles` is empty. Only `tool-icons/fire.png` and `police.png` changed, and the zip copy of fire.png matches `data/`. |
| 2 | Zoom drift fixed (8px → 16px), 1280x800 | **PASS.** World tile at the anchor before → after (no jump):<br>• Keyboard Ctrl+= at view centre: (60,50) → (60,50).<br>• RT trigger at view centre: (60,45) → (60,45).<br>• Ctrl+wheel at the pointer (400,300): (23,40) → (23,40).<br>• Pinch with its centre at (896,480): (85,57) → (85,57). |
| 2 | Same, taller 1280x1000 window (vertical grey band at 8px) | **PASS.**<br>• Keyboard at centre: (60,50) → (60,50).<br>• RT at centre: (60,50) → (60,50).<br>• Ctrl+wheel at (400,300): (23,27) → (23,27) at 16, and still (23,27) at 32.<br>• Pinch at (896,600): (85,65) → (85,65).<br>Minimap check: after the keyboard zoom 8→16 the rectangle is centred (x 28–87 of ~6–110). |
| 3 | Gamepad at 8px (Deck, 1280x800) | **PASS.**<br>• Marker sits on the tile centre (centre 1130.5,581.5 = tile 1127–1134 + 3.5). The target tile has a 1 px black/white outline before A (`deck-zoom-8-cursor.png`).<br>• Can't rest on the grey band. Moving right from the toolbar lands on the first map column (218.5). Small left nudges step 242 → 218 → toolbar (108); the cursor never stops in the band (x≈118–214).<br>• Pushing outward reaches the toolbar, where the normal 20 px crosshair returns. A there selects tools (Seaport, then Roads).<br>• A builds on the outlined tile: road tile 66 appears exactly in that cell, Funds −$10, and nothing else on the map changes (`pad8-build-cmp.png`). |
| 4 | Regression: steps, click accuracy, minimap | **PASS.**<br>• Steps 8/16/32/48/64 were confirmed from the pixels.<br>• Mouse road clicks (1 px inside one dirt cell's top-left and 2 px inside another's bottom-right) changed exactly the targeted cells at 8, 16, 32, 48 and 64 (`zt-N-b/a.png`). The only extra changes were road auto-connect/tree-edge neighbours, a looping map animation, and the old lingering hover outline.<br>• Minimap rectangle, w×h in px: 102×71 / 60×35 / 30×16 / 19×10 / 13×7 at 8/16/32/48/64, against ≈104×74 / 63×37 / 31×18 / 21×12 / 16×10 expected (minimap ≈104×87, minus the 2 px frame). |
| 5 | FD legible at 1x | **PASS.** FD was redrawn: letters span x 9–29 (21 px), with a 2 px gap between F and D, a 4–5 px red margin to the gold rim at both ends, and no dark shading inside. On the real 1x toolbar it now reads as "FD" on a red cross, not a white stripe (`toolbar-pdfd-1x-3x.png`: 1:1, 3× nearest zoom, and a real UI-scale-3 render). |

Notes (not blockers):
- At 8px the outline plus marker cover nearly all of the 8 px target tile, so what is on it, or what was just built, is hard to see until the cursor moves.
- The hover outline still lingers where the mouse last was over the map (pre-existing).
- When a pinch starts, the first finger still applies the tool (review item G1, pre-existing).

Screenshots: `shots-v3/normal1280-default.png` (16px default), `zoom-8.png`, `deck-zoom-8-cursor.png`, `toolbar-pdfd-1x-3x.png`. Supporting: `t-*.png`, `n-*.png`, `d-*.png`, `zt-*.png`, `pad8-build-cmp.png`.

Cleanup: killed by exact PID only. That covers the apps (842290, 842700, 845631, 852755), Xvfb :976/:977/:978 (838426, 838428, 852729), their dbus-launch/dbus-daemon (841279/80, 842706/7, 852761/2), and this build's gpg-agent (838587).

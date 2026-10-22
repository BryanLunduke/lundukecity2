# SVGA look — sample only

Approval sample for a future Options-menu choice, **Classic Look** / **SVGA Look**.
Classic stays the tiles that ship now. This folder is the SVGA trial for two
buildings. It does not add menu items, and it does not change the game.

Do not merge this branch.

The recoloured pixels are derived from the vendored Micropolis atlas
`third_party/micropolis-assets/images/micropolisEngine/tiles.png`
(GPL-3.0-or-later). That file is unchanged.

## What was recoloured

| Building | Atlas indices | Layout |
| --- | --- | --- |
| Police station | 770–778 | 3×3, row-major. No animation and no powered/unpowered swap. The simulator only repairs the zone. |
| Airport | 709–744 | 6×6, row-major. Tile 711 is the static radar dish. |
| Radar cycle | 832–839 | `RADAR0`–`RADAR7`. The powered airport draws these in cell (2, 0). |

53 tiles, each still 16×16. PNGs are under `tiles/`.

## Palette

`palette/vga-mode13h.gpl` is the default VGA Mode 13h palette: 16 EGA colours,
16 greys, 24 hues × 3 saturations × 3 values, and 8 unused black entries.
`palette/vga-mode13h-swatch.png` shows it as a 16×16 grid.

DAC levels are 0–63. The 8-bit values in the file and in the PNGs are those
levels expanded by bit replication, `(v << 2) | (v >> 6)`. A pixel is legal
when its RGB triple equals one of the 256 rows. Index 0 is black and is the
only black used.

## How a tile is repainted

The stock tiles are flat colours. The recolour keeps every pixel in its
original colour region and only changes which VGA entry that region uses.
Hues stay with the classic tile: blue walls stay blue, the yellow sign stays
yellow, grey roofs stay neutral grey. The aim is a richer 16-bit pass of the
same city, not a new scheme.

- Original black stays index 0, and nothing else becomes black. Outlines,
  window holes, and the tile frame stay on the same pixels.
- Each other source colour gets a light-to-dark ramp. Ramps for colours that
  touch are disjoint, so a boundary cannot disappear.
- Light comes from the upper left. A roof or wall is shaded inside its own
  connected region, so each building gets its own gradient. On a four-step
  ramp the face uses the middle two steps, the lightest entry is only the
  north/west highlight, and the darkest is only the south/east edge. A
  three-step ramp gradients across the first two and keeps the last for that
  edge. A pixel with fewer than two same-colour neighbours skips the edge
  step, so a dither pattern does not strobe.
- Ground south or east of a building takes a 2px shadow, then a 1px softer
  step. The shadow is a darker entry of that ground's own ramp, never black.
  Only a solid region (8 pixels or more) casts, so a 1px dither dot does not.
  A black outline with a building pixel beside it does cast, which lets the
  shadow cross the stroke. Grass is also one step darker where it simply
  touches pavement or a building.
- Dirt and the police lawn are a dense dither of a few close shades,
  in 2px clusters with a ragged edge so the pattern is not a 16px grid.
  Sheets paint the lot in scene coordinates, and building tiles are painted
  as one assembly before slicing. Dirt is a warm mid-brown: dominant
  `(113, 97, 56)`, with close neighbours `(113, 113, 56)` and `(113, 85, 56)`.
  The darker neighbour is also the rim where the lot meets the soil marks.
  `(170, 85, 0)` is not used. The lawn is deep
  green: `(0, 170, 0)`, `(56, 113, 0)`, `(28, 113, 0)` and `(0, 113, 0)`,
  with a darker green where it meets pavement or a building. The neon
  `(85, 255, 85)` is not used. The cast shadow is a darker step of that same
  ground.
- Asphalt has no grain. A pixel whose north or west neighbour is dirt steps
  one grey lighter, and one whose south or east neighbour is dirt steps one
  grey darker. A thin strip with dirt on both sides stays the body colour.
  White and yellow paint are not an edge, so the dashes and runway marks
  stay crisp. Classic black lines stay the dark stroke.
- Marks that have to stay crisp are a single entry: runway yellow, the
  emergency lights, white panes, the orange detail, the small police
  highlight, and the sign's letter strokes (dark grey connected to the yellow
  sign, index 18).

There is no water in these tiles. The blues are roof, wall, and glass.

The same source grey is a different material on each building: slate and
concrete on the police station, asphalt and hangar walls on the airport.
A full-set pass should assign ramps per material, per building, with the
same disjoint-ramp rule.

## Ramps

Indexes match `palette/vga-mode13h.gpl`. `ramps-used.txt` is the same list,
written by the generator. Entries are light to dark. Where a ramp has a
shadow-only tail, the speckle uses the earlier entries and the cast shadow
uses the last.

**Police**

- Lawn `(0, 230, 0)`: EGA green 2 `(0, 170, 0)`, then 118, 119, 120, and 194, 193 for the shadow. Deep greens. The neon `(85, 255, 85)` is not used.
- Hedge `(0, 127, 0)`: 240, 216, 191, 192. Darker than the lawn, so the shrub row stays a separate band.
- Roof and trim `(102, 102, 230)`: 56, EGA blue 9 `(85, 85, 255)`, 128. Red stays equal to green.
- Walls `(0, 0, 230)`: 32 `(0, 0, 255)`, EGA blue 1 `(0, 0, 170)`, 104.
- Concrete roof, forecourt, driveway `(127, 127, 127)`: grey indexes 27–24. The face sits on 26 and 25, around the classic 127.
- Left wing `(63, 63, 63)`: grey indexes 22–20. Sign letters that touch the yellow are ink 18 instead.
- Sign `(255, 255, 0)`: 68, EGA yellow 14, pure yellow 44.
- Small light-grey highlight: grey 29. White panes: 31. Red lights: 40.

**Airport**

- Infield and clear land `(204, 127, 102)`: 140 `(113, 113, 56)`, 139 `(113, 97, 56)`, 138 `(113, 85, 56)`, and 137, 136 for the shadow. Index 139 is the base. Index 138 is also the 1px rim against the soil marks. The grey pavement reads against this brown without an orange edge.
- Darker soil marks `(153, 127, 76)`: 115, flat, so the marks stay a step off the speckled lot.
- Runways and aprons `(63, 63, 63)`: grey indexes 21, 20, 19. Index 20 is the body. 21 and 19 are only the dirt-side edges.
- Hangar walls `(127, 127, 127)`: grey indexes 27–24, same shape of ramp as the police concrete.
- Hangar roofs `(191, 191, 191)`: grey 30, 29, 28, and EGA 7 `(170, 170, 170)`. Neutral grey. The face is 29 and 28, around the classic 191. White panes stay index 31, off this ramp.
- Glass `(102, 102, 230)`: 56, 9, 128.
- Deep blue `(0, 0, 230)`: 32, EGA blue 1, 104.
- Runway paint: yellow 44, held flat. White 31. Red 40. Orange 42. The few dark-green pixels: 192.

Radar frames are painted on the 6×6 and then cropped. A pixel the frame shares with the static cell keeps the static colour, so the dish meets the neighbouring tiles.

## Sheets

Classic is on the left, SVGA on the right. Scaling is nearest-neighbour.

Comparisons sit the building on one tile of clear land and a horizontal road (tile 66) along the south edge:

- `comparisons/police-1x.png`
- `comparisons/police-3x.png`
- `comparisons/police-6x.png`
- `comparisons/airport-1x.png`
- `comparisons/airport-3x.png`
- `comparisons/airport-6x.png`
- `comparisons/police-airport-3x.png` — both buildings, Classic and SVGA, on one sheet at 3×

The airport comparison uses radar frame 834 so the dish is in the picture.
`comparisons/radar-3x.png` and `comparisons/radar-6x.png` show all eight frames.

Mock-ups are a wider lot with the same road, at 16px per tile and at 32px per tile. The airport mock-up uses radar frame 833:

- `mockups/police-classic-vs-svga-16px.png`
- `mockups/police-classic-vs-svga-32px.png`
- `mockups/airport-classic-vs-svga-16px.png`
- `mockups/airport-classic-vs-svga-32px.png`

Clear land and the road in those sheets use the airport dirt and tarmac ramps so the building meets the lot. The ground is shaded in scene coordinates rather than by stamping one recoloured tile. Those two tiles are not part of the delivered set.

## Check

```bash
python3 art/svga-sample/make_svga_sample.py
python3 art/svga-sample/verify_svga.py
```

`verify_svga.py` reads the GPL and the PNGs. A tile passes when it is 16×16,
every pixel is a palette entry, the black ink mask matches, and every classic
colour boundary is still a colour change. The assembled 3×3 and 6×6 are
checked the same way, so a seam between tiles is included. The last run is
`verify-report.txt`: 53 tiles, 0 palette violations, 0 ink mismatches,
0 collapsed boundaries. Strong classic edges (luma delta ≥ 80) still read at
half that delta on 2092 of 2496 pairs. The lot is darker than the classic tan, so some of those edges are softer; the pass rule is that the two sides stay different colours.

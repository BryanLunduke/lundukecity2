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

- Original black stays index 0, and nothing else becomes black. Outlines,
  window holes, and the tile frame stay on the same pixels.
- Each other source colour gets a light-to-dark ramp. Ramps for colours that
  touch are disjoint, so a boundary cannot disappear.
- Light comes from the upper left, the same way SNES SimCity shades a tile.
  The face of a surface uses the light end of its ramp (`body_last` in
  `make_svga_sample.py`). The south and east edges step one entry darker.
  The north and west edges step one entry lighter.
- A receiver pixel (lawn, hedge, concrete apron, dirt, soil, tarmac) that
  sits immediately south or east of a building colour, or of an interior
  black outline with a building colour beyond it, steps one entry darker.
  That is the 1px contact shadow. The outer two pixels of the assembled
  building are the frame and do not cast.
- Lawn, hedge, dirt, and tarmac also take a scrambled ±1 step inside the
  body of the ramp, so the texture is grain rather than a flat fill. Dirt
  additionally gets a sparse darker speck (about one pixel in 64).
- Marks that have to stay crisp are a single entry: runway yellow, the
  emergency lights, white panes, the orange detail, and the police sign's
  letter strokes (dark grey connected to the yellow sign, index 18).

There is no water in these tiles. The blues are roof, wall, and glass.

The same source grey is a different material on each building: slate and
concrete on the police station, asphalt and hangar walls on the airport.
A full-set pass should assign ramps per material, per building, with the
same disjoint-ramp rule.

## Ramps

Indexes match `palette/vga-mode13h.gpl`. `ramps-used.txt` is the same list,
written by the generator. Entries are light to dark.

**Police**

- Lawn `(0, 230, 0)`: 10, 47, 48, then EGA green 2 `(0, 170, 0)` for the shadow step.
- Hedge `(0, 127, 0)`: 240, 216, 191, 192. Darker than the lawn, so the shrub row stays a separate band.
- Roof and trim `(102, 102, 230)`: 80, 56. Pale blue, equal red and green.
- Walls `(0, 0, 230)`: EGA blue 1 `(0, 0, 170)`, then 104 for the shadow edge.
- Concrete roof, forecourt, driveway `(127, 127, 127)`: grey indexes 28–24.
- Left wing `(63, 63, 63)`: grey indexes 23–20. Sign letters that touch the yellow are ink 18 instead.
- Sign `(255, 255, 0)`: 68, then pure yellow 44.
- Small light-grey highlight: grey 30. White panes: 31. Red lights: 40.

**Airport**

- Infield and clear land `(204, 127, 102)`: 90, 66, 65, plus speck 137. The VGA wheel has no brown between luma about 110 and about 180, so the field sits in the light earth cycle and the asphalt stays in the dark greys. That keeps the runway edge.
- Darker soil marks `(153, 127, 76)`: 138, flat.
- Runways and aprons `(63, 63, 63)`: grey indexes 23–19.
- Hangar walls `(127, 127, 127)`: grey indexes 29–25.
- Hangar roofs `(191, 191, 191)`: 103, 81, 80, 79. A cool highlight, off the neutral wall ramp.
- Glass `(102, 102, 230)`: 57, 56, 152, 128.
- Deep blue `(0, 0, 230)`: 129, 200, 104.
- Runway paint: yellow 44, held flat so the marks do not pick up a gradient. White 31. Red 40. Orange 42. The few dark-green pixels: 192.

Radar frames are painted in place on the 6×6, then cropped, so a background pixel that did not move matches the static cell.

## Sheets

Classic is on the left, SVGA on the right. Scaling is nearest-neighbour.

Comparisons sit the building on one tile of clear land and a horizontal road (tile 66) along the south edge:

- `comparisons/police-1x.png`
- `comparisons/police-3x.png`
- `comparisons/police-6x.png`
- `comparisons/airport-1x.png`
- `comparisons/airport-3x.png`
- `comparisons/airport-6x.png`

The airport comparison uses radar frame 834 so the dish is in the picture.
`comparisons/radar-3x.png` and `comparisons/radar-6x.png` show all eight frames.

Mock-ups are a wider lot with the same road, at 16px per tile and at 32px per tile. The airport mock-up uses radar frame 833:

- `mockups/police-classic-vs-svga-16px.png`
- `mockups/police-classic-vs-svga-32px.png`
- `mockups/airport-classic-vs-svga-16px.png`
- `mockups/airport-classic-vs-svga-32px.png`

Clear land and the road in those sheets use the airport dirt and tarmac ramps so the building meets the lot. Those two tiles are not part of the delivered set.

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
half that delta on 2492 of 2496 pairs.

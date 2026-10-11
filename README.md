# wang tilegrid generator

Builds edge-matched (and corner-matched) Wang tile grids, turns **your own tilesets** into big seamless
textures, and extrudes the grid into a themed 3D maze you can walk around in a browser.

## Requirements
1. A C compiler (gcc/clang) and `make`
2. GTK 3 + gdk-pixbuf dev packages (GUI only)
3. `stb_image.h`, `stb_image_write.h` (included)
4. Python 3 (tests and a local web server for the 3D viewer; Pillow is optional, for a few extra tests)

## Build
```
make cli     # builds ./wang_cli
make gui     # builds ./image_resizer (needs GTK 3 and pkg-config)
make test    # runs the checks in tests/
```

## Quick tour
```
./wang_cli --size 20x12 --seed 42 --out grid.png                      # a Wang grid image
./wang_cli --tileset assets/tilesets-with-no-predefined-order/brench.png --size 20x12 --out grid.png
./wang_cli --make-texture clouds --texture-out clouds.png             # a seamless texture from a tileset
./wang_cli --theme dungeon --size 30x20 --ceiling --obj world.obj --out grid.png
```
`--out` defaults to `grid.png` in the current directory: pass it explicitly if you do not want that file overwritten.

## Tilesets: how they are read
A Wang tileset is 16 tiles (two colours on four edges, or at four corners: 2^4 combinations). Give `--tileset` a
**sheet image** (a 4x4 grid of square tiles, any tile size; 8x2, 2x8, 16x1 and 1x16 also work) or a **folder of
16 images** (png, gif, jpg, bmp, tga). You do not have to put the tiles in any particular order: the order is
worked out from the pixels.

* **Edge or corner.** Edge sets (paths, mazes, pipes) must match on the colour of shared edges; corner sets
  (terrain, clouds, grass/sand, wallpaper patches) on the colour of shared corners. Both are detected.
* **Order.** For edge sets, the tile id is read off from which of two colours each side has. The result is only
  accepted if all 16 tiles come out different (a wrong guess passes that about once in a million), a few noisy
  strips (JPEG) are repaired, and among the candidates the one whose tiles actually fit together best is kept.
  Your tilesets in `assets/`: the 6 numbered folders are read exactly like their file numbers, the 256px
  sheets are recognised as already being in this project's order, and the three "no predefined order" sheets
  decode to the standard arranged 4x4 layout.
* **Confidence.** `--tileset-info` prints how much more alike touching sides are than others (1 = chance). Below
  1.4 the tileset is refused rather than producing a scrambled texture.
* **Colour naming.** Swapping the two colours gives an equally valid set, so for a set that looks the same both
  ways (e.g. `groove`, where tile 0 looks like tile 15) the naming is arbitrary. Otherwise the emptiest tile is
  taken as tile 0. If it comes out the wrong way round, or detection fails, say how the tiles are numbered:

| Option | Meaning |
| --- | --- |
| `--tileset-order auto` | (default) work it out from the pixels |
| `--tileset-order stagecast` | numbered as on the Stagecast pages: edges N=1 E=2 S=4 W=8, corners NE=1 SE=2 SW=4 NW=8 (a folder of files `0..15` is placed by file number) |
| `--tileset-order project` | this project's edge order (right=1 top=2 left=4 bottom=8), as in `reference.png` |
| `--tileset-matching auto\|edge\|corner` | force edge or corner matching |
| `--tileset-info` | print how the tileset was read and exit |
| `--make-tileset S --tileset-out F` | write a built-in or your own tileset as a standard 4x4 sheet (tile id t at column t%4, row t/4): a starting point for drawing your own |

An edge tileset given with `--tileset` becomes the floor of the maze (and of the 2D grid). A corner tileset
textures the floor by itself (the maze is made of edges), so you get terrain-like patches under the walls.
`--atlas FILE` still works: a 256x256 sheet in this project's order.

## Textures from tilesets
Wang tiles were made for this: a small set of tiles assembles into a large image that does not visibly repeat.
`--make-texture` does a random Wang tiling that also matches across its own border, so the result tiles
seamlessly:

```
./wang_cli --make-texture clouds --texture-out clouds.png --texture-cells 8
./wang_cli --make-texture assets/maze2 --texture-out my_wall.png          # from your own tileset
```
`S` is a built-in name or the path of your tileset. Built-in textures are corner-matched sets generated in C:

| name | A / B | name | A / B |
| --- | --- | --- | --- |
| `clouds` | sky / cloud | `plaster` | plaster / brick |
| `sunset` | dusk sky / dark cloud | `stripes` | cream / striped wallpaper |
| `grass` | dirt / grass | `damask` | crimson / gold damask wallpaper |
| `shore` | sand / water | `wood` | oak / walnut planks |
| `lava` | basalt / lava | `marble` | white / black marble |
| `snow` | rock / snow | `ice` | frost / glacier ice |
| `moss` | stone slabs / moss | | |

`--list-textures` prints them. The same specs texture a 3D world: `--wall-texture`, `--ceiling-texture`,
`--floor-texture` (replaces the maze-path floor by one continuous texture) and `--sky-texture` (clouds drift
across a sky dome in the viewer). `--texture-cells N` sets how many tiles one repeat covers (default 8).

## 3D worlds
`--obj world.obj` extrudes the grid: a set edge bit is an open passage, a clear bit a wall. Neighbours agree on
shared edges, so every wall is emitted exactly once and the whole grid is one consistent world.

```
./wang_cli --size 30x20 --seed 5 --out grid.png --obj world.obj
./wang_cli --size 30x20 --seed 5 --out grid.png --obj world.obj --terrain   # floors rise with open edges
```
The export writes `world.obj`, `world.mtl`, `world_atlas.png` (the floor tiles), `world.json` (sky, fog, lights,
start and goal for the viewer) and, depending on the options, `world_wall.png`, `_ceiling.png`, `_floor.png`
and `_sky.png`. Keep them together. Each floor cell uses its own atlas tile, so the export works for any grid size.

### Themes
`--theme NAME` gives a look, with generated textures, props and lights:

| theme | walls | floor | extras |
| --- | --- | --- | --- |
| `dungeon` | brick | flagstones, red carpet along the open edges | pillars, crates, rocks, torches |
| `hedge` | leafy hedge | grass, dirt paths | trees, bushes, lanterns, ponds |
| `scifi` | metal panels | dark deck, glowing circuit lines | pylons, crates, light strips |
| `desert` | sandstone | rippled sand, paved paths | cacti, rocks, torches, oases |
| `ice` | frosted ice | snow, glossy ice paths | crystals that glow, snow mounds |

Other options: `--ceiling`, `--props PCT`, `--water PCT`, `--lights PCT`, `--markers` / `--no-markers`
(a green start pad and a gold goal beacon at the two ends of the longest route), `--wall-height F` (default
0.6), `--wall-thickness F` (default 0.1). The same `--seed` always gives the same world. `--wall-texture` and
friends override a theme's own textures.

### Viewer
```
python3 -m http.server 8000
# open http://localhost:8000/viewer/index.html   (loads ../world.obj; or ?obj=path/to/file.obj)
```
Drag to orbit, scroll to zoom. Untick "Show walls" (H) to see the floor pattern, "Show props & lights" (P), and
"Show ceiling in orbit view" (C). W A S D or the arrow keys move in both modes. "Walk inside the maze" starts at
the start pad facing the goal (drag to look, or move the mouse when the browser grants pointer lock; Esc to
leave); the ceiling is always shown while walking. The nearest torches, crystals and pylons light the scene.
There is no wall collision yet.

This is a 2.5D extrusion of 2D Wang tiles, not true Wang cubes (matching on all six faces of a 3D lattice).

## Other options
| Option | Meaning |
| --- | --- |
| `--procedural blobs\|roads` | generate a plain atlas instead |
| `--size WxH` | grid size in tiles (default 10x10, at most 1024 per side) |
| `--seed N` | RNG seed. The same seed gives the same grid. Default is the current time and is printed |
| `--out FILE` | grid image (default `grid.png`); at most 16384 tiles, e.g. 128x128 |
| `--atlas-out FILE` | also save the atlas that was used |
| `--tiles-out FILE` | dump tile ids as hex digits, one grid row per line |

Edge tile ids use bits `1=right, 2=top, 4=left, 8=bottom`; neighbouring tiles always carry the same bit on the
edge they share. Floor atlas priority: `--tileset`, then `--atlas`, then `--procedural`, then the theme's own.

## GUI
`./image_resizer`: "Open Image" takes any edge-matched tileset sheet (its order is worked out from the pixels),
then set width, height and an optional seed, pick a theme, and press "Create Wang Grid". Empty fields mean 10x10
and a random seed; without an opened image the theme's (or the built-in) atlas is used. "Export 3D (.obj)" writes
the world, with the "Ceiling" box and the chosen theme. Corner tilesets and the texture options are command-line only.

## Demo
### Input Grid
![pipes](https://github.com/user-attachments/assets/f83f2404-2973-4904-97d0-e130c2dfc11c)

### Output Grid
![photo_2025-02-08_11-11-14](https://github.com/user-attachments/assets/4003f1f4-be21-4189-9126-bf7fd69fea6c)

## References
* Stagecast Wang tiles pages (Guy Walker): https://www.boristhebrave.com/permanent/24/06/cr31/stagecast/wang/intro.html
  (a mirror; original https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html).
  Source of the 16-tile numbering, the edge/corner distinction (edge sets suit paths and mazes, corner sets suit
  patches and terrain) and most of the tilesets in `assets/`.
* Cohen, Shade, Hiller, Deussen, "Wang Tiles for Image and Texture Generation" (2003): non-repeating textures from
  a small tile set by random tiling. https://graphics.uni-konstanz.de/publikationen/Cohen2003WangTilesImage/index.html
* https://en.wikipedia.org/wiki/Wang_tile
* https://tessellation.space/explore/

## Assets
https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html

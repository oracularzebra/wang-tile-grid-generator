# wang tilegrid generator

Generates edge-matched Wang tile grids from a 4x4 tile atlas, and can extrude the
same grid into a 3D maze/terrain you can walk around in a browser.

## Requirements
1. A C compiler (gcc/clang) and `make`
2. GTK 3 + gdk-pixbuf dev packages (GUI only)
3. `stb_image.h`, `stb_image_write.h` (included)
4. Python 3 (tests and a local web server for the 3D viewer)

## Build
```
make cli     # builds ./wang_cli
make gui     # builds ./image_resizer (needs GTK 3 and pkg-config)
make test    # runs the checks in tests/
```

## CLI
```
./wang_cli --size 20x12 --seed 42 --out grid.png
./wang_cli --atlas reference.png --size 30x30 --out grid.png
./wang_cli --procedural roads --size 8x8 --atlas-out atlas.png
```
| Option | Meaning |
| --- | --- |
| `--atlas FILE` | 256x256 atlas image, 4x4 tiles of 64px in the order shown in `reference.png` |
| `--procedural blobs\|roads` | generate the atlas instead (default `blobs`) |
| `--size WxH` | grid size in tiles (default 10x10, at most 1024 per side) |
| `--seed N` | RNG seed. The same seed gives the same grid. Default is the current time and is printed |
| `--out FILE` | grid image (default `grid.png`); at most 16384 tiles, e.g. 128x128 |
| `--tiles-out FILE` | dump tile ids as hex digits, one grid row per line |
| `--obj FILE` | export a 3D world (see below) |

Tile ids use bits `1=right, 2=top, 4=left, 8=bottom`; neighbouring tiles always carry the same bit on the edge they share.

## GUI
`./image_resizer`, then open an atlas image (it is resized to 256x256; the tile order must follow
`reference.png`), set the width, height and an optional seed, and press "Create Wang Grid".
Leaving the fields empty uses 10x10 and a random seed, and without an opened image a built-in atlas is used.
"Export 3D (.obj)" writes the last grid as an OBJ plus a floor texture.

## 3D worlds
The edge bits become a maze: a set bit is an open passage, a clear bit is a wall. Because
neighbours agree on shared edges, every wall is emitted exactly once and the whole grid is one
consistent world. Each floor cell is textured with its own tile from the atlas, so the Wang pattern is visible on the ground and the export works for any grid size (no giant grid image needed).

```
./wang_cli --size 30x20 --seed 5 --out grid.png --obj world.obj
./wang_cli --size 30x20 --seed 5 --out grid.png --obj world.obj --terrain   # floors rise with open edges
```
Extra options: `--wall-height F` (default 0.6) and `--wall-thickness F` (default 0.1), in cell units.
The export writes three files: `world.obj`, `world.mtl` and `world_atlas.png`. Open `world.obj` in Blender, Godot, etc. (keep all three together), or view it in the browser:

```
python3 -m http.server 8000
# open http://localhost:8000/viewer/index.html   (loads ../world.obj; or ?obj=path/to/file.obj)
```
Drag to orbit and scroll to zoom. Untick "Show walls" (or press H) to hide the walls and see the tile pattern on the floor. W A S D (or the arrow keys) move in both modes. Press "Walk inside the maze" for first-person: drag to look (or move the mouse if the browser grants pointer lock), Esc to leave. There is no wall collision yet.

This is a 2.5D extrusion of 2D Wang tiles, not true Wang cubes (matching on all six faces of a 3D lattice).

## Demo
### Input Grid
![pipes](https://github.com/user-attachments/assets/f83f2404-2973-4904-97d0-e130c2dfc11c)

### Output Grid
![photo_2025-02-08_11-11-14](https://github.com/user-attachments/assets/4003f1f4-be21-4189-9126-bf7fd69fea6c)

## References
https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html
https://tessellation.space/explore/

## Assets
https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html

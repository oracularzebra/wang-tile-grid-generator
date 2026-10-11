#ifndef TILESET_H_
#define TILESET_H_

#include <stddef.h>
#include "wang.h"

// Tiles larger than this are shrunk when a tileset is loaded.
#define TS_MAX_TILE_PX 128

typedef enum {
    TS_MATCH_AUTO,
    TS_MATCH_EDGE,    // neighbours must agree on the colour of the shared edge
    TS_MATCH_CORNER,  // neighbours must agree on the colour of the shared corners (patches, terrain, clouds)
} TsMatching;

typedef enum {
    TS_ORDER_AUTO,      // work out each tile's edges/corners from its pixels, whatever order the tiles are in
    TS_ORDER_STAGECAST, // the numbering of the Stagecast pages: edges N=1 E=2 S=4 W=8, corners NE=1 SE=2 SW=4 NW=8
    TS_ORDER_PROJECT,   // this project's edge order (right=1 top=2 left=4 bottom=8), as in reference.png
} TsOrder;

// A complete two-colour tileset (16 tiles). The tile with id t is stored at px + t * tile_px * tile_px.
// Edge sets use the edge bits of wang.h (WANG_RIGHT...); corner sets use the WANG_CORNER_* bits.
typedef struct {
    TsMatching matching;  // TS_MATCH_EDGE or TS_MATCH_CORNER once loaded
    int tile_px;
    RGBA32 *px;
    int source_pos[16];   // where tile id t came from: position in the sheet (row-major) or the folder
    int detected;         // 1 if the ids were worked out from the pixels
    float confidence;     // detection quality: how much more alike touching sides are than others (1 = chance)
    float bias;           // corner sets: share of grid vertices given colour 1 when assembling (0 means 0.5)
    char name[128];
} Tileset;

// Loads a tileset from a sheet image (a 4x4 grid of square tiles; 8x2, 2x8, 16x1 and 1x16 also work) or from a
// folder holding exactly 16 images. Returns 0, or -1 with wang_last_error() set.
int tileset_load(const char *path, TsOrder order, TsMatching matching, Tileset *out);

// Built-in corner-matched texture sets, generated on the fly (clouds, grass, wallpaper, ...).
int tileset_builtin(const char *name, Tileset *out);                 // 64px tiles
int tileset_builtin_px(const char *name, int tile_px, Tileset *out); // any tile size, 16..256
int tileset_is_builtin(const char *name);
const char *tileset_builtin_names(void); // "clouds, sunset, ..."

void tileset_free(Tileset *ts);

// Writes the tiles into a 256x256 atlas (id t in slot t%4, t/4; tiles scaled to 64px).
void tileset_to_atlas(const Tileset *ts, RGBA32 *atlas);

// Random Wang tiling of cells_w x cells_h tiles that also matches across its own border, so the image repeats
// seamlessly. Returns a malloc'd image of (cells_w*tile_px) x (cells_h*tile_px), or NULL on error.
RGBA32 *tileset_assemble(const Tileset *ts, size_t cells_w, size_t cells_h, uint32_t seed);

#endif // TILESET_H_

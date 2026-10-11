#ifndef WANG_H_
#define WANG_H_

#include <stddef.h>
#include <stdint.h>

#define WANG_TILE_PX   64
#define WANG_ATLAS_TL  4
#define WANG_ATLAS_PX  (WANG_TILE_PX * WANG_ATLAS_TL)

// Largest grid we generate, and largest grid we render to an image (8192x8192 px).
#define WANG_MAX_GRID_TL    1024
#define WANG_MAX_RENDER_TL  16384

typedef uint32_t RGBA32;
typedef uint8_t BLTR;

// Edge bits of a tile id (0..15); a tile's neighbour must carry the same bit on the shared edge.
#define WANG_RIGHT  1
#define WANG_TOP    2
#define WANG_LEFT   4
#define WANG_BOTTOM 8

typedef enum {
    WANG_SHADER_BLOBS,
    WANG_SHADER_ROADS,
} WangShader;

// Message describing the last failed call (empty if none).
const char *wang_last_error(void);
// Records a printf-style message (one %s) as the last error and returns -1.
int wang_fail(const char *fmt, const char *arg);

// Atlas is WANG_ATLAS_PX x WANG_ATLAS_PX; tile id t sits at column t%4, row t/4.
int wang_atlas_load(RGBA32 *atlas, const char *path);
void wang_atlas_generate(RGBA32 *atlas, WangShader shader);
int wang_atlas_save(const RGBA32 *atlas, const char *path);

// Corner bits of a corner-matched tile (the numbering used on the Stagecast pages).
#define WANG_CORNER_NE 1
#define WANG_CORNER_SE 2
#define WANG_CORNER_SW 4
#define WANG_CORNER_NW 8

// Fills out[w*h] with edge-matched tile ids. Same seed gives the same grid.
int wang_generate_grid(size_t w, size_t h, uint32_t seed, BLTR *out);
// As above; with wrap the grid also matches across its own border, so it repeats seamlessly.
int wang_generate_grid_ex(size_t w, size_t h, uint32_t seed, int wrap, BLTR *out);
// Corner-matched tile ids (for patch/terrain/cloud tilesets), optionally wrapping.
// bias is the share of vertices that get colour 1 (0.5 = an even mix; lower makes sparser patches/clouds).
int wang_generate_corner_grid(size_t w, size_t h, uint32_t seed, int wrap, float bias, BLTR *out);

// Returns a malloc'd w*h*TILE_PX^2 RGBA image (free() it), or NULL on error.
RGBA32 *wang_render_grid(const BLTR *tiles, size_t w, size_t h, const RGBA32 *atlas);
int wang_save_image(const char *path, const RGBA32 *px, size_t w_px, size_t h_px);

// Returns 1 if every shared edge matches, else 0.
int wang_grid_is_valid(const BLTR *tiles, size_t w, size_t h);

#endif // WANG_H_

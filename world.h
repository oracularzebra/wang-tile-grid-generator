#ifndef WORLD_H_
#define WORLD_H_

#include "mesh3d.h"
#include "tileset.h"

typedef struct {
    const Theme *theme;   // NULL = classic look
    Mesh3dOptions mesh;   // wall height, thickness, terrain; the theme-dependent fields are filled in by the export
    int ceiling;
    int markers;          // -1 = default: on for themes, off for classic
    float props_pct;      // < 0 = theme default (0 for classic)
    float water_pct;      // < 0 = theme default
    float wall_light_pct; // < 0 = theme default
    uint32_t seed;

    // Surface textures built from tilesets. Each is a built-in name (see tileset_builtin_names) or the path of a
    // tileset (sheet image or folder of 16 images). NULL keeps the theme's own look. The texture is a seamless
    // random tiling of texture_cells x texture_cells tiles, repeated over the surface.
    const char *wall_texture;
    const char *ceiling_texture;
    const char *floor_texture;  // replaces the maze-path atlas by one continuous texture
    const char *sky_texture;    // clouds etc., drawn by the viewer as a sky dome
    int texture_cells;          // default 8
    int texture_px;             // pixels per tile of built-in textures (default 64; walls use 128), 16..256
    float relief;               // depth of the wall relief (bump) map: < 0 = automatic, 0 = none, e.g. 1 = normal
    TsOrder texture_order;      // how to read tileset paths
    TsMatching texture_matching;
    const BLTR *floor_tiles;    // per-cell atlas ids for the floor (corner tilesets), or NULL
} WorldOptions;

WorldOptions world_default_options(void);

// Loads a texture spec: the path of a tileset (sheet image or folder, read with opt->texture_order/matching) or
// the name of a built-in tileset.
int wang_load_texture_spec(const char *spec, const WorldOptions *opt, Tileset *ts);

// Writes a seamless relief (bump) map made from the brightness of an image: dark grooves become low, bright
// surfaces high. Returns 0, or -1 with wang_last_error().
int wang_make_bump(const RGBA32 *img, int w, int h, const char *path);

// Builds one seamless texture (cells x cells tiles, repeating without seams) from a built-in texture name or the
// path of a tileset, and writes it to 'path' as a PNG. Only the tileset reading options of 'opt' are used.
int wang_make_texture(const char *spec, const WorldOptions *opt, uint32_t seed, int cells, const char *path);

// Writes a complete 3D world next to obj_path (which should end in .obj):
//   <stem>.obj, <stem>.mtl, <stem>_atlas.png (the floor, from 'atlas'),
//   <stem>_wall.png (themes and wall textures), <stem>_ceiling.png / <stem>_floor.png / <stem>_sky.png
//   when those textures were asked for, and <stem>.json (sky, fog, lights, start/goal for the viewer).
int wang_export_world(const BLTR *tiles, size_t w, size_t h, const RGBA32 *atlas,
                      const char *obj_path, const WorldOptions *opt);

#endif // WORLD_H_

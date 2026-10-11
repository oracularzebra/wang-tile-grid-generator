#ifndef WORLD_H_
#define WORLD_H_

#include "mesh3d.h"

typedef struct {
    const Theme *theme;   // NULL = classic look
    Mesh3dOptions mesh;   // wall height, thickness, terrain; the theme-dependent fields are filled in by the export
    int ceiling;
    int markers;          // -1 = default: on for themes, off for classic
    float props_pct;      // < 0 = theme default (0 for classic)
    float water_pct;      // < 0 = theme default
    float wall_light_pct; // < 0 = theme default
    uint32_t seed;
} WorldOptions;

WorldOptions world_default_options(void);

// Writes a complete 3D world next to obj_path (which should end in .obj):
//   <stem>.obj, <stem>.mtl, <stem>_atlas.png (the floor, from 'atlas'),
//   <stem>_wall.png (themes only) and <stem>.json (sky, fog, lights, start/goal for the viewer).
int wang_export_world(const BLTR *tiles, size_t w, size_t h, const RGBA32 *atlas,
                      const char *obj_path, const WorldOptions *opt);

#endif // WORLD_H_

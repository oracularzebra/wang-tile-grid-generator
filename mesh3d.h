#ifndef MESH3D_H_
#define MESH3D_H_

#include <stddef.h>
#include <stdint.h>
#include "wang.h"
#include "themes.h"

typedef struct {
    float wall_height;      // default 0.6
    float wall_thickness;   // default 0.1
    int terrain;            // lift each cell's floor by 'terrain_step' per set edge bit
    float terrain_step;     // default 0.15
    const char *texture;    // atlas image file name for the floor (mtl map_Kd), or NULL for a flat colour
    const char *wall_texture; // wall image file name (also used for cliffs and the ceiling), or NULL

    const Theme *theme;     // NULL = classic look: no props, water or wall lights
    int ceiling;            // add a roof over the whole maze
    int markers;            // start pad and goal beacon
    float props_pct;        // percent of cells that get a prop
    float water_pct;        // percent of cells that are water
    float wall_light_pct;   // percent of cells with a torch/lantern/light strip on a wall
    uint32_t seed;          // seeds the placement of extras (props, water, lights)
} Mesh3dOptions;

typedef struct {
    float x, y, z;
    float r, g, b;
    float intensity;
} WorldLight;

// What the exporter placed, for the .json sidecar the viewer reads.
typedef struct {
    int has_markers;
    size_t start_x, start_y, goal_x, goal_y; // cells
    size_t path_len;                         // steps along open passages from start to goal
    int has_ceiling;
    float ceiling_y;
    size_t nprops, nwater;
    WorldLight *lights;
    size_t nlights, cap_lights;
} Mesh3dInfo;

Mesh3dOptions mesh3d_default_options(void);
void mesh3d_info_free(Mesh3dInfo *info);

// Extrudes the edge-matched grid into a maze: 1x1 floor per cell (UV-mapped to its atlas tile), a wall on every
// closed edge, plus the optional extras. +x is grid right, +z is grid down, +y is up.
// Writes obj_path and the materials at mtl_path. 'info' may be NULL; free it with mesh3d_info_free.
// Returns 0 on success, -1 on failure (wang_last_error()).
int wang_export_obj(const BLTR *tiles, size_t w, size_t h,
                    const char *obj_path, const char *mtl_path,
                    const Mesh3dOptions *opts, Mesh3dInfo *info);

#endif // MESH3D_H_

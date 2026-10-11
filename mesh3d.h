#ifndef MESH3D_H_
#define MESH3D_H_

#include <stddef.h>
#include "wang.h"

typedef struct {
    float wall_height;      // default 0.6
    float wall_thickness;   // default 0.1
    int terrain;            // lift each cell's floor by 'terrain_step' per set edge bit
    float terrain_step;     // default 0.15
    const char *texture;    // atlas image file name for the floor (mtl map_Kd), or NULL for a flat colour
} Mesh3dOptions;

Mesh3dOptions mesh3d_default_options(void);

// Extrudes the edge-matched grid into a maze: 1x1 floor per cell (UV-mapped to its atlas tile), a wall on every
// closed edge. +x is grid right, +z is grid down, +y is up. Writes obj_path and a
// sibling mtl file at mtl_path. Returns 0 on success, -1 on failure (wang_last_error()).
int wang_export_obj(const BLTR *tiles, size_t w, size_t h,
                    const char *obj_path, const char *mtl_path,
                    const Mesh3dOptions *opts);

#endif // MESH3D_H_

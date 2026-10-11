#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh3d.h"

typedef enum { DIR_PX, DIR_NX, DIR_PY, DIR_PZ, DIR_NZ } Dir;

#define FACE(d) (1 << (d))
#define ALL_SIDES (FACE(DIR_PX) | FACE(DIR_NX) | FACE(DIR_PY) | FACE(DIR_PZ) | FACE(DIR_NZ))

typedef struct {
    FILE *f;
    size_t nv;   // vertices written so far (OBJ indices are 1-based)
    size_t nvt;
    size_t nfaces;
} Obj;

// vn lines are written once up front, in Dir order, so faces refer to them as dir+1.
static const float NORMALS[5][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1},
};

Mesh3dOptions mesh3d_default_options(void)
{
    Mesh3dOptions o = {0};
    o.wall_height = 0.6f;
    o.wall_thickness = 0.1f;
    o.terrain_step = 0.15f;
    return o;
}

// Emits one quad facing 'dir'. Corners are counter-clockwise seen from outside.
static void quad(Obj *o, Dir dir, const float p[4][3], const float uv[4][2])
{
    for (int i = 0; i < 4; ++i) fprintf(o->f, "v %g %g %g\n", p[i][0], p[i][1], p[i][2]);
    if (uv) for (int i = 0; i < 4; ++i) fprintf(o->f, "vt %g %g\n", uv[i][0], uv[i][1]);
    fprintf(o->f, "f");
    for (size_t i = 0; i < 4; ++i) {
        if (uv) fprintf(o->f, " %zu/%zu/%d", o->nv + i + 1, o->nvt + i + 1, (int)dir + 1);
        else    fprintf(o->f, " %zu//%d", o->nv + i + 1, (int)dir + 1);
    }
    fprintf(o->f, "\n");
    o->nv += 4;
    if (uv) o->nvt += 4;
    o->nfaces++;
}

// One face of the axis-aligned box [x0,x1]x[y0,y1]x[z0,z1].
static void face(Obj *o, Dir dir, float x0, float x1, float y0, float y1, float z0, float z1)
{
    float p[4][3];
    switch (dir) {
    case DIR_PY: { float q[4][3] = {{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}; memcpy(p, q, sizeof p); } break;
    case DIR_PZ: { float q[4][3] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}; memcpy(p, q, sizeof p); } break;
    case DIR_NZ: { float q[4][3] = {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}; memcpy(p, q, sizeof p); } break;
    case DIR_PX: { float q[4][3] = {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}}; memcpy(p, q, sizeof p); } break;
    case DIR_NX: { float q[4][3] = {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}}; memcpy(p, q, sizeof p); } break;
    }
    quad(o, dir, p, NULL);
}

static void box(Obj *o, int mask, float x0, float x1, float y0, float y1, float z0, float z1)
{
    for (Dir d = DIR_PX; d <= DIR_NZ; ++d)
        if (mask & FACE(d)) face(o, d, x0, x1, y0, y1, z0, z1);
}

typedef struct {
    const BLTR *tiles;
    size_t w, h;
    const Mesh3dOptions *opt;
} Grid;

static float cell_height(const Grid *g, size_t x, size_t y)
{
    if (!g->opt->terrain) return 0.0f;
    return (float)__builtin_popcount(g->tiles[y * g->w + x]) * g->opt->terrain_step;
}

// Horizontal edge on the line z=j covering x in [i,i+1]; j in 0..h, i in 0..w-1.
// A shared edge is a wall unless both neighbours list it as open.
static int has_hwall(const Grid *g, size_t j, size_t i)
{
    int open_above = j == 0 ? 0 : !!(g->tiles[(j - 1) * g->w + i] & WANG_BOTTOM);
    int open_below = j == g->h ? 0 : !!(g->tiles[j * g->w + i] & WANG_TOP);
    if (j == 0 || j == g->h) return !(open_above || open_below);
    return !(open_above && open_below);
}

// Vertical edge on the line x=i covering z in [j,j+1]; i in 0..w, j in 0..h-1.
static int has_vwall(const Grid *g, size_t j, size_t i)
{
    int open_left = i == 0 ? 0 : !!(g->tiles[j * g->w + i - 1] & WANG_RIGHT);
    int open_right = i == g->w ? 0 : !!(g->tiles[j * g->w + i] & WANG_LEFT);
    if (i == 0 || i == g->w) return !(open_left || open_right);
    return !(open_left && open_right);
}

static float fmin2(float a, float b) { return a < b ? a : b; }
static float fmax2(float a, float b) { return a > b ? a : b; }

// Lowest/highest floor among the existing cells in [x0,x1]x[y0,y1] (inclusive).
static void height_range(const Grid *g, long x0, long x1, long y0, long y1, float *lo, float *hi)
{
    *lo = 1e9f;
    *hi = -1e9f;
    for (long y = y0; y <= y1; ++y) {
        for (long x = x0; x <= x1; ++x) {
            if (x < 0 || y < 0 || x >= (long)g->w || y >= (long)g->h) continue;
            float c = cell_height(g, (size_t)x, (size_t)y);
            *lo = fmin2(*lo, c);
            *hi = fmax2(*hi, c);
        }
    }
}

static int write_mtl(const char *mtl_path, const Mesh3dOptions *opt)
{
    FILE *f = fopen(mtl_path, "w");
    if (!f) return wang_fail("Could not write '%s'", mtl_path);
    fprintf(f, "newmtl floor\n");
    if (opt->texture) fprintf(f, "Kd 1 1 1\nmap_Kd %s\n", opt->texture);
    else fprintf(f, "Kd 0.45 0.55 0.25\n");
    fprintf(f, "\nnewmtl wall\nKd 0.72 0.50 0.38\n\nnewmtl cliff\nKd 0.35 0.30 0.25\n");
    fclose(f);
    return 0;
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int wang_export_obj(const BLTR *tiles, size_t w, size_t h,
                    const char *obj_path, const char *mtl_path,
                    const Mesh3dOptions *user_opt)
{
    Mesh3dOptions opt = user_opt ? *user_opt : mesh3d_default_options();
    if (w == 0 || h == 0 || w > WANG_MAX_GRID_TL || h > WANG_MAX_GRID_TL)
        return wang_fail("Grid size is out of range for %s", obj_path);
    if (opt.wall_thickness <= 0.0f || opt.wall_thickness > 0.9f)
        return wang_fail("Wall thickness must be in (0, 0.9] for %s", obj_path);
    if (opt.wall_height <= 0.0f)
        return wang_fail("Wall height must be positive for %s", obj_path);

    if (write_mtl(mtl_path, &opt) != 0) return -1;

    Obj o = { fopen(obj_path, "w"), 0, 0, 0 };
    if (!o.f) return wang_fail("Could not write '%s'", obj_path);

    Grid g = { tiles, w, h, &opt };
    const float t2 = opt.wall_thickness / 2.0f;

    fprintf(o.f, "# Wang tile world %zux%zu\nmtllib %s\n", w, h, base_name(mtl_path));
    for (int i = 0; i < 5; ++i)
        fprintf(o.f, "vn %g %g %g\n", NORMALS[i][0], NORMALS[i][1], NORMALS[i][2]);

    // Floor: one quad per cell, textured with that cell's tile from the atlas.
    fprintf(o.f, "usemtl floor\n");
    for (size_t cy = 0; cy < h; ++cy) {
        for (size_t cx = 0; cx < w; ++cx) {
            float x0 = (float)cx, x1 = x0 + 1.0f, z0 = (float)cy, z1 = z0 + 1.0f;
            float y = cell_height(&g, cx, cy);
            float p[4][3] = {{x0,y,z0},{x0,y,z1},{x1,y,z1},{x1,y,z0}};
            // UV rectangle of this cell's tile in the atlas, inset half a texel to avoid bleeding.
            BLTR t = tiles[cy * w + cx];
            const float inset = 0.5f / (float)WANG_ATLAS_PX, step = 1.0f / (float)WANG_ATLAS_TL;
            float u0 = (float)(t % WANG_ATLAS_TL) * step + inset, u1 = u0 + step - 2.0f * inset;
            float v_top = 1.0f - (float)(t / WANG_ATLAS_TL) * step - inset, v_bot = v_top - step + 2.0f * inset;
            float uv[4][2] = {{u0,v_top},{u0,v_bot},{u1,v_bot},{u1,v_top}};
            quad(&o, DIR_PY, p, uv);
        }
    }

    // Terrain: untextured cliff faces wherever a neighbour (or the border) is lower.
    if (opt.terrain) {
        fprintf(o.f, "usemtl cliff\n");
        for (size_t cy = 0; cy < h; ++cy) {
            for (size_t cx = 0; cx < w; ++cx) {
                float x0 = (float)cx, x1 = x0 + 1.0f, z0 = (float)cy, z1 = z0 + 1.0f;
                float y = cell_height(&g, cx, cy);
                float hr = cx + 1 < w ? cell_height(&g, cx + 1, cy) : 0.0f;
                float hl = cx > 0     ? cell_height(&g, cx - 1, cy) : 0.0f;
                float hd = cy + 1 < h ? cell_height(&g, cx, cy + 1) : 0.0f;
                float hu = cy > 0     ? cell_height(&g, cx, cy - 1) : 0.0f;
                if (hr < y) face(&o, DIR_PX, x0, x1, hr, y, z0, z1);
                if (hl < y) face(&o, DIR_NX, x0, x1, hl, y, z0, z1);
                if (hd < y) face(&o, DIR_PZ, x0, x1, hd, y, z0, z1);
                if (hu < y) face(&o, DIR_NZ, x0, x1, hu, y, z0, z1);
            }
        }
    }

    // Walls: each shared edge once, shortened to leave room for the corner posts.
    fprintf(o.f, "usemtl wall\n");
    size_t nwalls = 0, nposts = 0;
    for (size_t j = 0; j <= h; ++j) {
        for (size_t i = 0; i < w; ++i) {
            if (!has_hwall(&g, j, i)) continue;
            float lo, hi;
            height_range(&g, (long)i, (long)i, (long)j - 1, (long)j, &lo, &hi);
            box(&o, FACE(DIR_PY) | FACE(DIR_PZ) | FACE(DIR_NZ),
                (float)i + t2, (float)i + 1.0f - t2, lo, hi + opt.wall_height,
                (float)j - t2, (float)j + t2);
            nwalls++;
        }
    }
    for (size_t j = 0; j < h; ++j) {
        for (size_t i = 0; i <= w; ++i) {
            if (!has_vwall(&g, j, i)) continue;
            float lo, hi;
            height_range(&g, (long)i - 1, (long)i, (long)j, (long)j, &lo, &hi);
            box(&o, FACE(DIR_PY) | FACE(DIR_PX) | FACE(DIR_NX),
                (float)i - t2, (float)i + t2, lo, hi + opt.wall_height,
                (float)j + t2, (float)j + 1.0f - t2);
            nwalls++;
        }
    }
    for (size_t j = 0; j <= h; ++j) {
        for (size_t i = 0; i <= w; ++i) {
            int needed = (i > 0 && has_hwall(&g, j, i - 1)) || (i < w && has_hwall(&g, j, i)) ||
                         (j > 0 && has_vwall(&g, j - 1, i)) || (j < h && has_vwall(&g, j, i));
            if (!needed) continue;
            float lo, hi;
            height_range(&g, (long)i - 1, (long)i, (long)j - 1, (long)j, &lo, &hi);
            box(&o, ALL_SIDES,
                (float)i - t2, (float)i + t2, lo, hi + opt.wall_height,
                (float)j - t2, (float)j + t2);
            nposts++;
        }
    }

    int err = ferror(o.f);
    if (fclose(o.f) != 0 || err) return wang_fail("Error while writing '%s'", obj_path);
    fprintf(stderr, "3D: %zu wall segments, %zu posts, %zu faces\n", nwalls, nposts, o.nfaces);
    return 0;
}

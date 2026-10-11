#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh3d.h"

#define PI_F 3.14159265f

typedef enum { DIR_PX, DIR_NX, DIR_PY, DIR_PZ, DIR_NZ, DIR_NY } Dir;

#define FACE(d) (1 << (d))
#define SIDES_NO_BOTTOM (FACE(DIR_PX) | FACE(DIR_NX) | FACE(DIR_PY) | FACE(DIR_PZ) | FACE(DIR_NZ))
#define NUM_SHARED_NORMALS 6

typedef struct {
    FILE *f;
    size_t nv;   // vertices written so far (OBJ indices are 1-based)
    size_t nvt;
    size_t nvn;
    size_t nfaces;
    const char *mat;
    float uv_scale;   // texture coordinates of box faces = world units * uv_scale
} Obj;

// vn lines are written once up front, in Dir order, so axis-aligned faces refer to them as dir+1.
static const float NORMALS[NUM_SHARED_NORMALS][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0},
};

Mesh3dOptions mesh3d_default_options(void)
{
    Mesh3dOptions o = {0};
    o.wall_height = 0.6f;
    o.wall_thickness = 0.1f;
    o.terrain_step = 0.15f;
    return o;
}

void mesh3d_info_free(Mesh3dInfo *info)
{
    if (!info) return;
    free(info->lights);
    memset(info, 0, sizeof *info);
}

static void add_light(Mesh3dInfo *info, float x, float y, float z, const float rgb[3], float intensity)
{
    if (info->nlights == info->cap_lights) {
        size_t cap = info->cap_lights ? info->cap_lights * 2 : 64;
        WorldLight *l = realloc(info->lights, cap * sizeof *l);
        if (!l) return; // losing a light is harmless
        info->lights = l;
        info->cap_lights = cap;
    }
    WorldLight L = { x, y, z, rgb[0], rgb[1], rgb[2], intensity };
    info->lights[info->nlights++] = L;
}

// ---------------------------------------------------------------- random numbers

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static float rnd(uint32_t *s) { return (float)(rng_next(s) & 0xFFFFFF) / 16777216.0f; }

static uint32_t hash2(int x, int y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth, non-periodic value noise in 0..1 (used to make water pools clump together).
static float value_noise(float x, float y, uint32_t s)
{
    float xf = floorf(x), yf = floorf(y);
    float tx = x - xf, ty = y - yf;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    int x0 = (int)xf, y0 = (int)yf;
    float a = (float)(hash2(x0, y0, s) & 0xFFFF) / 65536.0f;
    float b = (float)(hash2(x0 + 1, y0, s) & 0xFFFF) / 65536.0f;
    float c = (float)(hash2(x0, y0 + 1, s) & 0xFFFF) / 65536.0f;
    float d = (float)(hash2(x0 + 1, y0 + 1, s) & 0xFFFF) / 65536.0f;
    float top = a + (b - a) * tx, bot = c + (d - c) * tx;
    return top + (bot - top) * ty;
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

// ---------------------------------------------------------------- OBJ primitives

static void usemtl(Obj *o, const char *name)
{
    if (o->mat && !strcmp(o->mat, name)) return;
    fprintf(o->f, "usemtl %s\n", name);
    o->mat = name;
}

static void emit(Obj *o, const float (*p)[3], int n, const float (*uv)[2], size_t vn)
{
    for (int i = 0; i < n; ++i) fprintf(o->f, "v %g %g %g\n", p[i][0], p[i][1], p[i][2]);
    if (uv) for (int i = 0; i < n; ++i) fprintf(o->f, "vt %g %g\n", uv[i][0], uv[i][1]);
    fprintf(o->f, "f");
    for (int i = 0; i < n; ++i) {
        if (uv) fprintf(o->f, " %zu/%zu/%zu", o->nv + (size_t)i + 1, o->nvt + (size_t)i + 1, vn);
        else    fprintf(o->f, " %zu//%zu", o->nv + (size_t)i + 1, vn);
    }
    fprintf(o->f, "\n");
    o->nv += (size_t)n;
    if (uv) o->nvt += (size_t)n;
    o->nfaces++;
}

// Polygon with its own normal. The winding is flipped if needed so it faces along 'nrm'.
static void poly(Obj *o, const float (*pts)[3], int n, const float nrm[3])
{
    float tmp[16][3];
    float nx = 0, ny = 0, nz = 0; // Newell's method
    for (int i = 0; i < n; ++i) {
        const float *a = pts[i], *b = pts[(i + 1) % n];
        nx += (a[1] - b[1]) * (a[2] + b[2]);
        ny += (a[2] - b[2]) * (a[0] + b[0]);
        nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    int flip = nx * nrm[0] + ny * nrm[1] + nz * nrm[2] < 0.0f;
    for (int i = 0; i < n; ++i) memcpy(tmp[i], pts[flip ? n - 1 - i : i], sizeof tmp[i]);
    fprintf(o->f, "vn %g %g %g\n", nrm[0], nrm[1], nrm[2]);
    o->nvn++;
    emit(o, tmp, n, NULL, o->nvn);
}

// One face of the axis-aligned box [x0,x1]x[y0,y1]x[z0,z1], corners counter-clockwise from outside.
// Texture coordinates are world units, so a texture repeats once per cell and runs on across neighbouring boxes.
static void face(Obj *o, Dir dir, float x0, float x1, float y0, float y1, float z0, float z1, int with_uv)
{
    float p[4][3], uv[4][2];
    switch (dir) {
    case DIR_PY: { float q[4][3] = {{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}; memcpy(p, q, sizeof p); } break;
    case DIR_NY: { float q[4][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}}; memcpy(p, q, sizeof p); } break;
    case DIR_PZ: { float q[4][3] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}; memcpy(p, q, sizeof p); } break;
    case DIR_NZ: { float q[4][3] = {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}; memcpy(p, q, sizeof p); } break;
    case DIR_PX: { float q[4][3] = {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}}; memcpy(p, q, sizeof p); } break;
    case DIR_NX: { float q[4][3] = {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}}; memcpy(p, q, sizeof p); } break;
    }
    if (with_uv) {
        for (int i = 0; i < 4; ++i) {
            float k = o->uv_scale;
            if (dir == DIR_PY || dir == DIR_NY) { uv[i][0] = p[i][0] * k; uv[i][1] = p[i][2] * k; }
            else if (dir == DIR_PX || dir == DIR_NX) { uv[i][0] = p[i][2] * k; uv[i][1] = p[i][1] * k; }
            else { uv[i][0] = p[i][0] * k; uv[i][1] = p[i][1] * k; }
        }
    }
    emit(o, p, 4, with_uv ? uv : NULL, (size_t)dir + 1);
}

static void box(Obj *o, int mask, int with_uv, float x0, float x1, float y0, float y1, float z0, float z1)
{
    static const Dir dirs[6] = { DIR_PX, DIR_NX, DIR_PY, DIR_PZ, DIR_NZ, DIR_NY };
    for (int i = 0; i < 6; ++i)
        if (mask & FACE(dirs[i])) face(o, dirs[i], x0, x1, y0, y1, z0, z1, with_uv);
}

// Upright n-sided prism (sides plus top cap).
static void cylinder(Obj *o, float cx, float cz, float r, float y0, float y1, int sides)
{
    float top[16][3];
    for (int i = 0; i < sides; ++i) {
        float a0 = 2.0f * PI_F * (float)i / (float)sides, a1 = 2.0f * PI_F * (float)(i + 1) / (float)sides;
        float am = (a0 + a1) / 2.0f;
        float p[4][3] = {
            {cx + r * cosf(a0), y0, cz + r * sinf(a0)}, {cx + r * cosf(a1), y0, cz + r * sinf(a1)},
            {cx + r * cosf(a1), y1, cz + r * sinf(a1)}, {cx + r * cosf(a0), y1, cz + r * sinf(a0)},
        };
        float n[3] = {cosf(am), 0.0f, sinf(am)};
        poly(o, p, 4, n);
        top[i][0] = cx + r * cosf(a0); top[i][1] = y1; top[i][2] = cz + r * sinf(a0);
    }
    float up[3] = {0.0f, 1.0f, 0.0f};
    poly(o, top, sides, up);
}

// Upright cone with its apex at y1 (no base: it sits on the floor).
static void cone(Obj *o, float cx, float cz, float r, float y0, float y1, int sides)
{
    float h = y1 - y0;
    for (int i = 0; i < sides; ++i) {
        float a0 = 2.0f * PI_F * (float)i / (float)sides, a1 = 2.0f * PI_F * (float)(i + 1) / (float)sides;
        float am = (a0 + a1) / 2.0f;
        float p[3][3] = {
            {cx + r * cosf(a0), y0, cz + r * sinf(a0)}, {cx + r * cosf(a1), y0, cz + r * sinf(a1)}, {cx, y1, cz},
        };
        float n[3] = {cosf(am) * h, r, sinf(am) * h};
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        n[0] /= len; n[1] /= len; n[2] /= len;
        poly(o, p, 3, n);
    }
}

// ---------------------------------------------------------------- grid helpers

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

// Cells reachable from cell i through open edges.
static size_t neighbours(const Grid *g, size_t i, size_t out[4])
{
    size_t x = i % g->w, y = i / g->w, n = 0;
    BLTR t = g->tiles[i];
    if ((t & WANG_RIGHT) && x + 1 < g->w) out[n++] = i + 1;
    if ((t & WANG_LEFT) && x > 0) out[n++] = i - 1;
    if ((t & WANG_TOP) && y > 0) out[n++] = i - g->w;
    if ((t & WANG_BOTTOM) && y + 1 < g->h) out[n++] = i + g->w;
    return n;
}

// Breadth-first search from src over cells whose dist is -1. Returns how many cells it reached;
// they are listed in queue in visiting order, so the last one is a farthest cell.
static size_t bfs(const Grid *g, size_t src, int *dist, size_t *queue)
{
    size_t head = 0, tail = 0, nb[4];
    dist[src] = 0;
    queue[tail++] = src;
    while (head < tail) {
        size_t c = queue[head++];
        size_t n = neighbours(g, c, nb);
        for (size_t k = 0; k < n; ++k) {
            if (dist[nb[k]] < 0) {
                dist[nb[k]] = dist[c] + 1;
                queue[tail++] = nb[k];
            }
        }
    }
    return tail;
}

static void bfs_reset(int *dist, const size_t *queue, size_t count)
{
    for (size_t k = 0; k < count; ++k) dist[queue[k]] = -1;
}

// Picks the two ends of the longest route inside the biggest connected region of the maze.
// Returns 1 and fills the outputs, 0 if the maze has no passages, -1 on out of memory.
static int pick_markers(const Grid *g, size_t *start, size_t *goal, size_t *len)
{
    size_t n = g->w * g->h;
    int *dist = malloc(n * sizeof *dist);
    unsigned char *seen = calloc(n, 1);
    size_t *queue = malloc(n * sizeof *queue);
    if (!dist || !seen || !queue) { free(dist); free(seen); free(queue); return -1; }
    for (size_t i = 0; i < n; ++i) dist[i] = -1;

    size_t best_rep = 0, best_size = 0;
    for (size_t i = 0; i < n; ++i) {
        if (seen[i]) continue;
        size_t count = bfs(g, i, dist, queue);
        for (size_t k = 0; k < count; ++k) seen[queue[k]] = 1;
        if (count > best_size) { best_size = count; best_rep = i; }
        bfs_reset(dist, queue, count);
        if (best_size > n / 2) break; // no other region can be bigger than a majority
    }
    free(seen);

    int ok = 0;
    if (best_size >= 2) {
        size_t cnt = bfs(g, best_rep, dist, queue);
        size_t a = queue[cnt - 1];
        bfs_reset(dist, queue, cnt);
        cnt = bfs(g, a, dist, queue);
        size_t b = queue[cnt - 1];
        *start = a;
        *goal = b;
        *len = (size_t)dist[b];
        ok = 1;
    }
    free(dist);
    free(queue);
    return ok;
}

// ---------------------------------------------------------------- props

static void emit_prop(Obj *o, PropKind kind, float cx, float cz, float y, float s, float wall_h,
                      uint32_t *rng, Mesh3dInfo *info)
{
    switch (kind) {
    case PROP_PILLAR:
        usemtl(o, "stone");
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.14f, cx + 0.14f, y, y + 0.05f * s, cz - 0.14f, cz + 0.14f);
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.10f, cx + 0.10f, y + 0.05f * s, y + wall_h - 0.05f * s, cz - 0.10f, cz + 0.10f);
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.14f, cx + 0.14f, y + wall_h - 0.05f * s, y + wall_h, cz - 0.14f, cz + 0.14f);
        break;
    case PROP_CRATE: {
        float sz = 0.22f + 0.10f * rnd(rng);
        usemtl(o, "crate");
        box(o, SIDES_NO_BOTTOM, 0, cx - sz / 2, cx + sz / 2, y, y + sz * s, cz - sz / 2, cz + sz / 2);
        if (rnd(rng) > 0.6f) {
            float t = sz * 0.6f;
            box(o, SIDES_NO_BOTTOM, 0, cx - t / 2, cx + t / 2, y + sz * s, y + (sz + t) * s, cz - t / 2, cz + t / 2);
        }
        break;
    }
    case PROP_TREE:
        usemtl(o, "wood");
        cylinder(o, cx, cz, 0.045f, y, y + 0.2f * s, 6);
        usemtl(o, "leaf");
        cone(o, cx, cz, 0.20f, y + 0.14f * s, y + 0.46f * s, 8);
        cone(o, cx, cz, 0.14f, y + 0.32f * s, y + 0.56f * s, 8);
        break;
    case PROP_BUSH:
        usemtl(o, "leaf");
        cone(o, cx, cz, 0.17f, y, y + 0.20f * s, 8);
        cone(o, cx + 0.09f, cz + 0.06f, 0.10f, y, y + 0.14f * s, 8);
        break;
    case PROP_CACTUS:
        usemtl(o, "cactus");
        cylinder(o, cx, cz, 0.055f, y, y + 0.45f * s, 8);
        box(o, SIDES_NO_BOTTOM, 0, cx + 0.05f, cx + 0.14f, y + 0.20f * s, y + 0.26f * s, cz - 0.03f, cz + 0.03f);
        box(o, SIDES_NO_BOTTOM, 0, cx + 0.11f, cx + 0.17f, y + 0.20f * s, y + 0.36f * s, cz - 0.03f, cz + 0.03f);
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.12f, cx - 0.05f, y + 0.12f * s, y + 0.18f * s, cz - 0.03f, cz + 0.03f);
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.14f, cx - 0.09f, y + 0.12f * s, y + 0.27f * s, cz - 0.03f, cz + 0.03f);
        break;
    case PROP_ROCK:
        usemtl(o, "rock");
        cone(o, cx, cz, 0.15f, y, y + 0.13f * s, 6);
        cone(o, cx + 0.10f, cz - 0.07f, 0.08f, y, y + 0.09f * s, 5);
        break;
    case PROP_CRYSTAL: {
        static const float crystal_light[3] = {0.5f, 0.8f, 1.0f};
        usemtl(o, "crystal");
        cone(o, cx, cz, 0.09f, y, y + 0.42f * s, 6);
        cone(o, cx + 0.10f, cz + 0.04f, 0.06f, y, y + 0.26f * s, 6);
        cone(o, cx - 0.08f, cz + 0.07f, 0.055f, y, y + 0.22f * s, 6);
        if (rnd(rng) < 0.5f) add_light(info, cx, y + 0.3f * s, cz, crystal_light, 0.8f);
        break;
    }
    case PROP_PYLON: {
        static const float pylon_light[3] = {0.3f, 0.9f, 1.0f};
        usemtl(o, "metal");
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.05f, cx + 0.05f, y, y + 0.50f * s, cz - 0.05f, cz + 0.05f);
        usemtl(o, "glow");
        box(o, SIDES_NO_BOTTOM, 0, cx - 0.07f, cx + 0.07f, y + 0.50f * s, y + 0.56f * s, cz - 0.07f, cz + 0.07f);
        add_light(info, cx, y + 0.58f * s, cz, pylon_light, 0.9f);
        break;
    }
    case PROP_MOUND:
        usemtl(o, "snow");
        cone(o, cx, cz, 0.20f, y, y + 0.12f * s, 10);
        break;
    }
}

enum { SIDE_TOP, SIDE_BOTTOM, SIDE_LEFT, SIDE_RIGHT };

// A torch / lantern / light strip fixed to the inside face of the wall on 'side' of cell (cx, cz).
static void emit_wall_light(Obj *o, WallLight kind, int side, float cx, float cz, float y, float s,
                            float t2, const float colour[3], Mesh3dInfo *info)
{
    float off = t2 + 0.03f;
    float px, pz, dx = 0, dz = 0; // dx/dz: direction pointing into the cell
    switch (side) {
    case SIDE_TOP:    px = cx + 0.5f; pz = cz + off;        dz = 1; break;
    case SIDE_BOTTOM: px = cx + 0.5f; pz = cz + 1.0f - off; dz = -1; break;
    case SIDE_LEFT:   px = cx + off;  pz = cz + 0.5f;       dx = 1; break;
    default:          px = cx + 1.0f - off; pz = cz + 0.5f; dx = -1; break;
    }
    float ly = y;
    switch (kind) {
    case WALL_LIGHT_TORCH:
        usemtl(o, "wood");
        box(o, SIDES_NO_BOTTOM, 0, px - 0.025f, px + 0.025f, y + 0.28f * s, y + 0.44f * s, pz - 0.025f, pz + 0.025f);
        usemtl(o, "flame");
        box(o, SIDES_NO_BOTTOM, 0, px - 0.04f, px + 0.04f, y + 0.44f * s, y + 0.52f * s, pz - 0.04f, pz + 0.04f);
        ly = y + 0.55f * s;
        break;
    case WALL_LIGHT_LANTERN:
        usemtl(o, "wood");
        box(o, SIDES_NO_BOTTOM, 0, px - 0.015f, px + 0.015f, y, y + 0.34f * s, pz - 0.015f, pz + 0.015f);
        usemtl(o, "flame");
        box(o, SIDES_NO_BOTTOM, 0, px - 0.04f, px + 0.04f, y + 0.34f * s, y + 0.42f * s, pz - 0.04f, pz + 0.04f);
        ly = y + 0.45f * s;
        break;
    case WALL_LIGHT_STRIP: {
        float ex = dz != 0 ? 0.17f : 0.015f, ez = dz != 0 ? 0.015f : 0.17f;
        usemtl(o, "glow");
        box(o, SIDES_NO_BOTTOM, 0, px - ex, px + ex, y + 0.42f * s, y + 0.46f * s, pz - ez, pz + ez);
        ly = y + 0.44f * s;
        break;
    }
    case WALL_LIGHT_NONE:
        return;
    }
    add_light(info, px + dx * 0.08f, ly, pz + dz * 0.08f, colour, 1.0f);
}

// ---------------------------------------------------------------- materials

static void write_material_ex(FILE *f, const char *name, float r, float g, float b, const char *map,
                              float emit_scale, float alpha, const char *bump, float bump_strength)
{
    fprintf(f, "newmtl %s\nKd %g %g %g\n", name, r, g, b);
    if (map) fprintf(f, "map_Kd %s\n", map);
    if (bump) fprintf(f, "map_Bump -bm %g %s\n", bump_strength, bump);
    if (emit_scale > 0.0f) fprintf(f, "Ke %g %g %g\n", r * emit_scale, g * emit_scale, b * emit_scale);
    if (alpha < 1.0f) fprintf(f, "d %g\n", alpha);
    fprintf(f, "\n");
}

static void write_material(FILE *f, const char *name, float r, float g, float b, const char *map,
                           float emit_scale, float alpha)
{
    write_material_ex(f, name, r, g, b, map, emit_scale, alpha, NULL, 0);
}

static int write_mtl(const char *mtl_path, const Mesh3dOptions *opt)
{
    FILE *f = fopen(mtl_path, "w");
    if (!f) return wang_fail("Could not write '%s'", mtl_path);

    if (opt->texture) write_material(f, "floor", 1, 1, 1, opt->texture, 0, 1);
    else              write_material(f, "floor", 0.45f, 0.55f, 0.25f, NULL, 0, 1);

    if (opt->wall_texture) {
        write_material_ex(f, "wall", 1, 1, 1, opt->wall_texture, 0, 1, opt->wall_bump, opt->bump_strength > 0 ? opt->bump_strength : 1);
        write_material_ex(f, "cliff", 0.55f, 0.55f, 0.55f, opt->wall_texture, 0, 1, opt->wall_bump, opt->bump_strength > 0 ? opt->bump_strength : 1);
    } else {
        write_material(f, "wall", 0.72f, 0.50f, 0.38f, NULL, 0, 1);
        write_material(f, "cliff", 0.35f, 0.30f, 0.25f, NULL, 0, 1);
    }
    if (opt->ceiling_texture)    write_material(f, "ceiling", 1, 1, 1, opt->ceiling_texture, 0, 1);
    else if (opt->wall_texture)  write_material(f, "ceiling", 0.6f, 0.6f, 0.6f, opt->wall_texture, 0, 1);
    else                         write_material(f, "ceiling", 0.40f, 0.38f, 0.36f, NULL, 0, 1);

    if (opt->theme) {
        int glowing = !strcmp(opt->theme->name, "scifi"); // coolant pools glow
        write_material(f, "water", opt->theme->water[0], opt->theme->water[1], opt->theme->water[2], NULL,
                       glowing ? 0.5f : 0.0f, opt->theme->water_alpha);
    } else {
        write_material(f, "water", 0.20f, 0.40f, 0.70f, NULL, 0, 0.6f);
    }

    write_material(f, "stone", 0.50f, 0.50f, 0.52f, NULL, 0, 1);
    write_material(f, "crate", 0.55f, 0.38f, 0.20f, NULL, 0, 1);
    write_material(f, "wood", 0.40f, 0.27f, 0.14f, NULL, 0, 1);
    write_material(f, "leaf", 0.18f, 0.48f, 0.18f, NULL, 0, 1);
    write_material(f, "cactus", 0.25f, 0.55f, 0.30f, NULL, 0, 1);
    write_material(f, "rock", 0.46f, 0.44f, 0.42f, NULL, 0, 1);
    write_material(f, "crystal", 0.55f, 0.82f, 1.00f, NULL, 0.35f, 0.85f);
    write_material(f, "metal", 0.50f, 0.54f, 0.60f, NULL, 0, 1);
    write_material(f, "snow", 0.95f, 0.97f, 1.00f, NULL, 0, 1);
    write_material(f, "glow", 0.30f, 0.90f, 1.00f, NULL, 1.0f, 1);
    write_material(f, "flame", 1.00f, 0.60f, 0.15f, NULL, 1.0f, 1);
    write_material(f, "start", 0.20f, 0.90f, 0.30f, NULL, 1.0f, 1);
    write_material(f, "goal", 1.00f, 0.80f, 0.10f, NULL, 1.0f, 1);

    if (fclose(f) != 0) return wang_fail("Error while writing '%s'", mtl_path);
    return 0;
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// ---------------------------------------------------------------- export

int wang_export_obj(const BLTR *tiles, size_t w, size_t h,
                    const char *obj_path, const char *mtl_path,
                    const Mesh3dOptions *user_opt, Mesh3dInfo *info_out)
{
    Mesh3dOptions opt = user_opt ? *user_opt : mesh3d_default_options();
    if (w == 0 || h == 0 || w > WANG_MAX_GRID_TL || h > WANG_MAX_GRID_TL)
        return wang_fail("Grid size is out of range for %s", obj_path);
    if (opt.wall_thickness <= 0.0f || opt.wall_thickness > 0.9f)
        return wang_fail("Wall thickness must be in (0, 0.9] for %s", obj_path);
    if (opt.wall_height <= 0.0f)
        return wang_fail("Wall height must be positive for %s", obj_path);

    Mesh3dInfo local = {0};
    Mesh3dInfo *info = info_out ? info_out : &local;
    memset(info, 0, sizeof *info);

    if (write_mtl(mtl_path, &opt) != 0) return -1;

    Obj o = { fopen(obj_path, "w"), 0, 0, NUM_SHARED_NORMALS, 0, NULL, 1.0f };
    const float wall_uv = opt.wall_period > 0.0f ? 1.0f / opt.wall_period : 1.0f;
    if (!o.f) return wang_fail("Could not write '%s'", obj_path);

    Grid g = { tiles, w, h, &opt };
    const size_t ncells = w * h;
    const float t2 = opt.wall_thickness / 2.0f;
    const float wall_h = opt.wall_height;
    const float s = wall_h / 0.6f; // props are designed for 0.6 high walls
    uint32_t rng = (opt.seed ^ 0xA5A5A5A5u) | 1u;

    fprintf(o.f, "# Wang tile world %zux%zu\nmtllib %s\n", w, h, base_name(mtl_path));
    for (int i = 0; i < NUM_SHARED_NORMALS; ++i)
        fprintf(o.f, "vn %g %g %g\n", NORMALS[i][0], NORMALS[i][1], NORMALS[i][2]);

    float max_floor = 0.0f;
    for (size_t cy = 0; cy < h; ++cy)
        for (size_t cx = 0; cx < w; ++cx) max_floor = fmax2(max_floor, cell_height(&g, cx, cy));

    // Start and goal at the ends of the longest route, so the maze has a purpose.
    size_t start_i = 0, goal_i = 0, path_len = 0;
    if (opt.markers) {
        int r = pick_markers(&g, &start_i, &goal_i, &path_len);
        if (r < 0) { fclose(o.f); return wang_fail("Out of memory while placing markers for %s", obj_path); }
        info->has_markers = r;
        if (r) {
            info->start_x = start_i % w; info->start_y = start_i / w;
            info->goal_x = goal_i % w;   info->goal_y = goal_i / w;
            info->path_len = path_len;
        }
    }

    // Water pools: the noisiest cells, so pools clump instead of being scattered single cells.
    unsigned char *water = calloc(ncells, 1);
    if (!water) { fclose(o.f); return wang_fail("Out of memory while placing water for %s", obj_path); }
    if (opt.theme && opt.water_pct > 0.0f) {
        float *noise = malloc(ncells * sizeof *noise), *sorted = malloc(ncells * sizeof *sorted);
        if (!noise || !sorted) {
            free(noise); free(sorted); free(water); fclose(o.f);
            return wang_fail("Out of memory while placing water for %s", obj_path);
        }
        uint32_t ns = opt.seed * 2654435761u + 7u;
        for (size_t i = 0; i < ncells; ++i) {
            float x = (float)(i % w), y = (float)(i / w);
            noise[i] = 0.7f * value_noise(x * 0.30f, y * 0.30f, ns) + 0.3f * value_noise(x * 0.9f, y * 0.9f, ns + 1u);
        }
        memcpy(sorted, noise, ncells * sizeof *sorted);
        qsort(sorted, ncells, sizeof *sorted, cmp_float);
        float pct = opt.water_pct > 100.0f ? 100.0f : opt.water_pct;
        size_t cut = (size_t)((1.0f - pct / 100.0f) * (float)ncells);
        if (cut >= ncells) cut = ncells - 1;
        float threshold = sorted[cut];
        for (size_t i = 0; i < ncells; ++i) {
            int is_marker = info->has_markers && (i == start_i || i == goal_i);
            if (noise[i] >= threshold && !is_marker) { water[i] = 1; info->nwater++; }
        }
        free(noise);
        free(sorted);
    }

    // Floor: one quad per cell, textured with that cell's tile from the atlas.
    usemtl(&o, "floor");
    for (size_t cy = 0; cy < h; ++cy) {
        for (size_t cx = 0; cx < w; ++cx) {
            float x0 = (float)cx, x1 = x0 + 1.0f, z0 = (float)cy, z1 = z0 + 1.0f;
            float y = cell_height(&g, cx, cy);
            float p[4][3] = {{x0,y,z0},{x0,y,z1},{x1,y,z1},{x1,y,z0}};
            if (opt.floor_period > 0.0f) {
                // One big seamless image: the texture follows the world position and simply repeats.
                float k = 1.0f / opt.floor_period;
                float uv[4][2] = {{x0*k,1-z0*k},{x0*k,1-z1*k},{x1*k,1-z1*k},{x1*k,1-z0*k}};
                emit(&o, p, 4, uv, DIR_PY + 1);
                continue;
            }
            // UV rectangle of this cell's tile in the atlas, inset half a texel to avoid bleeding.
            BLTR t = (opt.floor_tiles ? opt.floor_tiles : tiles)[cy * w + cx];
            const float inset = 0.5f / (float)WANG_ATLAS_PX, step = 1.0f / (float)WANG_ATLAS_TL;
            float u0 = (float)(t % WANG_ATLAS_TL) * step + inset, u1 = u0 + step - 2.0f * inset;
            float v_top = 1.0f - (float)(t / WANG_ATLAS_TL) * step - inset, v_bot = v_top - step + 2.0f * inset;
            float uv[4][2] = {{u0,v_top},{u0,v_bot},{u1,v_bot},{u1,v_top}};
            emit(&o, p, 4, uv, DIR_PY + 1);
        }
    }

    // Terrain: cliff faces wherever a neighbour (or the border) is lower.
    if (opt.terrain) {
        o.uv_scale = wall_uv;
        usemtl(&o, "cliff");
        for (size_t cy = 0; cy < h; ++cy) {
            for (size_t cx = 0; cx < w; ++cx) {
                float x0 = (float)cx, x1 = x0 + 1.0f, z0 = (float)cy, z1 = z0 + 1.0f;
                float y = cell_height(&g, cx, cy);
                float hr = cx + 1 < w ? cell_height(&g, cx + 1, cy) : 0.0f;
                float hl = cx > 0     ? cell_height(&g, cx - 1, cy) : 0.0f;
                float hd = cy + 1 < h ? cell_height(&g, cx, cy + 1) : 0.0f;
                float hu = cy > 0     ? cell_height(&g, cx, cy - 1) : 0.0f;
                if (hr < y) face(&o, DIR_PX, x0, x1, hr, y, z0, z1, 1);
                if (hl < y) face(&o, DIR_NX, x0, x1, hl, y, z0, z1, 1);
                if (hd < y) face(&o, DIR_PZ, x0, x1, hd, y, z0, z1, 1);
                if (hu < y) face(&o, DIR_NZ, x0, x1, hu, y, z0, z1, 1);
            }
        }
    }

    // Water: a see-through sheet just above the floor of each pool cell.
    if (info->nwater) {
        usemtl(&o, "water");
        for (size_t i = 0; i < ncells; ++i) {
            if (!water[i]) continue;
            float x0 = (float)(i % w), z0 = (float)(i / w), y = cell_height(&g, i % w, i / w) + 0.012f;
            float p[4][3] = {{x0,y,z0},{x0,y,z0+1},{x0+1,y,z0+1},{x0+1,y,z0}};
            emit(&o, p, 4, NULL, DIR_PY + 1);
        }
    }

    // Markers: green pad at the start, gold pad and a tall beacon at the goal.
    if (info->has_markers) {
        float beacon_h = opt.ceiling ? wall_h * 0.98f : wall_h * 1.7f;
        static const float goal_light[3] = {1.0f, 0.85f, 0.3f};
        for (int which = 0; which < 2; ++which) {
            size_t cx = which ? info->goal_x : info->start_x, cy = which ? info->goal_y : info->start_y;
            float y = cell_height(&g, cx, cy);
            float x0 = (float)cx + 0.15f, x1 = (float)cx + 0.85f, z0 = (float)cy + 0.15f, z1 = (float)cy + 0.85f;
            float p[4][3] = {{x0,y+0.014f,z0},{x0,y+0.014f,z1},{x1,y+0.014f,z1},{x1,y+0.014f,z0}};
            usemtl(&o, which ? "goal" : "start");
            emit(&o, p, 4, NULL, DIR_PY + 1);
            if (which) {
                box(&o, SIDES_NO_BOTTOM, 0, (float)cx + 0.44f, (float)cx + 0.56f, y, y + beacon_h,
                    (float)cy + 0.44f, (float)cy + 0.56f);
                add_light(info, (float)cx + 0.5f, y + beacon_h * 0.8f, (float)cy + 0.5f, goal_light, 1.5f);
            }
        }
    }

    // Props and wall lights, placed by a seeded rule so a seed always gives the same world.
    if (opt.theme) {
        const Theme *th = opt.theme;
        for (size_t cy = 0; cy < h; ++cy) {
            for (size_t cx = 0; cx < w; ++cx) {
                size_t i = cy * w + cx;
                float y = cell_height(&g, cx, cy);
                int is_marker = info->has_markers && (i == start_i || i == goal_i);

                if (!water[i] && !is_marker && opt.props_pct > 0.0f && th->nprops > 0 &&
                    rnd(&rng) * 100.0f < opt.props_pct) {
                    PropKind kind = th->props[rng_next(&rng) % (uint32_t)th->nprops];
                    float jx = (rnd(&rng) - 0.5f) * 0.24f, jz = (rnd(&rng) - 0.5f) * 0.24f;
                    emit_prop(&o, kind, (float)cx + 0.5f + jx, (float)cy + 0.5f + jz, y, s, wall_h, &rng, info);
                    info->nprops++;
                }

                if (th->wall_light != WALL_LIGHT_NONE && opt.wall_light_pct > 0.0f &&
                    rnd(&rng) * 100.0f < opt.wall_light_pct) {
                    int closed[4], n = 0;
                    if (has_hwall(&g, cy, cx))     closed[n++] = SIDE_TOP;
                    if (has_hwall(&g, cy + 1, cx)) closed[n++] = SIDE_BOTTOM;
                    if (has_vwall(&g, cy, cx))     closed[n++] = SIDE_LEFT;
                    if (has_vwall(&g, cy, cx + 1)) closed[n++] = SIDE_RIGHT;
                    if (n > 0)
                        emit_wall_light(&o, th->wall_light, closed[rng_next(&rng) % (uint32_t)n], (float)cx, (float)cy,
                                        y, s, t2, th->light, info);
                }
            }
        }
    }

    // Walls: each shared edge once, shortened to leave room for the corner posts.
    o.uv_scale = wall_uv;
    usemtl(&o, "wall");
    size_t nwalls = 0, nposts = 0;
    for (size_t j = 0; j <= h; ++j) {
        for (size_t i = 0; i < w; ++i) {
            if (!has_hwall(&g, j, i)) continue;
            float lo, hi;
            height_range(&g, (long)i, (long)i, (long)j - 1, (long)j, &lo, &hi);
            box(&o, FACE(DIR_PY) | FACE(DIR_PZ) | FACE(DIR_NZ), 1,
                (float)i + t2, (float)i + 1.0f - t2, lo, hi + wall_h,
                (float)j - t2, (float)j + t2);
            nwalls++;
        }
    }
    for (size_t j = 0; j < h; ++j) {
        for (size_t i = 0; i <= w; ++i) {
            if (!has_vwall(&g, j, i)) continue;
            float lo, hi;
            height_range(&g, (long)i - 1, (long)i, (long)j, (long)j, &lo, &hi);
            box(&o, FACE(DIR_PY) | FACE(DIR_PX) | FACE(DIR_NX), 1,
                (float)i - t2, (float)i + t2, lo, hi + wall_h,
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
            box(&o, SIDES_NO_BOTTOM, 1,
                (float)i - t2, (float)i + t2, lo, hi + wall_h,
                (float)j - t2, (float)j + t2);
            nposts++;
        }
    }

    // Ceiling: one roof quad flush with the top of the walls, facing down into the maze.
    if (opt.ceiling) {
        float y = max_floor + wall_h;
        o.uv_scale = opt.ceiling_period > 0.0f ? 1.0f / opt.ceiling_period : wall_uv;
        usemtl(&o, "ceiling");
        face(&o, DIR_NY, -t2, (float)w + t2, y, y, -t2, (float)h + t2, 1);
        info->has_ceiling = 1;
        info->ceiling_y = y;
    }

    free(water);
    int err = ferror(o.f);
    if (fclose(o.f) != 0 || err) {
        if (!info_out) mesh3d_info_free(&local);
        return wang_fail("Error while writing '%s'", obj_path);
    }
    fprintf(stderr, "3D: %zu wall segments, %zu posts, %zu props, %zu water cells, %zu lights, %zu faces\n",
            nwalls, nposts, info->nprops, info->nwater, info->nlights, o.nfaces);
    if (!info_out) mesh3d_info_free(&local);
    return 0;
}

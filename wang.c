#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "la.c"
#include "wang.h"

typedef RGB (*Frag_Shader)(BLTR bltr, UV uv);

static char last_error[256];

int wang_fail(const char *fmt, const char *arg)
{
    snprintf(last_error, sizeof last_error, fmt, arg);
    return -1;
}

const char *wang_last_error(void) { return last_error; }

static RGBA32 make_rgba32(RGB c)
{
    uint32_t r = (uint32_t)(fminf(fmaxf(c.c[R], 0.0f), 1.0f) * 255.0f);
    uint32_t g = (uint32_t)(fminf(fmaxf(c.c[G], 0.0f), 1.0f) * 255.0f);
    uint32_t b = (uint32_t)(fminf(fmaxf(c.c[B], 0.0f), 1.0f) * 255.0f);
    return (b << 16) | (g << 8) | r | 0xFF000000u;
}

static RGB shader_blobs(BLTR bltr, UV uv)
{
    float r = 0.50f;
    static const RGB colors[] = {
        {{0.4f, 0.1f, 0.3f}},
        {{0.5f, 1.0f, 1.0f}}
    };
    // Same order as the edge bits: right, top, left, bottom.
    static const Vec2f sides[4] = {
        {{1.0f, 0.5f}},
        {{0.5f, 0.0f}},
        {{0.0f, 0.5f}},
        {{0.5f, 1.0f}},
    };

    RGB result = vec3fs(0.0f);
    for (size_t i = 0; i < 4; i++) {
        float t = 1.0f - fminf(vec2f_len(vec2f_sub(sides[i], uv)) / r, 1.0f);
        result = vec3f_lerp(result, colors[bltr & 1], vec3fs(t));
        bltr >>= 1;
    }
    return vec3f_pow(result, vec3fs(1.0f / 2.2f));
}

static RGB shader_roads(BLTR bltr, UV uv)
{
    const RGB rail = {{0.1f, 0.1f, 0.1f}};
    const RGB background = {{1.0f, 1.0f, 1.0f}};
    const float half_width = 0.025f;
    const float u = uv.c[U], v = uv.c[V];

    bool near_row = fabsf(v - 0.5f) < half_width; // band for left/right arms
    bool near_col = fabsf(u - 0.5f) < half_width; // band for top/bottom arms

    if ((bltr & WANG_RIGHT  && u >= 0.5f && near_row) ||
        (bltr & WANG_LEFT   && u <= 0.5f && near_row) ||
        (bltr & WANG_TOP    && v <= 0.5f && near_col) ||
        (bltr & WANG_BOTTOM && v >= 0.5f && near_col)) {
        return rail;
    }
    return background;
}

static void generate_tile32(RGBA32 *pixels, size_t stride, BLTR bltr, Frag_Shader shader)
{
    for (size_t y = 0; y < WANG_TILE_PX; ++y) {
        for (size_t x = 0; x < WANG_TILE_PX; ++x) {
            float u = (float)x / (float)WANG_TILE_PX;
            float v = (float)y / (float)WANG_TILE_PX;
            pixels[y * stride + x] = make_rgba32(shader(bltr, vec2f(u, v)));
        }
    }
}

void wang_atlas_generate(RGBA32 *atlas, WangShader shader)
{
    Frag_Shader fn = shader == WANG_SHADER_ROADS ? shader_roads : shader_blobs;
    for (BLTR t = 0; t < 16; ++t) {
        size_t x = (t % WANG_ATLAS_TL) * WANG_TILE_PX;
        size_t y = (t / WANG_ATLAS_TL) * WANG_TILE_PX;
        generate_tile32(&atlas[y * WANG_ATLAS_PX + x], WANG_ATLAS_PX, t, fn);
    }
}

int wang_atlas_load(RGBA32 *atlas, const char *path)
{
    int w, h, channels;
    unsigned char *img = stbi_load(path, &w, &h, &channels, 4);
    if (!img) return wang_fail("Could not load atlas '%s'", path);
    if (w != WANG_ATLAS_PX || h != WANG_ATLAS_PX) {
        stbi_image_free(img);
        return wang_fail("Atlas '%s' must be 256x256 (4x4 tiles of 64px)", path);
    }
    memcpy(atlas, img, sizeof(RGBA32) * WANG_ATLAS_PX * WANG_ATLAS_PX);
    stbi_image_free(img);
    return 0;
}

int wang_save_image(const char *path, const RGBA32 *px, size_t w_px, size_t h_px)
{
    if (!stbi_write_png(path, (int)w_px, (int)h_px, 4, px, (int)(w_px * sizeof(RGBA32))))
        return wang_fail("Could not write '%s'", path);
    return 0;
}

int wang_atlas_save(const RGBA32 *atlas, const char *path)
{
    return wang_save_image(path, atlas, WANG_ATLAS_PX, WANG_ATLAS_PX);
}

// xorshift32: deterministic across platforms, unlike rand().
static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

// v - value mask, p - position mask: pick a random tile whose bits under p equal v's.
static BLTR rand_tile(BLTR v, BLTR p, uint32_t *rng)
{
    BLTR cs[16];
    size_t n = 0;
    for (BLTR c = 0; c < 16; ++c) {
        if ((c & p) == (v & p)) cs[n++] = c;
    }
    return cs[rng_next(rng) % n];
}

int wang_generate_grid(size_t w, size_t h, uint32_t seed, BLTR *out)
{
    if (w == 0 || h == 0 || w > WANG_MAX_GRID_TL || h > WANG_MAX_GRID_TL) {
        snprintf(last_error, sizeof last_error, "Grid size must be 1..%d in each direction", WANG_MAX_GRID_TL);
        return -1;
    }
    uint32_t rng = seed ? seed : 0x9E3779B9u; // xorshift state must be non-zero

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            BLTR v = 0, p = 0;
            if (x > 0) { // left neighbour's right edge must equal our left edge
                p |= WANG_LEFT;
                v |= (out[y * w + x - 1] & WANG_RIGHT) << 2;
            }
            if (y > 0) { // top neighbour's bottom edge must equal our top edge
                p |= WANG_TOP;
                v |= (out[(y - 1) * w + x] & WANG_BOTTOM) >> 2;
            }
            out[y * w + x] = rand_tile(v, p, &rng);
        }
    }
    return 0;
}

int wang_grid_is_valid(const BLTR *tiles, size_t w, size_t h)
{
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            BLTR t = tiles[y * w + x];
            if (x + 1 < w && !!(t & WANG_RIGHT) != !!(tiles[y * w + x + 1] & WANG_LEFT)) return 0;
            if (y + 1 < h && !!(t & WANG_BOTTOM) != !!(tiles[(y + 1) * w + x] & WANG_TOP)) return 0;
        }
    }
    return 1;
}

RGBA32 *wang_render_grid(const BLTR *tiles, size_t w, size_t h, const RGBA32 *atlas)
{
    if (w == 0 || h == 0 || w * h > WANG_MAX_RENDER_TL) {
        snprintf(last_error, sizeof last_error, "Cannot render more than %d tiles to an image", WANG_MAX_RENDER_TL);
        return NULL;
    }
    size_t w_px = w * WANG_TILE_PX, h_px = h * WANG_TILE_PX;
    RGBA32 *px = malloc(w_px * h_px * sizeof(RGBA32));
    if (!px) {
        snprintf(last_error, sizeof last_error, "Out of memory");
        return NULL;
    }

    for (size_t gy = 0; gy < h; ++gy) {
        for (size_t gx = 0; gx < w; ++gx) {
            BLTR t = tiles[gy * w + gx];
            const RGBA32 *src = &atlas[(t / WANG_ATLAS_TL) * WANG_TILE_PX * WANG_ATLAS_PX +
                                       (t % WANG_ATLAS_TL) * WANG_TILE_PX];
            RGBA32 *dst = &px[gy * WANG_TILE_PX * w_px + gx * WANG_TILE_PX];
            for (size_t row = 0; row < WANG_TILE_PX; ++row)
                memcpy(dst + row * w_px, src + row * WANG_ATLAS_PX, WANG_TILE_PX * sizeof(RGBA32));
        }
    }
    return px;
}

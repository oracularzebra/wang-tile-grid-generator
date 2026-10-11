#ifndef NOISE_H_
#define NOISE_H_

// Small colour and periodic-noise helpers shared by the procedural texture code (themes.c, tileset.c).
// Everything here is periodic with period 1.0 in u and v, so the textures built from it tile seamlessly.

#include <math.h>
#include <stdint.h>

#include "wang.h"

typedef struct { float r, g, b; } C3;

static inline C3 c3(float r, float g, float b) { C3 c = {r, g, b}; return c; }
static inline C3 mixc(C3 a, C3 b, float t) { return c3(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t); }
static inline C3 mulc(C3 a, float s) { return c3(a.r * s, a.g * s, a.b * s); }
static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float fractf(float x) { return x - floorf(x); }
static inline float minf2(float a, float b) { return a < b ? a : b; }

static inline float smoothstepf(float e0, float e1, float x)
{
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static inline uint32_t hash2(int x, int y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline float rnd01(int x, int y, uint32_t s) { return (float)(hash2(x, y, s) & 0xFFFFFF) / 16777216.0f; }

// Value noise that repeats every 1.0 in u and v (fx, fy lattice cells per period), so textures tile.
static inline float vnoise2(float u, float v, int fx, int fy, uint32_t s)
{
    float x = u * (float)fx, y = v * (float)fy;
    float xf = floorf(x), yf = floorf(y);
    float tx = x - xf, ty = y - yf;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    int x0 = (((int)xf % fx) + fx) % fx, y0 = (((int)yf % fy) + fy) % fy;
    int x1 = (x0 + 1) % fx, y1 = (y0 + 1) % fy;
    float a = rnd01(x0, y0, s), b = rnd01(x1, y0, s), c = rnd01(x0, y1, s), d = rnd01(x1, y1, s);
    float top = a + (b - a) * tx, bot = c + (d - c) * tx;
    return top + (bot - top) * ty;
}

static inline float fbm(float u, float v, int freq, int octaves, uint32_t s)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoise2(u, v, freq, freq, s + (uint32_t)o * 101u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2;
    }
    return sum / norm;
}

static inline RGBA32 pack(C3 c)
{
    uint32_t r = (uint32_t)(clamp01(c.r) * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(clamp01(c.g) * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(clamp01(c.b) * 255.0f + 0.5f);
    return (b << 16) | (g << 8) | r | 0xFF000000u;
}

#endif // NOISE_H_

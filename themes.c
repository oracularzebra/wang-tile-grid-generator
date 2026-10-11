#include <math.h>
#include <string.h>

#include "themes.h"
#include "noise.h"

// ---------------------------------------------------------------- theme table

static const PropKind DUNGEON_PROPS[] = { PROP_PILLAR, PROP_CRATE, PROP_ROCK };
static const PropKind HEDGE_PROPS[]   = { PROP_TREE, PROP_BUSH, PROP_ROCK };
static const PropKind SCIFI_PROPS[]   = { PROP_PYLON, PROP_CRATE, PROP_PILLAR };
static const PropKind DESERT_PROPS[]  = { PROP_CACTUS, PROP_ROCK, PROP_PILLAR };
static const PropKind ICE_PROPS[]     = { PROP_CRYSTAL, PROP_MOUND, PROP_ROCK };

enum { T_DUNGEON, T_HEDGE, T_SCIFI, T_DESERT, T_ICE, T_COUNT };

static const Theme THEMES[T_COUNT] = {
    { "dungeon", "brick walls, flagstone floor with a red carpet, torches",
      {0.03f, 0.03f, 0.05f}, {0.10f, 0.08f, 0.09f}, {0.04f, 0.035f, 0.04f}, 0.055f,
      0.45f, 0.35f,
      {0.10f, 0.22f, 0.20f}, 0.75f,
      {1.0f, 0.62f, 0.25f}, WALL_LIGHT_TORCH,
      DUNGEON_PROPS, 3, 6.0f, 8.0f, 7.0f },
    { "hedge", "leafy hedge walls, grass with dirt paths, lanterns",
      {0.40f, 0.65f, 0.95f}, {0.85f, 0.93f, 1.00f}, {0.75f, 0.88f, 0.85f}, 0.012f,
      1.0f, 1.6f,
      {0.20f, 0.45f, 0.70f}, 0.65f,
      {1.0f, 0.85f, 0.45f}, WALL_LIGHT_LANTERN,
      HEDGE_PROPS, 3, 9.0f, 8.0f, 3.0f },
    { "scifi", "metal panel walls, dark deck with glowing circuit lines",
      {0.03f, 0.02f, 0.10f}, {0.10f, 0.06f, 0.22f}, {0.04f, 0.03f, 0.12f}, 0.035f,
      0.55f, 0.45f,
      {0.00f, 0.55f, 0.65f}, 0.60f,
      {0.30f, 0.90f, 1.00f}, WALL_LIGHT_STRIP,
      SCIFI_PROPS, 3, 6.0f, 6.0f, 6.0f },
    { "desert", "sandstone walls, rippled sand with paved paths, torches",
      {0.93f, 0.66f, 0.38f}, {0.99f, 0.90f, 0.70f}, {0.95f, 0.82f, 0.62f}, 0.014f,
      1.1f, 1.8f,
      {0.10f, 0.65f, 0.60f}, 0.65f,
      {1.0f, 0.70f, 0.30f}, WALL_LIGHT_TORCH,
      DESERT_PROPS, 3, 6.0f, 6.0f, 3.0f },
    { "ice", "frosted ice walls, snow with glossy ice paths, glowing crystals",
      {0.62f, 0.80f, 0.98f}, {0.95f, 0.98f, 1.00f}, {0.80f, 0.90f, 1.00f}, 0.018f,
      1.1f, 1.4f,
      {0.05f, 0.25f, 0.55f}, 0.75f,
      {0.55f, 0.80f, 1.00f}, WALL_LIGHT_NONE,
      ICE_PROPS, 3, 8.0f, 10.0f, 0.0f },
};

const Theme *theme_by_name(const char *name)
{
    for (int i = 0; i < T_COUNT; ++i)
        if (!strcmp(name, THEMES[i].name)) return &THEMES[i];
    return NULL;
}

int theme_is_known(const char *name)
{
    return !strcmp(name, "classic") || theme_by_name(name) != NULL;
}

const char *theme_name_list(void)
{
    return "classic, dungeon, hedge, scifi, desert, ice";
}

// ---------------------------------------------------------------- floor atlas

static float dist_to_segment(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    float t = clamp01(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy));
    float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return sqrtf(cx * cx + cy * cy);
}

// 0..1 mask of the path joining the tile centre to the midpoint of every open edge.
// Open edges end exactly on the tile border, so neighbouring tiles continue the same path.
static float path_mask(BLTR bits, float u, float v, float half_width, float wobble, uint32_t seed)
{
    static const float mx[4] = {1.0f, 0.5f, 0.0f, 0.5f}; // right, top, left, bottom
    static const float my[4] = {0.5f, 0.0f, 0.5f, 1.0f};
    float best = 1e9f;
    for (int i = 0; i < 4; ++i) {
        if (bits & (1 << i)) best = minf2(best, dist_to_segment(u, v, 0.5f, 0.5f, mx[i], my[i]));
    }
    if (best > 1e8f) return 0.0f;
    float w = half_width + (vnoise2(u, v, 4, 4, seed) - 0.5f) * wobble;
    return 1.0f - smoothstepf(w - 0.02f, w + 0.02f, best);
}

static C3 floor_dungeon(BLTR bits, float u, float v)
{
    int row = (int)floorf(v * 2.0f);
    float sx = u * 2.0f + 0.5f * (float)(row & 1);
    int col = (int)floorf(sx);
    float fx = sx - (float)col, fy = v * 2.0f - (float)row;
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    float mortar = 1.0f - smoothstepf(0.03f, 0.06f, edge);
    float id = rnd01(((col % 2) + 2) % 2, row & 1, 11);
    float base = 0.30f + 0.14f * id;
    float n = fbm(u, v, 8, 3, 12);
    C3 c = mulc(c3(1.0f, 1.0f, 1.06f), base * (0.85f + 0.3f * n));
    c = mixc(c, c3(0.08f, 0.08f, 0.09f), mortar);

    float outer = path_mask(bits, u, v, 0.15f, 0.03f, 13);
    float inner = path_mask(bits, u, v, 0.12f, 0.03f, 13);
    C3 carpet = mulc(c3(0.50f, 0.07f, 0.09f), 0.85f + 0.3f * vnoise2(u, v, 32, 32, 14));
    c = mixc(c, carpet, inner);
    return mixc(c, c3(0.85f, 0.68f, 0.20f), outer * (1.0f - inner));
}

static C3 floor_hedge(BLTR bits, float u, float v)
{
    float n = fbm(u, v, 6, 3, 21);
    C3 c = mixc(c3(0.12f, 0.34f, 0.10f), c3(0.30f, 0.60f, 0.18f), n);
    if (vnoise2(u, v, 48, 48, 22) > 0.8f) c = mulc(c, 1.25f);

    float m = path_mask(bits, u, v, 0.15f, 0.05f, 23);
    C3 dirt = mixc(c3(0.50f, 0.38f, 0.24f), c3(0.65f, 0.52f, 0.35f), fbm(u, v, 10, 2, 24));
    if (vnoise2(u, v, 40, 40, 25) > 0.86f) dirt = c3(0.55f, 0.55f, 0.55f); // pebbles
    return mixc(c, dirt, m);
}

static C3 floor_scifi(BLTR bits, float u, float v)
{
    float fx = fractf(u * 4.0f), fy = fractf(v * 4.0f);
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    float brushed = vnoise2(u, v, 2, 96, 31);
    C3 c = mulc(c3(0.07f, 0.08f, 0.11f), 0.9f + 0.4f * brushed);
    c = mixc(c, c3(0.17f, 0.20f, 0.27f), 1.0f - smoothstepf(0.015f, 0.035f, edge));

    float halo = path_mask(bits, u, v, 0.13f, 0.0f, 32);
    float core = path_mask(bits, u, v, 0.035f, 0.0f, 32);
    c = mixc(c, c3(0.02f, 0.25f, 0.36f), halo * 0.7f);
    return mixc(c, c3(0.60f, 1.00f, 1.00f), core);
}

static C3 floor_desert(BLTR bits, float u, float v)
{
    float n = fbm(u, v, 4, 2, 41);
    float ripple = 0.5f + 0.5f * sinf(6.2831853f * (6.0f * v + n * 1.5f + vnoise2(u, v, 3, 3, 42)));
    C3 c = mixc(c3(0.82f, 0.66f, 0.42f), c3(0.92f, 0.78f, 0.52f), ripple * 0.5f + n * 0.5f);
    c = mulc(c, 0.97f + 0.06f * vnoise2(u, v, 64, 64, 43));

    float m = path_mask(bits, u, v, 0.15f, 0.02f, 44);
    int px = (int)floorf(u * 4.0f), py = (int)floorf(v * 4.0f);
    float fx = fractf(u * 4.0f), fy = fractf(v * 4.0f);
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    C3 paver = mulc(c3(0.62f, 0.40f, 0.28f), 0.85f + 0.3f * rnd01(px & 3, py & 3, 45));
    paver = mixc(paver, c3(0.40f, 0.26f, 0.18f), 1.0f - smoothstepf(0.03f, 0.07f, edge));
    return mixc(c, paver, m);
}

static C3 floor_ice(BLTR bits, float u, float v)
{
    float n = fbm(u, v, 6, 3, 51);
    C3 c = mixc(c3(0.80f, 0.90f, 0.97f), c3(0.95f, 0.98f, 1.00f), n);
    if (vnoise2(u, v, 64, 64, 52) > 0.9f) c = c3(1.0f, 1.0f, 1.0f); // sparkles

    float m = path_mask(bits, u, v, 0.15f, 0.04f, 53);
    C3 ice = mulc(c3(0.40f, 0.68f, 0.90f), 0.9f + 0.2f * fbm(u, v, 5, 3, 54));
    float crack = 1.0f - smoothstepf(0.012f, 0.03f, fabsf(fbm(u, v, 6, 3, 55) - 0.5f));
    ice = mixc(ice, c3(0.85f, 0.95f, 1.0f), crack * 0.8f);
    return mixc(c, ice, m);
}

typedef C3 (*FloorFn)(BLTR bits, float u, float v);
static const FloorFn FLOOR_FNS[T_COUNT] = { floor_dungeon, floor_hedge, floor_scifi, floor_desert, floor_ice };

void theme_generate_atlas(const Theme *theme, RGBA32 *atlas)
{
    FloorFn fn = FLOOR_FNS[theme - THEMES];
    for (BLTR t = 0; t < 16; ++t) {
        size_t ox = (t % WANG_ATLAS_TL) * WANG_TILE_PX, oy = (t / WANG_ATLAS_TL) * WANG_TILE_PX;
        for (size_t y = 0; y < WANG_TILE_PX; ++y) {
            for (size_t x = 0; x < WANG_TILE_PX; ++x) {
                // Pixel centres, so opposite edges of neighbouring tiles are mirror images of each other.
                float u = ((float)x + 0.5f) / (float)WANG_TILE_PX;
                float v = ((float)y + 0.5f) / (float)WANG_TILE_PX;
                atlas[(oy + y) * WANG_ATLAS_PX + ox + x] = pack(fn(t, u, v));
            }
        }
    }
}

// ---------------------------------------------------------------- wall textures

static C3 wall_dungeon(float u, float v)
{
    int row = (int)floorf(v * 4.0f);
    float bx = u * 2.0f + 0.5f * (float)(row & 1);
    int col = (int)floorf(bx);
    float fx = bx - (float)col, fy = v * 4.0f - (float)row;
    float id = rnd01(((col % 2) + 2) % 2, row & 3, 61);
    C3 brick = mulc(c3(0.55f, 0.25f, 0.18f), 0.75f + 0.5f * id);
    brick = mulc(brick, 0.85f + 0.3f * fbm(u, v, 16, 3, 62));
    float mortar = 1.0f - smoothstepf(0.0f, 0.04f, minf2(minf2(fx, 1.0f - fx) * 0.5f, minf2(fy, 1.0f - fy) * 0.25f));
    return mixc(brick, c3(0.34f, 0.32f, 0.29f), mortar);
}

static C3 wall_hedge(float u, float v)
{
    float n = fbm(u, v, 10, 3, 71);
    C3 c = mixc(c3(0.08f, 0.25f, 0.07f), c3(0.28f, 0.60f, 0.15f), n);
    c = mixc(c, c3(0.03f, 0.10f, 0.03f), 1.0f - smoothstepf(0.25f, 0.4f, fbm(u, v, 16, 2, 72))); // dark gaps
    if (vnoise2(u, v, 32, 32, 73) > 0.78f) c = mulc(c, 1.35f);                                   // bright leaves
    return c;
}

static C3 wall_scifi(float u, float v)
{
    float fx = fractf(u * 2.0f), fy = fractf(v * 2.0f);
    float edge = minf2(minf2(fx, 1.0f - fx), minf2(fy, 1.0f - fy));
    C3 c = mulc(c3(0.35f, 0.38f, 0.43f), 0.9f + 0.2f * vnoise2(u, v, 2, 128, 81)); // brushed metal
    c = mixc(c, c3(0.04f, 0.05f, 0.07f), 1.0f - smoothstepf(0.01f, 0.03f, edge));    // seams
    // rivets near each panel corner
    float rx = fractf(u * 2.0f + 0.5f) - 0.5f, ry = fractf(v * 2.0f + 0.5f) - 0.5f;
    if (sqrtf(rx * rx + ry * ry) < 0.025f) c = c3(0.6f, 0.62f, 0.66f);
    // glowing strip across the middle of every panel row
    float strip = 1.0f - smoothstepf(0.012f, 0.03f, fabsf(fractf(v * 2.0f) - 0.5f));
    return mixc(c, c3(0.25f, 0.95f, 1.0f), strip * 0.9f);
}

static C3 wall_desert(float u, float v)
{
    int row = (int)floorf(v * 3.0f);
    float bx = u * 2.0f + 0.5f * (float)(row & 1);
    int col = (int)floorf(bx);
    float fx = bx - (float)col, fy = v * 3.0f - (float)row;
    float band = fbm(u, v, 4, 3, 91);
    C3 stone = mixc(c3(0.72f, 0.52f, 0.33f), c3(0.88f, 0.72f, 0.50f), band);
    stone = mulc(stone, 0.9f + 0.2f * rnd01(((col % 2) + 2) % 2, row % 3, 92));
    float joint = 1.0f - smoothstepf(0.0f, 0.05f, minf2(minf2(fx, 1.0f - fx) * 0.5f, minf2(fy, 1.0f - fy) * 0.33f));
    return mixc(stone, c3(0.48f, 0.36f, 0.24f), joint);
}

static C3 wall_ice(float u, float v)
{
    float n = fbm(u, v, 5, 4, 101);
    C3 c = mixc(c3(0.50f, 0.72f, 0.90f), c3(0.90f, 0.97f, 1.00f), n);
    float crack = 1.0f - smoothstepf(0.01f, 0.03f, fabsf(fbm(u, v, 5, 3, 102) - 0.5f));
    c = mixc(c, c3(1.0f, 1.0f, 1.0f), crack * 0.85f);
    if (vnoise2(u, v, 64, 64, 103) > 0.93f) c = c3(1.0f, 1.0f, 1.0f);
    return c;
}

typedef C3 (*WallFn)(float u, float v);
static const WallFn WALL_FNS[T_COUNT] = { wall_dungeon, wall_hedge, wall_scifi, wall_desert, wall_ice };

void theme_generate_wall(const Theme *theme, RGBA32 *px)
{
    WallFn fn = WALL_FNS[theme - THEMES];
    for (int y = 0; y < WALL_TEX_PX; ++y) {
        for (int x = 0; x < WALL_TEX_PX; ++x) {
            float u = ((float)x + 0.5f) / (float)WALL_TEX_PX;
            float v = ((float)y + 0.5f) / (float)WALL_TEX_PX;
            px[y * WALL_TEX_PX + x] = pack(fn(u, v));
        }
    }
}

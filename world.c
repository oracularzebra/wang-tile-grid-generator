#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "world.h"

WorldOptions world_default_options(void)
{
    WorldOptions o = {0};
    o.mesh = mesh3d_default_options();
    o.markers = -1;
    o.props_pct = o.water_pct = o.wall_light_pct = -1.0f;
    o.texture_cells = 8;
    o.texture_px = 0; // 0 = automatic
    o.relief = -1.0f;
    return o;
}

static char *join(const char *stem, const char *suffix)
{
    char *s = malloc(strlen(stem) + strlen(suffix) + 1);
    if (s) { strcpy(s, stem); strcat(s, suffix); }
    return s;
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// A texture spec is the path of a user tileset, or the name of a built-in one.
int wang_load_texture_spec(const char *spec, const WorldOptions *opt, Tileset *ts)
{
    struct stat st;
    if (stat(spec, &st) == 0) return tileset_load(spec, opt->texture_order, opt->texture_matching, ts);
    if (tileset_is_builtin(spec)) return tileset_builtin_px(spec, opt->texture_px > 0 ? opt->texture_px : WANG_TILE_PX, ts);
    char msg[256];
    snprintf(msg, sizeof msg, "Unknown texture '%%s' (built-in: %s; or give the path of a tileset image or folder)",
             tileset_builtin_names());
    return wang_fail(msg, spec);
}

// Seamless relief map from brightness: blur a little (wrapping around, so it still tiles), then stretch the
// contrast so that the darkest 2% become black and the brightest 2% white.
int wang_make_bump(const RGBA32 *img, int w, int h, const char *path)
{
    size_t n = (size_t)w * h;
    float *lum = malloc(n * sizeof *lum), *blur = malloc(n * sizeof *blur), *sorted = malloc(n * sizeof *sorted);
    RGBA32 *out = malloc(n * sizeof *out);
    int rc = -1;
    if (!lum || !blur || !sorted || !out) { wang_fail("Out of memory making the relief map for %s", path); goto done; }
    for (size_t i = 0; i < n; ++i) {
        RGBA32 p = img[i];
        lum[i] = 0.3f * (float)(p & 255) + 0.6f * (float)((p >> 8) & 255) + 0.1f * (float)((p >> 16) & 255);
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    sum += lum[(size_t)((y + dy + h) % h) * w + (size_t)((x + dx + w) % w)];
            blur[(size_t)y * w + x] = sum / 9.0f;
        }
    }
    memcpy(sorted, blur, n * sizeof *sorted);
    // percentiles by selection of a coarse histogram (values are 0..255)
    {
        size_t hist[256] = {0}, acc = 0;
        for (size_t i = 0; i < n; ++i) { int b = (int)blur[i]; hist[b < 0 ? 0 : b > 255 ? 255 : b]++; }
        int lo = 0, hi = 255;
        for (int b = 0; b < 256; ++b) { acc += hist[b]; if (acc >= n / 50) { lo = b; break; } }
        acc = 0;
        for (int b = 255; b >= 0; --b) { acc += hist[b]; if (acc >= n / 50) { hi = b; break; } }
        if (hi <= lo) hi = lo + 1;
        for (size_t i = 0; i < n; ++i) {
            float t = (blur[i] - (float)lo) / (float)(hi - lo);
            unsigned v = (unsigned)((t < 0 ? 0 : t > 1 ? 1 : t) * 255.0f + 0.5f);
            out[i] = v | (v << 8) | (v << 16) | 0xFF000000u;
        }
    }
    rc = wang_save_image(path, out, (size_t)w, (size_t)h);
done:
    free(lum); free(blur); free(sorted); free(out);
    return rc;
}

// Builds one seamless texture from a tileset. Returns a malloc'd image of *w x *h pixels, or NULL.
static RGBA32 *build_surface(const char *spec, const WorldOptions *opt, uint32_t seed, int cells, int *w, int *h)
{
    Tileset ts;
    if (wang_load_texture_spec(spec, opt, &ts) != 0) return NULL;
    RGBA32 *img = tileset_assemble(&ts, (size_t)cells, (size_t)cells, seed);
    *w = cells * ts.tile_px;
    *h = cells * ts.tile_px;
    tileset_free(&ts);
    return img;
}

// Builds one seamless texture from a tileset and writes it as a PNG (and, if bump_path is given, its relief map).
static int make_surface_files(const char *spec, const WorldOptions *opt, uint32_t seed, int cells, const char *path,
                              const char *bump_path)
{
    int w, h;
    RGBA32 *img = build_surface(spec, opt, seed, cells, &w, &h);
    if (!img) return -1;
    int rc = wang_save_image(path, img, (size_t)w, (size_t)h);
    if (rc == 0 && bump_path) rc = wang_make_bump(img, w, h, bump_path);
    free(img);
    return rc;
}

int wang_make_texture(const char *spec, const WorldOptions *opt, uint32_t seed, int cells, const char *path)
{
    return make_surface_files(spec, opt, seed, cells, path, NULL);
}

// How deep the wall relief should be when --relief is not given: carved stone and cave rock always get it,
// the brick and sandstone themes a little.
static float auto_relief(const char *wall_spec, const Theme *th)
{
    if (wall_spec) return (!strcmp(wall_spec, "cave") || !strcmp(wall_spec, "stone")) ? 1.0f : 0.0f;
    if (th && (!strcmp(th->name, "dungeon") || !strcmp(th->name, "desert"))) return 0.6f;
    return 0.0f;
}

static void json_rgb(FILE *f, const float c[3])
{
    fprintf(f, "[%g, %g, %g]", c[0], c[1], c[2]);
}

static int write_json(const char *path, const Theme *th, size_t w, size_t h, const WorldOptions *opt,
                      const Mesh3dInfo *info, const char *sky_file, const BLTR *tiles)
{
    // Used when there is no theme (classic look).
    static const float classic_sky[3] = {0.06f, 0.08f, 0.10f};
    FILE *f = fopen(path, "w");
    if (!f) return wang_fail("Could not write '%s'", path);

    fprintf(f, "{\n  \"theme\": \"%s\",\n  \"size\": {\"w\": %zu, \"h\": %zu},\n", th ? th->name : "classic", w, h);
    fprintf(f, "  \"sky\": {\"top\": ");
    json_rgb(f, th ? th->sky_top : classic_sky);
    fprintf(f, ", \"bottom\": ");
    json_rgb(f, th ? th->sky_bottom : classic_sky);
    fprintf(f, "},\n  \"fog\": {\"color\": ");
    json_rgb(f, th ? th->fog : classic_sky);
    fprintf(f, ", \"density\": %g},\n", th ? th->fog_density : 0.0f);
    if (sky_file) fprintf(f, "  \"sky_texture\": \"%s\",\n", sky_file);
    else          fprintf(f, "  \"sky_texture\": null,\n");
    fprintf(f, "  \"ambient\": %g,\n  \"sun\": %g,\n", th ? th->ambient : 1.0f, th ? th->sun : 1.6f);
    fprintf(f, "  \"terrain\": %s,\n", opt->mesh.terrain ? "true" : "false");
    fprintf(f, "  \"terrain_step\": %g,\n  \"wall_thickness\": %g,\n", opt->mesh.terrain_step, opt->mesh.wall_thickness);
    // The maze itself (one hex digit per cell: right=1 top=2 left=4 bottom=8), so the viewer can stop you at walls.
    fprintf(f, "  \"tiles\": [");
    for (size_t y = 0; y < h; ++y) {
        fprintf(f, "%s\n    \"", y ? "," : "");
        for (size_t x = 0; x < w; ++x) fputc("0123456789abcdef"[tiles[y * w + x] & 15], f);
        fputc('"', f);
    }
    fprintf(f, "\n  ],\n");

    if (info->has_ceiling) fprintf(f, "  \"ceiling\": {\"y\": %g},\n", info->ceiling_y);
    else                   fprintf(f, "  \"ceiling\": null,\n");

    if (info->has_markers) {
        float step = opt->mesh.terrain_step;
        fprintf(f, "  \"start\": {\"cell\": [%zu, %zu], \"x\": %g, \"z\": %g},\n", info->start_x, info->start_y,
                (double)info->start_x + 0.5, (double)info->start_y + 0.5);
        fprintf(f, "  \"goal\": {\"cell\": [%zu, %zu], \"x\": %g, \"z\": %g},\n", info->goal_x, info->goal_y,
                (double)info->goal_x + 0.5, (double)info->goal_y + 0.5);
        fprintf(f, "  \"path_len\": %zu,\n", info->path_len);
        (void)step;
    } else {
        fprintf(f, "  \"start\": null,\n  \"goal\": null,\n  \"path_len\": 0,\n");
    }

    fprintf(f, "  \"wall_materials\": [\"wall\", \"cliff\"],\n");
    fprintf(f, "  \"prop_materials\": [\"stone\", \"crate\", \"wood\", \"leaf\", \"cactus\", \"rock\", "
               "\"crystal\", \"metal\", \"snow\", \"glow\", \"flame\"],\n");
    fprintf(f, "  \"counts\": {\"props\": %zu, \"water\": %zu, \"lights\": %zu},\n", info->nprops, info->nwater, info->nlights);

    fprintf(f, "  \"lights\": [");
    for (size_t i = 0; i < info->nlights; ++i) {
        const WorldLight *l = &info->lights[i];
        fprintf(f, "%s\n    [%g, %g, %g, %g, %g, %g, %g]", i ? "," : "", l->x, l->y, l->z, l->r, l->g, l->b, l->intensity);
    }
    fprintf(f, "%s]\n}\n", info->nlights ? "\n  " : "");

    int err = ferror(f);
    if (fclose(f) != 0 || err) return wang_fail("Error while writing '%s'", path);
    return 0;
}

int wang_export_world(const BLTR *tiles, size_t w, size_t h, const RGBA32 *atlas,
                      const char *obj_path, const WorldOptions *user)
{
    WorldOptions opt = user ? *user : world_default_options();
    const Theme *th = opt.theme;

    size_t n = strlen(obj_path);
    char *stem = malloc(n + 1);
    if (!stem) return wang_fail("Out of memory exporting %s", obj_path);
    strcpy(stem, obj_path);
    if (n > 4 && !strcmp(stem + n - 4, ".obj")) stem[n - 4] = '\0';

    char *mtl_path = join(stem, ".mtl"), *atlas_path = join(stem, "_atlas.png");
    char *wall_path = join(stem, "_wall.png"), *json_path = join(stem, ".json");
    char *ceiling_path = join(stem, "_ceiling.png"), *floor_path = join(stem, "_floor.png"), *sky_path = join(stem, "_sky.png");
    char *bump_path = join(stem, "_wall_bump.png");
    int rc = -1;
    RGBA32 *wall_px = NULL;
    Mesh3dInfo info = {0};
    if (!mtl_path || !atlas_path || !wall_path || !json_path || !ceiling_path || !floor_path || !sky_path || !bump_path) {
        wang_fail("Out of memory exporting %s", obj_path);
        goto done;
    }
    int cells = opt.texture_cells > 0 ? opt.texture_cells : 8;
    if (cells > 32) cells = 32;

    Mesh3dOptions m = opt.mesh;
    m.theme = th;
    m.wall_texture = NULL;
    m.floor_tiles = opt.floor_tiles;

    // Floor: a texture built from a tileset, or the atlas whose tiles follow the maze paths.
    if (opt.floor_texture) {
        if (make_surface_files(opt.floor_texture, &opt, opt.seed + 11u, cells, floor_path, NULL) != 0) goto done;
        m.texture = base_name(floor_path);
        m.floor_period = (float)cells;
    } else {
        // The floor samples the atlas, so it works for grids too big to render as one image.
        if (wang_atlas_save(atlas, atlas_path) != 0) goto done;
        m.texture = base_name(atlas_path);
    }

    // Walls (and cliffs): a tileset texture, else the theme's own brick/leaf/metal/... texture.
    float relief = opt.relief >= 0 ? opt.relief : auto_relief(opt.wall_texture, th);
    if (opt.wall_texture) {
        WorldOptions wall_opt = opt;
        if (wall_opt.texture_px <= 0) wall_opt.texture_px = 128; // walls are seen up close
        if (make_surface_files(opt.wall_texture, &wall_opt, opt.seed + 23u, cells, wall_path, relief > 0 ? bump_path : NULL) != 0) goto done;
        m.wall_texture = base_name(wall_path);
        m.wall_period = (float)cells;
    } else if (th) {
        wall_px = malloc(sizeof(RGBA32) * WALL_TEX_PX * WALL_TEX_PX);
        if (!wall_px) { wang_fail("Out of memory exporting %s", obj_path); goto done; }
        theme_generate_wall(th, wall_px);
        if (wang_save_image(wall_path, wall_px, WALL_TEX_PX, WALL_TEX_PX) != 0) goto done;
        if (relief > 0 && wang_make_bump(wall_px, WALL_TEX_PX, WALL_TEX_PX, bump_path) != 0) goto done;
        m.wall_texture = base_name(wall_path);
    }
    if (m.wall_texture && relief > 0) {
        m.wall_bump = base_name(bump_path);
        m.bump_strength = relief;
    }
    if (opt.ceiling_texture) {
        if (make_surface_files(opt.ceiling_texture, &opt, opt.seed + 37u, cells, ceiling_path, NULL) != 0) goto done;
        m.ceiling_texture = base_name(ceiling_path);
        m.ceiling_period = (float)cells;
    }
    const char *sky_file = NULL;
    if (opt.sky_texture) {
        if (make_surface_files(opt.sky_texture, &opt, opt.seed + 53u, cells * 2 > 32 ? 32 : cells * 2, sky_path, NULL) != 0) goto done;
        sky_file = base_name(sky_path);
    }
    m.ceiling = opt.ceiling;
    m.markers = opt.markers < 0 ? (th != NULL) : opt.markers;
    m.props_pct = opt.props_pct >= 0 ? opt.props_pct : (th ? th->default_props_pct : 0.0f);
    m.water_pct = opt.water_pct >= 0 ? opt.water_pct : (th ? th->default_water_pct : 0.0f);
    m.wall_light_pct = opt.wall_light_pct >= 0 ? opt.wall_light_pct : (th ? th->default_wall_light_pct : 0.0f);
    m.seed = opt.seed;

    if (wang_export_obj(tiles, w, h, obj_path, mtl_path, &m, &info) != 0) goto done;
    if (write_json(json_path, th, w, h, &opt, &info, sky_file, tiles) != 0) goto done;
    printf("wrote %s, %s and %s\n", obj_path, mtl_path, json_path);
    printf("textures: floor %s, walls %s%s%s\n", m.texture, m.wall_texture ? m.wall_texture : "(flat colour)",
           m.ceiling_texture ? ", ceiling " : "", m.ceiling_texture ? m.ceiling_texture : "");
    if (sky_file) printf("sky: %s\n", sky_file);
    rc = 0;

done:
    mesh3d_info_free(&info);
    free(wall_px);
    free(stem); free(mtl_path); free(atlas_path); free(wall_path); free(json_path);
    free(ceiling_path); free(floor_path); free(sky_path); free(bump_path);
    return rc;
}

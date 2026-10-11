#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wang.h"
#include "world.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Grid\n"
        "  --atlas FILE        256x256 atlas image (4x4 tiles, see reference.png)\n"
        "  --procedural NAME   generate the atlas instead: blobs (default) or roads\n"
        "  --size WxH          grid size in tiles (default 10x10)\n"
        "  --seed N            RNG seed (default: current time; printed on stdout)\n"
        "  --out FILE          grid image to write (default grid.png)\n"
        "  --atlas-out FILE    also save the atlas that was used\n"
        "  --tiles-out FILE    dump tile ids as hex digits, one grid row per line\n"
        "3D world\n"
        "  --obj FILE          export a 3D world: FILE (.obj), .mtl, _atlas.png, _wall.png and .json\n"
        "  --theme NAME        %s (default classic)\n"
        "                      a theme also picks the atlas, unless --atlas or --procedural is given\n"
        "  --ceiling           add a roof over the maze\n"
        "  --props PCT         percent of cells with a prop (default depends on the theme)\n"
        "  --water PCT         percent of cells that are water pools\n"
        "  --lights PCT        percent of cells with a torch/lantern/light strip on a wall\n"
        "  --markers           add start and goal markers (default for themes)\n"
        "  --no-markers        leave them out\n"
        "  --wall-height F     wall height in cell units (default 0.6)\n"
        "  --wall-thickness F  wall thickness in cell units (default 0.1)\n"
        "  --terrain           lift cell floors by number of open edges\n",
        prog, theme_name_list());
}

static int parse_uint(const char *s, unsigned long max, unsigned long *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > max) return -1;
    *out = v;
    return 0;
}

static int parse_float(const char *s, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != '\0') return -1;
    *out = v;
    return 0;
}

static int parse_percent(const char *s, float *out)
{
    return parse_float(s, out) || *out < 0.0f || *out > 100.0f ? -1 : 0;
}

int main(int argc, char **argv)
{
    const char *atlas_path = NULL, *out_path = "grid.png", *atlas_out = NULL, *tiles_out = NULL, *obj_path = NULL;
    const char *theme_name = "classic";
    WangShader shader = WANG_SHADER_BLOBS;
    int procedural_set = 0;
    unsigned long w = 10, h = 10, seed = 0;
    int have_seed = 0;
    WorldOptions world = world_default_options();

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--terrain")) { world.mesh.terrain = 1; continue; }
        if (!strcmp(a, "--ceiling")) { world.ceiling = 1; continue; }
        if (!strcmp(a, "--markers")) { world.markers = 1; continue; }
        if (!strcmp(a, "--no-markers")) { world.markers = 0; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        if (!val) { fprintf(stderr, "Missing value for %s\n", a); usage(argv[0]); return 2; }
        ++i;

        if (!strcmp(a, "--atlas")) atlas_path = val;
        else if (!strcmp(a, "--procedural")) {
            if (!strcmp(val, "blobs")) shader = WANG_SHADER_BLOBS;
            else if (!strcmp(val, "roads")) shader = WANG_SHADER_ROADS;
            else { fprintf(stderr, "Unknown procedural atlas '%s'\n", val); return 2; }
            procedural_set = 1;
        }
        else if (!strcmp(a, "--theme")) {
            if (!theme_is_known(val)) { fprintf(stderr, "Unknown theme '%s' (choose: %s)\n", val, theme_name_list()); return 2; }
            theme_name = val;
        }
        else if (!strcmp(a, "--size")) {
            char *x = strchr(val, 'x');
            char *wstr = x ? strndup(val, (size_t)(x - val)) : NULL;
            if (!x || !wstr || parse_uint(wstr, WANG_MAX_GRID_TL, &w) || parse_uint(x + 1, WANG_MAX_GRID_TL, &h) || !w || !h) {
                fprintf(stderr, "Bad --size '%s' (expected WxH, 1..%d)\n", val, WANG_MAX_GRID_TL);
                free(wstr);
                return 2;
            }
            free(wstr);
        }
        else if (!strcmp(a, "--seed")) {
            if (parse_uint(val, 0xFFFFFFFFul, &seed)) { fprintf(stderr, "Bad --seed '%s'\n", val); return 2; }
            have_seed = 1;
        }
        else if (!strcmp(a, "--out")) out_path = val;
        else if (!strcmp(a, "--atlas-out")) atlas_out = val;
        else if (!strcmp(a, "--tiles-out")) tiles_out = val;
        else if (!strcmp(a, "--obj")) obj_path = val;
        else if (!strcmp(a, "--props")) {
            if (parse_percent(val, &world.props_pct)) { fprintf(stderr, "Bad --props '%s' (0..100)\n", val); return 2; }
        }
        else if (!strcmp(a, "--water")) {
            if (parse_percent(val, &world.water_pct)) { fprintf(stderr, "Bad --water '%s' (0..100)\n", val); return 2; }
        }
        else if (!strcmp(a, "--lights")) {
            if (parse_percent(val, &world.wall_light_pct)) { fprintf(stderr, "Bad --lights '%s' (0..100)\n", val); return 2; }
        }
        else if (!strcmp(a, "--wall-height")) {
            if (parse_float(val, &world.mesh.wall_height)) { fprintf(stderr, "Bad --wall-height '%s'\n", val); return 2; }
        }
        else if (!strcmp(a, "--wall-thickness")) {
            if (parse_float(val, &world.mesh.wall_thickness)) { fprintf(stderr, "Bad --wall-thickness '%s'\n", val); return 2; }
        }
        else { fprintf(stderr, "Unknown option %s\n", a); usage(argv[0]); return 2; }
    }
    if (!have_seed) seed = (unsigned long)time(NULL) & 0xFFFFFFFFul;
    world.seed = (uint32_t)seed;
    world.theme = theme_by_name(theme_name); // NULL for "classic"

    RGBA32 *atlas = malloc(sizeof(RGBA32) * WANG_ATLAS_PX * WANG_ATLAS_PX);
    BLTR *tiles = malloc(w * h);
    if (!atlas || !tiles) { fprintf(stderr, "Out of memory\n"); return 1; }

    // Floor atlas: --atlas beats --procedural beats the theme's own atlas.
    if (atlas_path) {
        if (wang_atlas_load(atlas, atlas_path)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }
    } else if (world.theme && !procedural_set) {
        theme_generate_atlas(world.theme, atlas);
    } else {
        wang_atlas_generate(atlas, shader);
    }
    if (atlas_out && wang_atlas_save(atlas, atlas_out)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }

    if (wang_generate_grid(w, h, (uint32_t)seed, tiles)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }
    printf("seed %lu, grid %lux%lu, theme %s\n", seed, w, h, theme_name);

    if (tiles_out) {
        FILE *f = fopen(tiles_out, "w");
        if (!f) { fprintf(stderr, "Error: could not write '%s'\n", tiles_out); return 1; }
        for (size_t y = 0; y < h; ++y) {
            for (size_t x = 0; x < w; ++x) fputc("0123456789abcdef"[tiles[y * w + x] & 15], f);
            fputc('\n', f);
        }
        fclose(f);
    }

    // The grid image is skipped only when the grid is too big to render but a 3D export was asked for.
    if (w * h <= WANG_MAX_RENDER_TL) {
        RGBA32 *px = wang_render_grid(tiles, w, h, atlas);
        if (!px || wang_save_image(out_path, px, w * WANG_TILE_PX, h * WANG_TILE_PX)) {
            fprintf(stderr, "Error: %s\n", wang_last_error());
            return 1;
        }
        free(px);
        printf("wrote %s\n", out_path);
    } else if (!obj_path) {
        fprintf(stderr, "Error: grid too large to render to an image (max %d tiles)\n", WANG_MAX_RENDER_TL);
        return 1;
    } else {
        printf("grid image skipped: more than %d tiles (the 3D export does not need it)\n", WANG_MAX_RENDER_TL);
    }

    if (obj_path && wang_export_world(tiles, w, h, atlas, obj_path, &world)) {
        fprintf(stderr, "Error: %s\n", wang_last_error());
        return 1;
    }

    free(atlas);
    free(tiles);
    return 0;
}

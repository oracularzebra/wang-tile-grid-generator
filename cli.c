#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wang.h"
#include "mesh3d.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --atlas FILE        256x256 atlas image (4x4 tiles, see reference.png)\n"
        "  --procedural NAME   generate the atlas instead: blobs (default) or roads\n"
        "  --size WxH          grid size in tiles (default 10x10)\n"
        "  --seed N            RNG seed (default: current time; printed on stdout)\n"
        "  --out FILE          grid image to write (default grid.png)\n"
        "  --atlas-out FILE    also save the atlas that was used\n"
        "  --tiles-out FILE    dump tile ids as hex digits, one grid row per line\n"
        "  --obj FILE          export a 3D maze/terrain (.obj + .mtl next to it)\n"
        "  --wall-height F     3D wall height in cell units (default 0.6)\n"
        "  --wall-thickness F  3D wall thickness in cell units (default 0.1)\n"
        "  --terrain           3D: lift cell floors by number of open edges\n",
        prog);
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

static char *replace_ext(const char *path, const char *ext)
{
    size_t n = strlen(path);
    char *out = malloc(n + strlen(ext) + 1);
    if (!out) return NULL;
    strcpy(out, path);
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    strcat(out, ext);
    return out;
}

int main(int argc, char **argv)
{
    const char *atlas_path = NULL, *out_path = "grid.png", *atlas_out = NULL, *tiles_out = NULL, *obj_path = NULL;
    WangShader shader = WANG_SHADER_BLOBS;
    unsigned long w = 10, h = 10, seed = 0;
    int have_seed = 0;
    Mesh3dOptions mesh = mesh3d_default_options();

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--terrain")) { mesh.terrain = 1; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        if (!val) { fprintf(stderr, "Missing value for %s\n", a); usage(argv[0]); return 2; }
        ++i;

        if (!strcmp(a, "--atlas")) atlas_path = val;
        else if (!strcmp(a, "--procedural")) {
            if (!strcmp(val, "blobs")) shader = WANG_SHADER_BLOBS;
            else if (!strcmp(val, "roads")) shader = WANG_SHADER_ROADS;
            else { fprintf(stderr, "Unknown procedural atlas '%s'\n", val); return 2; }
            atlas_path = NULL;
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
        else if (!strcmp(a, "--wall-height")) {
            if (parse_float(val, &mesh.wall_height)) { fprintf(stderr, "Bad --wall-height '%s'\n", val); return 2; }
        }
        else if (!strcmp(a, "--wall-thickness")) {
            if (parse_float(val, &mesh.wall_thickness)) { fprintf(stderr, "Bad --wall-thickness '%s'\n", val); return 2; }
        }
        else { fprintf(stderr, "Unknown option %s\n", a); usage(argv[0]); return 2; }
    }
    if (!have_seed) seed = (unsigned long)time(NULL) & 0xFFFFFFFFul;

    RGBA32 *atlas = malloc(sizeof(RGBA32) * WANG_ATLAS_PX * WANG_ATLAS_PX);
    BLTR *tiles = malloc(w * h);
    if (!atlas || !tiles) { fprintf(stderr, "Out of memory\n"); return 1; }

    if (atlas_path) {
        if (wang_atlas_load(atlas, atlas_path)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }
    } else {
        wang_atlas_generate(atlas, shader);
    }
    if (atlas_out && wang_atlas_save(atlas, atlas_out)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }

    if (wang_generate_grid(w, h, (uint32_t)seed, tiles)) { fprintf(stderr, "Error: %s\n", wang_last_error()); return 1; }
    printf("seed %lu, grid %lux%lu\n", seed, w, h);

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
    int render_image = w * h <= WANG_MAX_RENDER_TL;
    if (render_image) {
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
    }

    if (obj_path) {
        char *mtl_path = replace_ext(obj_path, ".mtl");
        if (!mtl_path) { fprintf(stderr, "Out of memory\n"); return 1; }
        const char *slash = strrchr(out_path, '/');
        mesh.texture = render_image ? (slash ? slash + 1 : out_path) : NULL; // keep the png next to the obj
        if (wang_export_obj(tiles, w, h, obj_path, mtl_path, &mesh)) {
            fprintf(stderr, "Error: %s\n", wang_last_error());
            return 1;
        }
        printf("wrote %s and %s\n", obj_path, mtl_path);
        free(mtl_path);
    }

    free(atlas);
    free(tiles);
    return 0;
}

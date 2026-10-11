#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

WorldOptions world_default_options(void)
{
    WorldOptions o = {0};
    o.mesh = mesh3d_default_options();
    o.markers = -1;
    o.props_pct = o.water_pct = o.wall_light_pct = -1.0f;
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

static void json_rgb(FILE *f, const float c[3])
{
    fprintf(f, "[%g, %g, %g]", c[0], c[1], c[2]);
}

static int write_json(const char *path, const Theme *th, size_t w, size_t h, const WorldOptions *opt,
                      const Mesh3dInfo *info)
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
    fprintf(f, "  \"ambient\": %g,\n  \"sun\": %g,\n", th ? th->ambient : 1.0f, th ? th->sun : 1.6f);
    fprintf(f, "  \"terrain\": %s,\n", opt->mesh.terrain ? "true" : "false");

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
    int rc = -1;
    RGBA32 *wall_px = NULL;
    Mesh3dInfo info = {0};
    if (!mtl_path || !atlas_path || !wall_path || !json_path) { wang_fail("Out of memory exporting %s", obj_path); goto done; }

    // The floor samples the atlas, so it works for grids too big to render as one image.
    if (wang_atlas_save(atlas, atlas_path) != 0) goto done;

    Mesh3dOptions m = opt.mesh;
    m.theme = th;
    m.texture = base_name(atlas_path);
    m.wall_texture = NULL;
    if (th) {
        wall_px = malloc(sizeof(RGBA32) * WALL_TEX_PX * WALL_TEX_PX);
        if (!wall_px) { wang_fail("Out of memory exporting %s", obj_path); goto done; }
        theme_generate_wall(th, wall_px);
        if (wang_save_image(wall_path, wall_px, WALL_TEX_PX, WALL_TEX_PX) != 0) goto done;
        m.wall_texture = base_name(wall_path);
    }
    m.ceiling = opt.ceiling;
    m.markers = opt.markers < 0 ? (th != NULL) : opt.markers;
    m.props_pct = opt.props_pct >= 0 ? opt.props_pct : (th ? th->default_props_pct : 0.0f);
    m.water_pct = opt.water_pct >= 0 ? opt.water_pct : (th ? th->default_water_pct : 0.0f);
    m.wall_light_pct = opt.wall_light_pct >= 0 ? opt.wall_light_pct : (th ? th->default_wall_light_pct : 0.0f);
    m.seed = opt.seed;

    if (wang_export_obj(tiles, w, h, obj_path, mtl_path, &m, &info) != 0) goto done;
    if (write_json(json_path, th, w, h, &opt, &info) != 0) goto done;
    printf("wrote %s, %s, %s%s%s and %s\n", obj_path, mtl_path, atlas_path, th ? ", " : "", th ? wall_path : "", json_path);
    rc = 0;

done:
    mesh3d_info_free(&info);
    free(wall_px);
    free(stem); free(mtl_path); free(atlas_path); free(wall_path); free(json_path);
    return rc;
}

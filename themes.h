#ifndef THEMES_H_
#define THEMES_H_

#include "wang.h"

#define WALL_TEX_PX 128

typedef enum {
    PROP_PILLAR,
    PROP_CRATE,
    PROP_TREE,
    PROP_BUSH,
    PROP_CACTUS,
    PROP_ROCK,
    PROP_CRYSTAL,
    PROP_PYLON,
    PROP_MOUND,
} PropKind;

typedef enum {
    WALL_LIGHT_NONE,
    WALL_LIGHT_TORCH,   // bracket + flame on a wall
    WALL_LIGHT_LANTERN, // post + glowing lantern against a wall
    WALL_LIGHT_STRIP,   // glowing light strip on a wall
} WallLight;

typedef struct {
    const char *name;
    const char *description;
    float sky_top[3], sky_bottom[3], fog[3];
    float fog_density;
    float ambient;                // viewer ambient light multiplier
    float sun;                    // viewer directional light multiplier
    float water[3];
    float water_alpha;
    float light[3];               // colour of wall lights
    WallLight wall_light;
    const PropKind *props;
    int nprops;
    float default_props_pct;      // percent of cells that get a prop
    float default_water_pct;      // percent of cells that are water
    float default_wall_light_pct; // percent of cells that get a wall light
} Theme;

// Returns NULL for an unknown name and for "classic" (use theme_is_known to tell them apart).
const Theme *theme_by_name(const char *name);
int theme_is_known(const char *name);
// "classic, dungeon, hedge, ..." for usage text.
const char *theme_name_list(void);

// Edge-matched floor atlas: a periodic background plus a path along the open edges of every tile.
void theme_generate_atlas(const Theme *theme, RGBA32 *atlas);
// Tileable WALL_TEX_PX x WALL_TEX_PX wall texture.
void theme_generate_wall(const Theme *theme, RGBA32 *px);

#endif // THEMES_H_

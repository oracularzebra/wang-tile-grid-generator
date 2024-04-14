#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<errno.h>
#include<pthread.h>
#include<stdbool.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "./la.c"

#define TILE_WIDTH_PX 64
#define TILE_HEIGHT_PX 64

#define ATLAS_WIDTH_TL 4
#define ATLAS_HEIGHT_TL 4
#define ATLAS_WIDTH_PX (TILE_WIDTH_PX * ATLAS_WIDTH_TL)
#define ATLAS_HEIGHT_PX (TILE_HEIGHT_PX * ATLAS_HEIGHT_TL)

#define TILESET_WIDTH (TILE_WIDTH_PX*4)
#define TILESET_HEIGH (TILE_HEIGHT_PX*4)

#define GRID_WIDTH_TL 300
#define GRID_HEIGHT_TL 300
#define GRID_WIDTH_PX (GRID_WIDTH_TL*TILE_WIDTH_PX)
#define GRID_HEIGHT_PX (GRID_HEIGHT_TL*TILE_HEIGHT_PX)

typedef uint32_t RGBA32;
typedef uint8_t BLTR;
typedef RGB(*Frag_Shader)(BLTR bltr, UV uv); //Frag_Shader is a pointer to a fnction that returns RGB and accepts UV.

RGBA32 make_rgba32(float r, float g, float b)
{
    return (((uint32_t)(b * 255.0)) << 16) |
            (((uint32_t)(g * 255.0)) << 8) |
            (uint32_t)(r * 255.0)          | 0xFF000000;
}
RGB stripes(UV uv)
{
    float n = 20.0f;
    return vec3f((sinf(uv.c[U]*n)+1.0f)*0.5f,
                 (sinf((uv.c[U] + uv.c[V]) * n)+1.0f)*0.5f,
                 (sinf(uv.c[V]*n)+1.0f)*0.5f);
}

RGB japan(UV uv)
{
    float r = 0.25;
    int a = vec2f_sqrlen(vec2f_sub(vec2fs(0.5f), uv)) > r * r;
    return vec3f(1.0f, (float) a,(float) a);
}

RGB wang(BLTR bltr, UV uv)
{
    float r = 0.50;
    static const RGB colors[] = {
        {{0.4f, 0.1f, 0.3f}},
        {{0.5f, 1.0f, 1.0f}}
    };
    static_assert(sizeof(colors)/sizeof(colors[0]) == 2, "colors array must have exactly two elements");

    static const Vec2f sides[4] = {
        {{1.0f, 0.5f}}, //r
        {{0.5f, 0.0f}}, //t
        {{0.0f, 0.5f}}, //l
        {{0.5f, 1.0f}}, //b
    };

    RGB result = vec3fs(0.0f);
    for (size_t i=0;i<4;i++){
        Vec2f p = sides[i];
        float t = 1.0f - fminf(vec2f_len(vec2f_sub(p, uv)) / r, 1.0f);
        RGB c = colors[bltr & 1];
        result = vec3f_lerp(result, c, vec3fs(t));
        bltr = bltr >> 1;
    }
    return vec3f_pow(result, vec3fs(1.0f/2.2f)); 
}

RGB wang_roads(BLTR bltr, UV uv) {
    RGB railColor = {{0.1f, 0.1f, 0.1f}}; // Dark gray for rails
    RGB backgroundColor = {{1.0f, 1.0f, 1.0f}}; // White for background

    // Calculate tile center and rail width
    Vec2f center = {{0.5f, 0.5f}};
    float railWidth = 0.05f; // Rail width

    // Determine presence of tracks on each edge
    bool hasTop = bltr & 8;
    bool hasRight = bltr & 4;
    bool hasBottom = bltr & 2;
    bool hasLeft = bltr & 1;

    // Calculate distance to center for drawing straight lines
    float distanceToCenter = vec2f_len(vec2f_sub(uv, center));

    // Determine if the current point is near one of the tile edges or center for drawing
    bool nearVerticalCenter = fabs(uv.c[V] - center.c[V]) < railWidth / 2;
    bool nearHorizontalCenter = fabs(uv.c[U] - center.c[U]) < railWidth / 2;
    bool onVerticalRail = nearVerticalCenter && (hasLeft || hasRight);
    bool onHorizontalRail = nearHorizontalCenter && (hasTop || hasBottom);

    // Draw rails based on the edge flags
    if ((hasTop && uv.c[V] <= center.c[V] && nearHorizontalCenter) ||
        (hasBottom && uv.c[V] >= center.c[V] && nearHorizontalCenter) ||
        (hasLeft && uv.c[U] <= center.c[U] && nearVerticalCenter) ||
        (hasRight && uv.c[U] >= center.c[U] && nearVerticalCenter) ||
        (onVerticalRail && onHorizontalRail)) {
        return railColor;
    }

    return backgroundColor;
}

void generate_tile32(uint32_t *pixels, size_t width, size_t height, size_t stride, BLTR bltr, Frag_Shader shader)
{
    for(size_t y=0;y< height; ++y){
        for(size_t x=0;x< width;++x){
            float u = (float)x / (float)width;
            float v = (float)y / (float)height;
            RGB p = shader(bltr, vec2f(u, v));
            pixels[y * stride + x] = make_rgba32(p.c[R], p.c[G], p.c[B]);
        }
    }
}

RGBA32 tile[TILE_WIDTH_PX*TILE_HEIGHT_PX];
RGBA32 atlas[ATLAS_WIDTH_PX*ATLAS_HEIGHT_PX];
BLTR grid_tl[GRID_WIDTH_TL*GRID_HEIGHT_TL]; // The tile we generate
RGBA32 grid_px[GRID_WIDTH_PX*GRID_HEIGHT_PX]; //The thing we will save in the image

void *generate_tile_thread(void *arg)
{
    size_t bltr = (size_t) arg;
    size_t y = (bltr/ATLAS_WIDTH_TL)*TILE_WIDTH_PX;
    size_t x = (bltr%ATLAS_WIDTH_TL)*TILE_WIDTH_PX;
    printf("Generating tile %d\n", bltr);
    generate_tile32(
        &atlas[y*ATLAS_WIDTH_PX+x],
        TILE_WIDTH_PX, TILE_HEIGHT_PX, ATLAS_WIDTH_PX,
        bltr, wang);

    return NULL;
}

//v - value mask , p - position mask  , candidate tile
BLTR rand_tile(BLTR v, BLTR p) 
{
    BLTR cs[16] = {0};
    size_t n = 0;

    for(BLTR c=0; c< 16;++c){
        if((c&p) == (v&p)){
            cs[n++] = c;
        }
    }

    return cs[rand() %n];
}

void copy_pixels32(RGBA32 *dst, size_t dst_stride, RGBA32 *src, size_t src_stride, size_t width, size_t height)
{
    while (height-- > 0){
        //copying row by row
        memcpy(dst, src, width * sizeof(RGBA32));
        dst += dst_stride;
        src += src_stride; 
    }
}
void load_tiles_from_file_into_atlas(const char* filename) {
    // Load the image
    int width, height, channels;
    RGBA32* loadedImage = (RGBA32*)stbi_load(filename, &width, &height, &channels, 4); // Force RGBA
    if (!loadedImage) {
        fprintf(stderr, "Error loading image\n");
        return;
    }

    // Assuming the sprite sheet is divided into a grid of tiles
    size_t tilesPerRow = width / TILE_WIDTH_PX;
    size_t tilesPerColumn = height / TILE_HEIGHT_PX;

    for(size_t tileY = 0; tileY < tilesPerColumn; ++tileY) {
        for(size_t tileX = 0; tileX < tilesPerRow; ++tileX) {
            // Calculate the starting pixel of the current tile in the loaded image
            size_t srcX = tileX * TILE_WIDTH_PX;
            size_t srcY = tileY * TILE_HEIGHT_PX;
            RGBA32* srcTile = &loadedImage[srcY * width + srcX];

            // Calculate the position to copy the tile into the atlas
            size_t atlasX = tileX * TILE_WIDTH_PX;
            size_t atlasY = tileY * TILE_HEIGHT_PX;
            RGBA32* dstTile = &atlas[atlasY * ATLAS_WIDTH_PX + atlasX];

            // Copy the tile from the loaded image into the correct position in the atlas
            copy_pixels32(dstTile, ATLAS_WIDTH_PX, srcTile, width, TILE_WIDTH_PX, TILE_HEIGHT_PX);
        }
    }

    // Free the loaded image
    stbi_image_free(loadedImage);
}
int main(){

#define THREADED
#ifdef THREADED

    // printf("Multi threaded mode\n");

    // pthread_t threads[16] = {0};

    // for(size_t i=0;i<16;++i){
    //     pthread_create(&threads[i], NULL, generate_tile_thread, (void*) i);
    // }

    // for(size_t i=0;i<16;++i){
    //     pthread_join(threads[i], NULL);
    // }

#else
    printf("Single threaded mode\n");

    for(BLTR bltr=0;bltr<16; bltr++){
        size_t y = (bltr/ATLAS_WIDTH_TL)*TILE_WIDTH_PX;
        size_t x = (bltr%ATLAS_WIDTH_TL)*TILE_WIDTH_PX;
        generate_tile32(
            &atlas[y*ATLAS_WIDTH_PX+x],
            TILE_WIDTH_PX, TILE_HEIGHT_PX, ATLAS_WIDTH_PX,
            bltr, wang);
    }    
#endif

    //Generating grid
    //First top left corner
    load_tiles_from_file_into_atlas("assets/walkway-small.jpg");
    grid_tl[0] = rand_tile(0,0);

    //First top row
    for(size_t x=1; x< GRID_WIDTH_TL; ++x){
        //p = bltr = 0b0100 = 1<<2

        BLTR v = (grid_tl[x-1] &1) << 2;
        BLTR p = 1 << 2;
        grid_tl[x] = rand_tile(v, p);
    }

    //First left column
    for(size_t y = 1; y< GRID_HEIGHT_TL; ++y){
    //p = bltr = 0b0010 = 1 << 1
    //g = grid[(y-1) * GRID_WIDTH_TL]
    //    bltr
    //g = abcd
    //g & 0b1000 = a000
    //(g & 0b1000) >> 2 = 00a0 , moving same bottom to top
    // v = (g & 8) >> 2
        BLTR v = (grid_tl[(y-1) * GRID_WIDTH_TL] & 8) >> 2;
        BLTR p = 1 << 1;
        grid_tl[y * GRID_WIDTH_TL] = rand_tile(v, p);
    }

    //The rest of the tiles
    for(size_t y = 1; y< GRID_HEIGHT_TL; ++y){
        for(size_t x = 1; x< GRID_WIDTH_TL; ++x){
            /*
                +----+
                | t  |
            ----+----+
            | l | g  |
            ----+----+

            p = bltr & 0b0110 = 0lt0
            v = 
            */
            BLTR lp = 1<<2;
            BLTR lv = (grid_tl[y*GRID_WIDTH_TL+x-1] & 1) << 2;
            BLTR tp = 1<<1;
            BLTR tv = (grid_tl[(y-1) * GRID_WIDTH_TL + x] & 8) >> 2;
            grid_tl[y * GRID_WIDTH_TL + x] = rand_tile(lv | tv, lp | tp);
            //v 
        }
    }

    //Rendering grid
    for(size_t gy_tl=0;gy_tl<GRID_HEIGHT_TL;++gy_tl){
        for(size_t gx_tl=0;gx_tl<GRID_WIDTH_TL;++gx_tl){
            BLTR bltr = grid_tl[gy_tl*GRID_HEIGHT_TL+gx_tl];
            size_t ax_tl = bltr % ATLAS_WIDTH_TL;
            size_t ay_tl = bltr / ATLAS_WIDTH_TL;

            size_t gx_px = gx_tl * TILE_WIDTH_PX;
            size_t gy_px = gy_tl * TILE_HEIGHT_PX;
            size_t ax_px = ax_tl * TILE_WIDTH_PX;
            size_t ay_px = ay_tl * TILE_HEIGHT_PX;
            
            copy_pixels32(&grid_px[gy_px * GRID_WIDTH_PX + gx_px], GRID_WIDTH_PX, &atlas[ay_px * ATLAS_WIDTH_PX + ax_px], ATLAS_WIDTH_PX, TILE_WIDTH_PX , TILE_HEIGHT_PX);
        }
    }

    //Saving atlas
    if(!stbi_write_png("atlas.png", ATLAS_WIDTH_PX, ATLAS_HEIGHT_PX, 4, atlas, ATLAS_WIDTH_PX*sizeof(RGBA32))){
        fprintf(stderr, "Error: Could not save file $s\n ", "atlas.png");
        exit(1);
    }

    //Saving grid
    if(!stbi_write_png("grid300x300.png", GRID_WIDTH_PX, GRID_HEIGHT_PX, 4, grid_px, GRID_WIDTH_PX*sizeof(RGBA32))){
        fprintf(stderr, "Error: Could not save file $s\n ", "grid.png");
        exit(1);
    }
    return 0;
}

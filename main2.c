#include<stdint.h>
#include<string.h>
#include<errno.h>
#include<stdbool.h>
#include<stddef.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "./la.c"
typedef uint32_t RGBA32;
typedef uint8_t BLTR;
#define TILE_WIDTH_PX 64
#define TILE_HEIGHT_PX 64
RGBA32 tile[TILE_WIDTH_PX*TILE_HEIGHT_PX];
#define ATLAS_WIDTH_TL 4
#define ATLAS_HEIGHT_TL 4
#define ATLAS_WIDTH_PX (TILE_WIDTH_PX * ATLAS_WIDTH_TL)
#define ATLAS_HEIGHT_PX (TILE_HEIGHT_PX * ATLAS_HEIGHT_TL)
RGBA32 atlas[ATLAS_WIDTH_PX*ATLAS_HEIGHT_PX];

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

bool generateGrid(size_t gw_tl, size_t gh_tl)
{
    const size_t GRID_WIDTH_TL=gw_tl;
    const size_t GRID_HEIGHT_TL=gh_tl;
    const size_t GRID_WIDTH_PX=GRID_WIDTH_TL*TILE_WIDTH_PX;
    const size_t GRID_HEIGHT_PX=GRID_HEIGHT_TL*TILE_HEIGHT_PX;
    BLTR grid_tl[GRID_WIDTH_TL*GRID_HEIGHT_TL];
    RGBA32 grid_px[GRID_WIDTH_PX*GRID_HEIGHT_PX];
    load_tiles_from_file_into_atlas("atlas.png");
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
    if(!stbi_write_png("grid.png", GRID_WIDTH_PX, GRID_HEIGHT_PX, 4, grid_px, GRID_WIDTH_PX*sizeof(RGBA32))){
        // fprintf(stderr, "Error: Could not save file $s\n ", "grid.png");
        exit(1);
    }
    return true;
}
# wang tilegrid generator

## Requirements
1. Gtk
2. C compiler
3. stb_image_write.h
4. stb_image.h

## How to generate wang tile

1. Comment the line load_tiles_from_file_into_atlas (line 239) function.
2. Uncomment the code between #ifdef THREADED and #else

## How to run CLI wang tile grid generator

In main change the image name in `load_tiles_from_file_into_atlas` function

`gcc -o main main.c -lm`


## How to run GUI wang tile grid generator
```
gcc `pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0` -o image_resizer making_gui2.c `pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0` -lm
```
`./image_resizer`

Select image of an atlas ( the order of tiles in the atlas should be pre defined check reference.png )

## Demo
### Input Grid
![pipes](https://github.com/user-attachments/assets/f83f2404-2973-4904-97d0-e130c2dfc11c)

### Output Grid
![photo_2025-02-08_11-11-14](https://github.com/user-attachments/assets/4003f1f4-be21-4189-9126-bf7fd69fea6c)

## References
https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html
https://tessellation.space/explore/

## Assets 
https://web.archive.org/web/20231111191627/http://cr31.co.uk/stagecast/wang/intro.html

###### Apologies for bad documentation

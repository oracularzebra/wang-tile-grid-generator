CC      ?= gcc
CFLAGS  ?= -O2
WARN    := -Wall -Wextra
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0 2>/dev/null)

CORE := wang.c mesh3d.c themes.c tileset.c world.c
DEPS := wang.h mesh3d.h themes.h tileset.h noise.h world.h la.c stb_image.h stb_image_write.h

.PHONY: all cli gui test worlds clean

all: cli

cli: wang_cli

wang_cli: cli.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(WARN) -o $@ cli.c $(CORE) -lm

gui: image_resizer

image_resizer: making_gui2.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -o $@ making_gui2.c $(CORE) $(GTK_LIBS) -lm

test: wang_cli
	python3 tests/run_tests.py

# One world per look, for the viewer's World dropdown (open the viewer after this).
worlds: wang_cli
	mkdir -p worlds
	./wang_cli --theme dungeon --size 24x16 --seed 7 --ceiling --out worlds/dungeon.png --obj worlds/dungeon.obj
	./wang_cli --theme dungeon --size 24x16 --seed 7 --ceiling --terrain --out worlds/dungeon_terrain.png --obj worlds/dungeon_terrain.obj
	./wang_cli --theme hedge --size 24x16 --seed 7 --sky-texture clouds --out worlds/hedge.png --obj worlds/hedge.obj
	./wang_cli --theme scifi --size 24x16 --seed 7 --ceiling --out worlds/scifi.png --obj worlds/scifi.obj
	./wang_cli --theme desert --size 24x16 --seed 7 --sky-texture sunset --out worlds/desert.png --obj worlds/desert.obj
	./wang_cli --theme ice --size 24x16 --seed 7 --sky-texture clouds --out worlds/ice.png --obj worlds/ice.obj
	./wang_cli --size 24x16 --seed 7 --out worlds/classic.png --obj worlds/classic.obj
	printf '[{"name":"Dungeon","file":"dungeon.obj"},{"name":"Dungeon (terrain)","file":"dungeon_terrain.obj"},{"name":"Hedge maze","file":"hedge.obj"},{"name":"Sci-fi","file":"scifi.obj"},{"name":"Desert","file":"desert.obj"},{"name":"Ice","file":"ice.obj"},{"name":"Classic","file":"classic.obj"}]\n' > worlds/index.json

clean:
	rm -f wang_cli image_resizer

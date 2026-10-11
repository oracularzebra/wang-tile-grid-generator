CC      ?= gcc
CFLAGS  ?= -O2
WARN    := -Wall -Wextra
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0 2>/dev/null)

CORE := wang.c mesh3d.c themes.c tileset.c world.c
DEPS := wang.h mesh3d.h themes.h tileset.h noise.h world.h la.c stb_image.h stb_image_write.h

.PHONY: all cli gui test clean

all: cli

cli: wang_cli

wang_cli: cli.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(WARN) -o $@ cli.c $(CORE) -lm

gui: image_resizer

image_resizer: making_gui2.c $(CORE) $(DEPS)
	$(CC) $(CFLAGS) $(WARN) $(GTK_CFLAGS) -o $@ making_gui2.c $(CORE) $(GTK_LIBS) -lm

test: wang_cli
	python3 tests/run_tests.py

clean:
	rm -f wang_cli image_resizer

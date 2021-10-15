WIREPLUMBER_DEBUG ?= 3

all:
	ninja -C build

install:
	ninja -C build install

uninstall:
	ninja -C build uninstall

clean:
	ninja -C build clean

run: all
	WIREPLUMBER_DEBUG=$(WIREPLUMBER_DEBUG) \
	./wp-uninstalled.sh $(DBG) wireplumber

test:
	meson test -C build

test_valgrind:
	meson test -C build --setup=valgrind

gdb:
	$(MAKE) run DBG=gdb

valgrind:
	G_SLICE=always-malloc \
	$(MAKE) run DBG="valgrind --leak-check=full"

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
	./wp-uninstalled.sh $(DBG) ./build/src/wireplumber

test: all
	ninja -C build test

gdb:
	$(MAKE) run DBG=gdb

valgrind:
	$(MAKE) run DBG=valgrind

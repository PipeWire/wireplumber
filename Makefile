all:
	ninja -C build

install:
	ninja -C build install

uninstall:
	ninja -C build uninstall

clean:
	ninja -C build clean

run: all
	WIREPLUMBER_MODULE_DIR=build/modules \
	WIREPLUMBER_CONFIG_FILE=src/config/wireplumber.conf \
	WIREPLUMBER_CONFIG_DIR=src/config \
	$(DBG) ./build/src/wireplumber

test: all
	ninja -C build test

gdb:
	$(MAKE) run DBG=gdb

valgrind:
	$(MAKE) run DBG="DISABLE_RTKIT=1 valgrind"

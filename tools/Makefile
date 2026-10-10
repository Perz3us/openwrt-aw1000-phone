ifeq ($(origin CC),default)
CC := aarch64-openwrt-linux-musl-gcc
endif

build/fxsctl: fxsctl.c ../kernel/aw1000-fxs/fxs_uapi.h
	mkdir -p build
	$(CC) -O2 -Wall -o $@ fxsctl.c

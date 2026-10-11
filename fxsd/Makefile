CFLAGS ?= -O2
WERROR ?= -Werror
override CFLAGS += -std=gnu11 -Wall -Wextra $(WERROR)
SRC := src/cid.c src/clcc.c src/line.c src/si3226x.c src/si_line.c src/patch.c src/aw1000_cfg.c src/bringup.c

all: build/fxsd build/fxs-bringup

build/fxsd: src/fxsd_main.c src/fxsdev.c src/modem.c $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/fxsd_main.c src/fxsdev.c src/modem.c $(SRC)

build/fxs-bringup: src/bringup_main.c src/fxsdev.c $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/bringup_main.c src/fxsdev.c $(SRC)

clean:
	rm -rf build

.PHONY: all clean

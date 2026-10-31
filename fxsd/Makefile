CFLAGS ?= -O2
WERROR ?= -Werror
override CFLAGS += -std=gnu11 -Wall -Wextra $(WERROR)
SRC := src/cid.c src/clcc.c src/line.c src/si3226x.c src/si_line.c src/patch.c src/aw1000_cfg.c src/bringup.c
FXSD_SRC := src/fxsd_main.c src/fxsdev.c src/modem.c src/voip.c src/voip_map.c src/pcmsock.c src/urc.c src/country.c
FXSD_LIBS := -lubus -lubox -lpthread

all: build/fxsd build/fxs-bringup

build/fxsd: $(FXSD_SRC) $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(FXSD_SRC) $(SRC) $(LDFLAGS) $(FXSD_LIBS)

build/fxs-bringup: src/bringup_main.c src/fxsdev.c $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/bringup_main.c src/fxsdev.c $(SRC)

test: tests/test_line.c src/line.c src/clcc.c $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/test_line tests/test_line.c src/line.c src/clcc.c
	./build/test_line

clean:
	rm -rf build

.PHONY: all clean test

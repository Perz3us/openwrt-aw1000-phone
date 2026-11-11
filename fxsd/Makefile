CFLAGS ?= -O2
WERROR ?= -Werror
override CFLAGS += -std=gnu11 -Wall -Wextra $(WERROR)
SRC := src/cid.c src/clcc.c src/line.c src/si3226x.c src/si_line.c src/patch.c src/aw1000_cfg.c src/bringup.c
FXSD_SRC := src/fxsd_main.c src/fxsdev.c src/modem.c src/voip.c src/voip_map.c src/sipmap.c src/pcmsock.c src/urc.c src/country.c src/netsvc.c src/rules.c src/schedule.c src/contacts.c src/alarm.c src/rec.c src/wav.c src/recorder.c
FXSD_LIBS := -lubus -lubox -lpthread

all: build/fxsd build/fxs-bringup

build/fxsd: $(FXSD_SRC) $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(FXSD_SRC) $(SRC) $(LDFLAGS) $(FXSD_LIBS)

build/fxs-bringup: src/bringup_main.c src/fxsdev.c $(SRC) $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/bringup_main.c src/fxsdev.c $(SRC)

test: tests/test_line.c src/line.c src/clcc.c src/rules.c $(wildcard src/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/test_line tests/test_line.c src/line.c src/clcc.c src/rules.c
	./build/test_line
	$(CC) $(CFLAGS) -o build/test_sipmap tests/test_sipmap.c src/sipmap.c
	./build/test_sipmap
	$(CC) $(CFLAGS) -o build/test_voip_map tests/test_voip_map.c src/voip_map.c
	./build/test_voip_map
	$(CC) $(CFLAGS) -o build/test_pcmsock tests/test_pcmsock.c src/pcmsock.c src/recorder.c src/wav.c -lpthread
	./build/test_pcmsock
	$(CC) $(CFLAGS) -o build/test_netsvc tests/test_netsvc.c src/netsvc.c
	./build/test_netsvc
	$(CC) $(CFLAGS) -o build/test_schedule tests/test_schedule.c src/schedule.c
	./build/test_schedule
	$(CC) $(CFLAGS) -o build/test_contacts tests/test_contacts.c src/contacts.c src/cid.c src/country.c src/si_line.c src/si3226x.c src/aw1000_cfg.c
	./build/test_contacts
	$(CC) $(CFLAGS) -o build/test_alarm tests/test_alarm.c src/alarm.c
	./build/test_alarm
	$(CC) $(CFLAGS) -o build/test_rec tests/test_rec.c src/rec.c src/contacts.c src/cid.c src/country.c src/si_line.c src/si3226x.c src/aw1000_cfg.c
	./build/test_rec
	$(CC) $(CFLAGS) -o build/test_wav tests/test_wav.c src/wav.c src/recorder.c -lpthread
	./build/test_wav
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-format-truncation -Wno-unused-result -o build/test_modem tests/test_modem.c
	./build/test_modem
	$(CC) $(CFLAGS) -o build/test_modem_audio tests/test_modem_audio.c src/bringup.c src/si3226x.c src/si_line.c src/aw1000_cfg.c
	./build/test_modem_audio

clean:
	rm -rf build

.PHONY: all clean test

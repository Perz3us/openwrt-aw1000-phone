# openwrt-aw1000-phone

Makes the RJ11 phone port on the Arcadyan AW1000 work on OpenWrt.

The AW1000 has a phone port on the back, but it only works on the stock Telstra firmware.
After moving to OpenWrt it was just a dead socket, so I reverse engineered the hardware and
wrote a driver for it. Now I can plug a normal desk phone into the router and make and take
calls with the SIM in the 5G modem.

There are three packages:

- `kmod-aw1000-fxs` kernel driver for the SLIC chip and the PCM audio bus
- `fxsd` daemon that handles the line (ringing, dial tone, digits) and talks to the modem
- `luci-app-fxs` LuCI pages for status, settings, speed dial and blocked numbers

## What works

Incoming and outgoing calls work. The phone rings when someone calls the SIM, you pick up and
talk. To call out, pick up, wait for the dial tone and dial. It calls when you press `#` or
after you stop dialing for 4 seconds. If the other person hangs up you get a busy tone. It
starts by itself after a reboot.

Other things that work:

- Caller ID on phones with a display (FSK Bellcore, FSK ETSI or DTMF)
- Ringing tone in the handset while the other side rings
- Hold: press FLASH during a call, press it again to come back
- Speed dial: save a number under a code and dial `*1`, `*2` and so on
- Blocked numbers, including hidden numbers if you want
- The phone LED on the router is on while the port is on
- Line test in LuCI with line voltage, loop current and a button to ring the phone

Call waiting is in the code but did not work on my SIM, the network answers busy for the
second call. Pulse dialing and tones for other countries are not done yet (dial and busy
tone are 425 Hz).

## What you need

- AW1000 with the Quectel RG500Q-EA modem
- OpenWrt 25.12. I use [NoobWRT](https://github.com/nooblk-98/noobwrt-arcadyan-aw1k) 25.12.5
- QModem (fxsd sends AT commands through its at-daemon)
- A SIM with voice calls (VoLTE)
- The SLIC DSP patch file, see below

The kernel module has to be built for the exact kernel your firmware uses. The packages in
the release are for NoobWRT 25.12.5. For other firmware, build them yourself (see Building).

## Install

Copy the apk files from the release to the router and install them:

```
apk add --allow-untrusted kmod-aw1000-fxs-*.apk fxsd-*.apk luci-app-fxs-*.apk
```

Put the modem audio interface in slave mode. You only do this once, the modem remembers it.
The modem restarts, so the internet drops for a minute.

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QDAI=x,1,0,4,0,0,1,1","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

(To go back to the factory setting use `AT+QDAI=x,0,0,4,0,0,1,1` and restart the modem again.)

Copy the patch file to `/etc/fxs/si3226x_patch.bin`, then turn the port on in LuCI
(Services > Phone (RJ11)) or run:

```
/etc/init.d/fxsd enable
/etc/init.d/fxsd start
```

## SLIC patch file

The SLIC chip needs a DSP patch from Skyworks every time it starts. It is their code, so I
can't put it in this repo. If you have the ProSLIC API source, convert
`si3226x_patch_C_FB_2017MAY26.c` (the same patch the stock firmware uses) with:

```
python3 tools/mkpatch.py si3226x_patch_C_FB_2017MAY26.c si3226x_patch.bin
```

## Settings

Everything can be set in LuCI under Services > Phone (RJ11). It has four tabs: Status,
Settings, Speed dial and Blocked numbers. The same settings are in `/etc/config/fxs`.

| Option | Default | |
|---|---|---|
| `enabled` | `1` | turn the port on or off |
| `own_number` | | your number, shown on the Status page |
| `tx_gain` | `-6` | your voice level to the other side (dB) |
| `rx_gain` | `0` | the other side's level to your phone (dB) |
| `volume` | `5` | modem speaker volume (`AT+CLVL`) |
| `modem_rx_gain` | `8192` | modem `AT+QRXGAIN` |
| `channel` | `1` | SLIC channel, leave it at 1 |
| `at_port` | `/dev/ttyUSB2` | modem AT port |
| `patch` | `/etc/fxs/si3226x_patch.bin` | patch file |
| `ringback` | `1` | ringing tone while the other side rings |
| `callerid` | `bellcore` | caller ID format: bellcore, etsi, dtmf or off |
| `led` | `green:phone` | LED that is on while the port is on, empty to leave it alone |
| `block_private` | `0` | reject calls without a number |

Speed dial entries are `config speed` sections with `code` and `number`, blocked numbers are
`config block` sections with `number`. Numbers are matched on the last 9 digits, so
`0771234567` also blocks `+94771234567`.

Logs: `logread -e fxsd`. Call log: `/etc/fxs/calls.log`.

If the router reboots while fxsd is starting up, it won't start again on that boot, just in
case it was the cause. Run `/etc/init.d/fxsd clear_guard` or press the button in LuCI.

## Building

You need Docker. The first run downloads the OpenWrt SDK, which takes a while.

```
./build.sh
```

That builds for official OpenWrt 25.12.5. For NoobWRT add your kernel hash, which is the part
after `~` in `apk list --installed kernel` on the router:

```
./build.sh ac1a6be367e722eec0e2a22d138a1127
```

The packages end up in `dist/`.

## Notes

The interesting part was the audio. The modem and the SLIC are on the same PCM bus but can't
hear each other, so the driver runs the IPQ8074 PCM block as the bus master and copies the
samples between them. My notes on the pinout, clocks, PCM timing and the registers that
crash the router are in [docs/hardware.md](docs/hardware.md).

Be careful with the RJ11 contacts, there is about 46 V on the line and more when it rings.
Don't rely on this for emergency calls.

Not affiliated with Arcadyan, Telstra, Quectel or Skyworks.

If you try it on your AW1000, let me know how it went in an issue. Pull requests are welcome.

## License

Copyright (C) 2026 perz3us. Licensed under GPL-2.0, see `LICENSE`.

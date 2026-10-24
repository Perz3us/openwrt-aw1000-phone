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

- HD calling (16 kHz wideband audio), off by default, turn it on in Settings
- Caller ID on phones with a display (FSK Bellcore, FSK ETSI or DTMF, before or after the first ring)
- Ringing tone in the handset while the other side rings
- Hold: press FLASH during a call, press it again to come back
- Speed dial: save a number under a code and dial `*1`, `*2` and so on
- Blocked numbers, including hidden numbers if you want
- The phone LED shows the line state: solid when ready, fast blink when a call comes in, slow
  blink while the handset is off hook (each can be changed)
- Line test in LuCI with line voltage, loop current and a button to ring the phone
- Call history with search, filters, CSV export and optional auto delete

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

Download the three apk files from the latest release to `/tmp` on the router (or copy them
there with scp) and install them:

```
cd /tmp
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.4.0/kmod-aw1000-fxs-6.12.94.0.2.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.4.0/fxsd-0.4.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.4.0/luci-app-fxs-0.4.0-r1.apk
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.2.0-r1.apk /tmp/fxsd-0.4.0-r1.apk /tmp/luci-app-fxs-0.4.0-r1.apk
```

When updating, always update all three together (HD calling needs the 0.2.0 driver), then run
`/etc/init.d/fxsd restart` and reload LuCI with Ctrl+F5.

Put the modem audio interface in slave mode. You only do this once, the modem remembers it.
The modem restarts, so the internet drops for a minute.

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QDAI=x,1,0,4,0,0,1,1","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

(To go back to the factory setting use `AT+QDAI=x,0,0,4,0,0,1,1` and restart the modem again.
You don't need to change the sample rate yourself for HD calling, fxsd does that.)

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

Everything can be set in LuCI under Services > Phone (RJ11). It has five tabs: Status,
Call history, Settings, Speed dial and Blocked numbers. The same settings are in `/etc/config/fxs`.

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
| `hd` | `0` | HD calling (16 kHz). Changing it restarts the modem, mobile data drops for about 30 seconds |
| `callerid` | `bellcore` | caller ID format: bellcore, etsi, dtmf or off |
| `callerid_timing` | `after` | send caller ID `after` the first ring (North America) or `before` it (most of Europe) |
| `led` | `green:phone` | LED to use, empty to leave it alone |
| `led_ready` | `on` | LED when the line is ready: on, slow, fast or off |
| `led_ringing` | `fast` | LED while a call comes in |
| `led_offhook` | `slow` | LED while the handset is off hook |
| `block_private` | `0` | reject calls without a number |
| `history_autodelete` | `0` | delete old calls from the call history |
| `history_days` | `30` | how many days of calls to keep when that is on |

Speed dial entries are `config speed` sections with `code` and `number`, blocked numbers are
`config block` sections with `number`. Numbers are matched on the last 9 digits, so
`0771234567` also blocks `+94771234567`.

Logs: `logread -e fxsd`. Call log: `/etc/fxs/calls.log`.

If the router reboots while fxsd is starting up, it won't start again on that boot, just in
case it was the cause. Run `/etc/init.d/fxsd clear_guard` or press the button in LuCI.

## Problems

Calls fail straight away and the log shows `call failed`: check that the modem has voice calls
turned on. Some modems come with `call_control` set to `1,1`, which blocks all calls. fxsd
warns about this in the log and on the Status page.

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\"call_control\"","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\"call_control\",0,0","timeout":3}'
```

On official OpenWrt, ModemManager also reads the modem AT port and takes the answers meant for
fxsd. Stop it or remove it if calls are not picked up.

The phone does not ring or LuCI says "Modem audio not set to slave mode": run the `AT+QDAI`
step from Install again and restart the modem.

Calls sound harsh or buzzy with HD calling on: the kernel driver is older than 0.2.0. Install
the kmod package from the same release, or turn HD calling off.

New tabs or settings do not show up after an update: the browser has the old pages cached,
press Ctrl+F5 on the page.

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

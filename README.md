<h1 align="center">openwrt-aw1000-phone</h1>

<p align="center">A working RJ11 phone port for the Arcadyan AW1000 on OpenWrt</p>

<p align="center">
  <a href="https://github.com/Perz3us/openwrt-aw1000-phone/releases/latest"><img src="https://img.shields.io/github/v/release/Perz3us/openwrt-aw1000-phone?label=release" alt="Release"></a>
  <a href="https://github.com/Perz3us/openwrt-aw1000-phone/releases"><img src="https://img.shields.io/github/downloads/Perz3us/openwrt-aw1000-phone/total?label=downloads" alt="Downloads"></a>
  <a href="https://github.com/Perz3us/openwrt-aw1000-phone/issues"><img src="https://img.shields.io/github/issues/Perz3us/openwrt-aw1000-phone?label=issues" alt="Issues"></a>
  <img src="https://img.shields.io/badge/router-Arcadyan%20AW1000-0a7bbb" alt="Router">
  <a href="https://openwrt.org"><img src="https://img.shields.io/badge/OpenWrt-25.12-00b5e2?logo=openwrt&logoColor=white" alt="OpenWrt"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Perz3us/openwrt-aw1000-phone" alt="License"></a>
</p>

The AW1000 has a phone port on the back, but it only works on the stock Telstra firmware.
After moving to OpenWrt it was just a dead socket, so I reverse engineered the hardware and
wrote a driver for it. Now I can plug a normal desk phone into the router and make and take
calls with the SIM in the 5G modem.

It comes as three packages: `kmod-aw1000-fxs`, the kernel driver, `fxsd`, the daemon that
runs the line and talks to the modem, and `luci-app-fxs`, the LuCI pages.

## What it does

Pick up, hear a dial tone, dial. Incoming calls ring the phone with caller ID. On top of that:

- HD calling (16 kHz wideband)
- Caller ID in FSK Bellcore, FSK ETSI or DTMF
- Hold and call waiting with the FLASH button
- Keypad tones during a call, for bank and carrier menus
- Speed dial, redial, call back and network codes like `*#21#`
- Call forwarding and USSD
- A hotline that dials one number when the handset is lifted
- Do not disturb with weekly windows, holidays and per-contact priority
- A Quiet button for an hour or two
- [Call recording](docs/recording.md) to a USB stick or the router
- [SIP phones and apps](docs/sip.md) sharing the SIM (experimental)
- Call history, phonebook, blocked numbers and limits on outgoing calls
- Alarm calls, a line test, and an LED that shows the line state
- [Scripts and MQTT](docs/scripts.md) for Home Assistant, doorbells and alerts
- Backup and restore of everything

Pulse dialing and tones for other countries are not done yet (dial and busy tone are 425 Hz).

## What you need

- AW1000 with the Quectel RG500Q-EA modem
- OpenWrt 25.12. I use [NoobWRT](https://github.com/nooblk-98/noobwrt-arcadyan-aw1k), whose
  rj11 images already include all three packages
- A SIM with VoLTE calls
- The SLIC patch file, see below

QModem is optional. fxsd sends its AT commands through QModem, through ModemManager when it
runs the modem, or straight to the AT port, whichever the firmware has. For SIP phones without
QModem there is [Modem SIP](https://github.com/Perz3us/openwrt-modem-sip), my own SIP server.

## Install

The kernel module has to match your firmware's kernel. Each release has one for official
OpenWrt and one for the latest NoobWRT with the start of its kernel hash in the name. Check
yours with `apk list --installed kernel`, it is the part after `~`.

Copy the three apk files from the latest release to `/tmp` on the router and install them:

```
cd /tmp
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/kmod-aw1000-fxs-6.12.94.0.4.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/fxsd-0.8.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/luci-app-fxs-0.8.0-r1.apk
/etc/init.d/fxsd stop 2>/dev/null; rmmod aw1000_fxs 2>/dev/null
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.4.0-r1.apk /tmp/fxsd-0.8.0-r1.apk /tmp/luci-app-fxs-0.8.0-r1.apk
```

Copy the patch file to `/etc/fxs/si3226x_patch.bin` and turn the port on in LuCI under
Services > Phone (RJ11). The first start puts the modem audio in slave mode and restarts the
modem, so the internet drops for about a minute once.

All settings are in LuCI, and the full list is in [docs/settings.md](docs/settings.md). If
something doesn't work, see [docs/problems.md](docs/problems.md).

## SLIC patch file

The SLIC chip needs a DSP patch from Skyworks every time it starts. It is their code, so I
can't put it here. If you have the ProSLIC API source, convert
`si3226x_patch_C_FB_2017MAY26.c` (the same patch the stock firmware uses):

```
python3 tools/mkpatch.py si3226x_patch_C_FB_2017MAY26.c si3226x_patch.bin
```

## Building

You need Docker. The first run downloads the OpenWrt SDK, which takes a while.

```
./build.sh                                     # official OpenWrt
./build.sh 2a69233f6bd4682b23e0218aa97d98ef    # NoobWRT, with your kernel hash
```

The packages end up in `dist/`. Building the same commit twice gives the same files.

## Notes

The interesting part was the audio. The modem and the SLIC share a PCM bus but can't hear each
other, so the driver runs the IPQ8074 PCM block as the bus master and copies the samples
between them. My notes on the hardware are in [docs/hardware.md](docs/hardware.md).

There is about 46 V on the RJ11 contacts and more while it rings, so be careful. Emergency
numbers are never blocked or hidden, but this hasn't been tested with a real emergency call,
so don't rely on it for one.

Not affiliated with Arcadyan, Telstra, Quectel or Skyworks. If you try it on your AW1000, let
me know how it went in an issue. Pull requests are welcome.

## License

Copyright (C) 2026 perz3us. Licensed under GPL-2.0, see [LICENSE](LICENSE).

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
- SIP calling through QModem voice (experimental, off by default, see below)
- Caller ID on phones with a display (FSK Bellcore, FSK ETSI or DTMF, before or after the first ring).
  Numbers can be shown the way you dial them, like 0771234567 instead of 94771234567
- Ringing tone in the handset while the other side rings
- Hold: press FLASH during a call, press it again to come back
- Speed dial: save a number under a code and dial `*1`, `*2` and so on
- Blocked numbers, including hidden numbers if you want
- The phone LED shows the line state: solid when ready, fast blink when a call comes in, slow
  blink while the handset is off hook (each can be changed). During NoobWRT's LED night mode
  it stays dark and only blinks while a call rings
- Do not disturb: the phone doesn't ring during set hours, calls still show in the history.
  A second call from the same number within 3 minutes rings
- Line test in LuCI with line voltage, loop current and a button to ring the phone. It also
  warns when no phone seems to be plugged in (checked each time the phone rings)
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

The kernel module has to be built for the exact kernel your firmware uses. NoobWRT rj11 images
already come with all three packages. The release has a kernel module for official OpenWrt
25.12.5 and one for the latest NoobWRT, with the start of its kernel hash in the file name.
Check yours with `apk list --installed kernel` (the part after `~`). For other firmware,
build it yourself (see Building).

## Install

Download the three apk files from the latest release to `/tmp` on the router (or copy them
there with scp) and install them:

```
cd /tmp
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.6.1/kmod-aw1000-fxs-6.12.94.0.3.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.6.1/fxsd-0.6.1-r4.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.6.1/luci-app-fxs-0.6.1-r2.apk
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.3.0-r1.apk /tmp/fxsd-0.6.1-r4.apk /tmp/luci-app-fxs-0.6.1-r2.apk
```

On NoobWRT with the kernel module already installed (0.3.0), you only need fxsd and
luci-app-fxs. Otherwise update all three together (SIP calling needs the 0.3.0 driver). The old
kernel module stays loaded until it is removed, so stop fxsd and remove it before installing:

```
/etc/init.d/fxsd stop
rmmod aw1000_fxs
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.3.0-r1.apk /tmp/fxsd-0.6.1-r4.apk /tmp/luci-app-fxs-0.6.1-r2.apk
/etc/init.d/fxsd start
```

Rebooting the router after installing does the same. Then reload LuCI with Ctrl+F5.

The modem audio interface has to be in slave mode. fxsd checks this when it starts and sets it
if needed, then restarts the modem, so the internet drops for a minute the first time. The
modem remembers it. To set it by hand instead:

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
| `at_port` | `auto` | modem AT port, `auto` finds it by itself |
| `patch` | `/etc/fxs/si3226x_patch.bin` | patch file |
| `ringback` | `1` | ringing tone while the other side rings |
| `hd` | `0` | HD calling (16 kHz). Changing it restarts the modem, mobile data drops for about 30 seconds |
| `sip` | `0` | SIP calling through QModem voice (experimental) |
| `callerid` | `bellcore` | caller ID format: bellcore, etsi, dtmf or off |
| `callerid_format` | `received` | `local` shows numbers from your own country the way you dial them and puts the international prefix (like 00) in front of other numbers. `received` sends the number without the + |
| `callerid_country` | `auto` | your country code without the +, `auto` reads it from the SIM |
| `callerid_prefix` | `auto` | the digit you dial before a local number, usually `0`. `none` for countries without one, like Portugal or Italy |
| `callerid_timing` | `after` | send caller ID `after` the first ring (North America), `before` it (most of Europe), or `rpas`: a short ring, then the number, then normal ringing (some European phones, FSK only) |
| `ring_pattern` | `standard` | how the phone rings: `standard` (2 s ring, 4 s pause), `short` (1 s, 4 s), `double` (UK and Australia) or `custom` |
| `ring_on` | `2000` | ring time in ms for `custom` (200 to 8000) |
| `ring_off` | `4000` | pause time in ms for `custom` (200 to 8000) |
| `led` | `green:phone` | LED to use, empty to leave it alone |
| `led_ready` | `on` | LED when the line is ready: on, slow, fast or off |
| `led_ringing` | `fast` | LED while a call comes in |
| `led_offhook` | `slow` | LED while the handset is off hook |
| `block_private` | `0` | reject calls without a number |
| `dnd` | `0` | do not disturb, the phone doesn't ring between `dnd_from` and `dnd_to` |
| `dnd_from` | `22:00` | start of do not disturb |
| `dnd_to` | `07:00` | end of do not disturb |
| `history_autodelete` | `0` | delete old calls from the call history |
| `history_days` | `30` | how many days of calls to keep when that is on |

Speed dial entries are `config speed` sections with `code` and `number`, blocked numbers are
`config block` sections with `number`. Numbers are matched on the last 9 digits, so
`0771234567` also blocks `+94771234567`.

Logs: `logread -e fxsd`. Call log: `/etc/fxs/calls.log`. Phone numbers and dialed digits
only go to the call log, not to the system log.

If the router reboots while fxsd is starting up, it won't start again on that boot, just in
case it was the cause. Run `/etc/init.d/fxsd clear_guard` or press the button in LuCI.

If fxsd ever gets stuck for 30 seconds it stops itself, the driver puts the line back to idle
and fxsd starts again.

## SIP calling (experimental)

With this on, SIP phones and apps on your network can make and answer calls through the SIM
as well as the RJ11 phone. The SIP side is done by QModem voice (qmodem-voip and
qmodem-sipd). fxsd hands it the modem audio and takes the RJ11 phone's calls through it, so
both sides know about each other: an incoming call rings the RJ11 phone and the SIP phones,
and whichever answers first gets the call.

What you need:

- a QModem build with AW1000 support in QModem voice. It was added in
  [FUjr/QModem#290](https://github.com/FUjr/QModem/pull/290) and is in QModem main, not in a
  release yet. You need qmodem-voip, qmodem-sipd and luci-app-qmodem-voip from it
- at-daemon 3.4.0 or newer (QModem voice needs it)
- the 0.3.0 kernel module or newer

Some NoobWRT images don't have `kmod-sound-core`, and installing qmodem-sipd fails because
alsa-lib asks for it. Nothing here uses it, so an empty package with that name is enough.

QModem has to send its AT commands through the at-daemon. Once, on the router (`4_1` is the
modem section, check yours with `uci show qmodem | grep modem-device`):

```
uci set qmodem.4_1.use_ubus=1
uci commit qmodem
reboot
```

Then in LuCI:

1. QModem > VoIP: turn on Inbound SIP, type a SIP account name and press Generate for the
   password. Write it down, it is shown once. Save.
2. Services > Phone (RJ11) > Settings: tick SIP calling and save. fxsd turns on QModem voice
   and the SIP service by itself.

The Status page shows whether fxsd is connected to QModem voice. If QModem voice is not
running, the RJ11 phone works on its own as before.

In the SIP app (Linphone, Zoiper and so on) use the account name and password from step 1,
domain `192.168.1.1` (your router's LAN address), port 5060, transport UDP. In the app's audio
codecs turn on PCMA or PCMU, other codecs don't work. The phone has to be on the router's
Wi-Fi or LAN. Keep the app running in the background or it can't ring.
If it was closed, open it while the RJ11 phone is ringing and the call shows up.

FLASH (hold and call waiting) on the RJ11 phone doesn't work while it is on. Calls to and
from SIP use 8 kHz audio, even with HD calling on. A call answered on a SIP phone shows as
"Answered on SIP" in the call history.

This is new and still being tested. If calls drop, have no sound or the phone stops
ringing, turn it off and open an [issue](https://github.com/Perz3us/openwrt-aw1000-phone/issues)
with the output of `logread | grep -E 'fxsd|qmodem_voip'`.

## Problems

Calls fail straight away and the log shows `call failed`: check that the modem has voice calls
turned on. Some modems come with `call_control` set to `1,1`, which blocks all calls. The modem
then answers every `ATD` with `NO CARRIER` in about 10 ms and `AT+CEER` gives no cause. fxsd
warns about this in the log and on the Status page. Set it to `0,0`, it is saved in the modem:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\"call_control\"","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\"call_control\",0,0","timeout":3}'
```

If calls still fail, restart the modem with `AT+CFUN=1,1`. Two `+CLCC` entries with no number
are normal on this firmware and fxsd ignores them.

VoLTE never registers (`AT+QCFG="ims"` shows `1,0`) even though the SIM does VoLTE in a phone:
check `sms_only` in the modem settings. Some units, like one that came from Australia, have it
set to `01`. The modem then tells the network it only wants data and SMS, so it never starts
VoLTE. A firmware flash does not reset it. Set it to `00`:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QNVFR=\"/nv/item_files/modem/mmode/sms_only\"","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QNVFW=\"/nv/item_files/modem/mmode/sms_only\",00","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

The modem restart moves the AT port off `ttyUSB2`, so reboot the router afterwards. After the
reboot `AT+QCFG="ims"` should show `1,1`.

On official OpenWrt, ModemManager also reads the modem AT port and takes the answers meant for
fxsd. Stop it or remove it if calls are not picked up.

The phone does not ring or LuCI says "Modem audio not set to slave mode": fxsd tried to set it
and the modem did not keep it. Run the `AT+QDAI` step from Install by hand and restart the modem.

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

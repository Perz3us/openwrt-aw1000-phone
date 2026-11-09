# openwrt-aw1000-phone

[![Release](https://img.shields.io/github/v/release/Perz3us/openwrt-aw1000-phone?label=release)](https://github.com/Perz3us/openwrt-aw1000-phone/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Perz3us/openwrt-aw1000-phone/total?label=downloads)](https://github.com/Perz3us/openwrt-aw1000-phone/releases)
[![Issues](https://img.shields.io/github/issues/Perz3us/openwrt-aw1000-phone?label=issues)](https://github.com/Perz3us/openwrt-aw1000-phone/issues)
[![Router](https://img.shields.io/badge/router-Arcadyan%20AW1000-0a7bbb)](#)
[![OpenWrt](https://img.shields.io/badge/OpenWrt-25.12-00b5e2?logo=openwrt&logoColor=white)](https://openwrt.org)
[![License](https://img.shields.io/github/license/Perz3us/openwrt-aw1000-phone)](LICENSE)

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
- SIP calling through a SIP service you choose, QModem voice is the first one (experimental, off by default, see below)
- Caller ID on phones with a display (FSK Bellcore, FSK ETSI or DTMF, before or after the first ring).
  Numbers can be shown the way you dial them, like 0771234567 instead of 94771234567
- Ringing tone in the handset while the other side rings
- Hold: press FLASH during a call, press it again to come back
- Keypad digits during a call, for phone menus and voicemail
- Speed dial: save a number under a code and dial `*1`, `*2` and so on
- Phone codes: `*66` redial and `*69` call back the last caller, which you can change in Settings
- Lifting the handset can call one set number by itself
- A Call button in the call history: the phone rings, you lift the handset and it dials
- Ring the phone from a script, for a doorbell or an alarm
- A script of your own that runs on call events, for example a message on a missed call
- Backup and restore of the settings and call history on the Status page
- Blocked numbers, including hidden numbers if you want
- Limit which numbers the RJ11 phone can call, by number or prefix. Emergency numbers always go through
- Network service codes on the handset, like `*#21#`. A short beep tone means the network took it, busy means it did not
- Call forwarding and USSD on the Phone services page. Forwarding is kept by the network on your SIM, and your network may charge for it
- Phonebook: names in the call history and on phones with a display, and a ring pattern per contact
- A stuttered dial tone after a missed call, if you turn it on
- Alarm calls: the phone rings at set times and days
- MQTT: send phone events to Home Assistant or another broker. Needs mosquitto-client-nossl
- The phone LED shows the line state: solid when ready, fast blink when a call comes in, slow
  blink while the handset is off hook (each can be changed). During NoobWRT's LED night mode
  it stays dark and only blinks while a call rings
- Do not disturb: weekly windows and holiday dates when the phone doesn't ring, gives one short
  ring, or turns the caller away. Contacts can be Important (always ring) or Never ring. A second
  call from the same number within a few minutes can ring. The call history says why each call
  rang or didn't
- Quiet for a while: a Quiet button on the Status page with a countdown, or `*78*60` on the
  phone for 60 minutes
- Call recording to a USB stick or the router, for the RJ11 phone and SIP phones
- Line test in LuCI with line voltage, loop current and a button to ring the phone. It also
  warns when no phone seems to be plugged in (checked each time the phone rings)
- Call history with search, filters, CSV export and optional auto delete
- Network tones: with no mobile service you hear a fast beeping tone (950 Hz) instead of dial
  tone, and a call the network turns down for lack of capacity gives a fast busy tone (425 Hz)
  instead of the normal busy tone. Digits still dial, emergency numbers too. The Status page
  says when there is no service. This is for the RJ11 phone only, and while a SIP phone is connected a
  failed call gives the normal busy tone

Call waiting: fxsd asks the network to turn it on when it starts. You hear a tone when a second
call comes in and FLASH switches between the calls. Some networks do not offer it, the log says
so. Pulse dialing and tones for other countries are not done yet (dial and busy tone are 425 Hz).

## What you need

- AW1000 with the Quectel RG500Q-EA modem
- OpenWrt 25.12. I use [NoobWRT](https://github.com/nooblk-98/noobwrt-arcadyan-aw1k) 25.12.5
- QModem is optional. With it fxsd sends AT commands through its at-daemon, without it fxsd
  talks to the modem AT port itself. SIP calling needs a SIP service, like QModem voice
- A SIM with voice calls (VoLTE)
- The SLIC DSP patch file, see below

The kernel module has to be built for the exact kernel your firmware uses. NoobWRT rj11 images
already come with all three packages. The release has a kernel module for official OpenWrt
25.12.5 and one for the latest NoobWRT, with the start of its kernel hash in the file name. When
a new official OpenWrt or NoobWRT kernel comes out, its kernel module is added as a separate
release named after it.
Check yours with `apk list --installed kernel` (the part after `~`). For other firmware,
build it yourself (see Building).

## Install

Download the three apk files from the latest release to `/tmp` on the router (or copy them
there with scp) and install them:

```
cd /tmp
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/kmod-aw1000-fxs-6.12.94.0.4.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/fxsd-0.8.0-r1.apk
wget https://github.com/Perz3us/openwrt-aw1000-phone/releases/download/v0.8.0/luci-app-fxs-0.8.0-r1.apk
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.4.0-r1.apk /tmp/fxsd-0.8.0-r1.apk /tmp/luci-app-fxs-0.8.0-r1.apk
```

On NoobWRT with the 0.3.0 kernel module already installed, fxsd and luci-app-fxs are enough,
but recording calls on the RJ11 phone needs the 0.4.0 driver. Otherwise update all three
together (SIP calling needs the 0.3.0 driver or newer). The old
kernel module stays loaded until it is removed, so stop fxsd and remove it before installing:

```
/etc/init.d/fxsd stop
rmmod aw1000_fxs
apk add --allow-untrusted /tmp/kmod-aw1000-fxs-6.12.94.0.4.0-r1.apk /tmp/fxsd-0.8.0-r1.apk /tmp/luci-app-fxs-0.8.0-r1.apk
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

Everything can be set in LuCI under Services > Phone (RJ11). Its tabs are Status,
Call history, Recording, Settings, SIP, Phonebook, Do not disturb, Phone services, Alarm calls, MQTT, Speed dial, Blocked numbers and Outgoing calls. The same settings are in `/etc/config/fxs`.
Save backup and Restore backup are on the Status page. A restore replaces the settings and the
call history, and restarts the phone service.

| Option | Default | |
|---|---|---|
| `enabled` | `1` | turn the port on or off. With `sip_enabled` on, turning it off keeps SIP calling working without the RJ11 phone |
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
| `sip_enabled` | `0` | SIP calling (experimental) |
| `sip_service` | `qmodem` | the SIP service to use, a folder in `/usr/share/fxs/sip` |
| `sip_vpn` | `0` | let SIP phones connect over a VPN |
| `sip_vpn_if` | | the VPN device to allow, like `wg0` or `tailscale0` |
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
| `out_mode` | `off` | `block` or `allow` the prefixes in the `outgoing` sections, for the RJ11 phone |
| `code_dnd_on` | `*78` | code to turn do not disturb on until you turn it off, a `*` and 1 to 6 digits. Add `*` and minutes, like `*78*60`, for 1 to 720 minutes |
| `code_dnd_off` | `*79` | code to go back to the do not disturb windows |
| `dnd_repeat` | `3` | minutes, a second call from the same number within this time rings anyway, 0 turns it off |
| `dnd_allow` | | list of numbers that always ring in a do not disturb window that allows them |
| `rec_mode` | `off` | record calls: `off`, `every` (all calls, the lists are kept but not used), `all` (except `recnum` sections set to `never`) or `chosen` (only `recnum` sections set to `always`). A `recnum` section with `enabled 0` is paused |
| `rec_format` | `stereo` | `stereo` keeps each side apart, `mono` mixes them for half the size |
| `rec_flash_mb` | `50` | MB for recordings on the router |
| `rec_usb_mb` | `1000` | MB for recordings on a USB stick |
| `history_autodelete` | `0` | delete old calls from the call history |
| `history_days` | `30` | how many days of calls to keep when that is on |
| `call_waiting` | `1` | ask the network for call waiting. While `sip_enabled` is on it is turned off and a second caller hears busy |
| `missed_tone` | `0` | stutter the dial tone after a missed call |
| `code_redial` | `*66` | code to call the last number you dialed, a `*` and 1 to 6 digits |
| `code_callback` | `*69` | code to call the last person who called you, a `*` and 1 to 6 digits |
| `hotline_enabled` | `0` | lifting the handset calls `hotline` by itself |
| `hotline` | | the number to call when the handset is lifted |
| `hotline_delay` | `5` | seconds to wait before that call (0 to 10). Pressing a key first gives a normal call. With 0 no other number can be dialed, not even an emergency number |
| `dial_wait` | `4` | seconds after the last digit before the call starts (2 to 15) |
| `flash_min` | `80` | a hook break shorter than this (ms) is ignored (20 to 500) |
| `flash_max` | `700` | a hook break longer than this (ms) hangs up (300 to 3000), at least 100 above `flash_min` |

Speed dial entries are `config speed` sections with `code` and `number`, blocked numbers are
`config block` sections with `number`. Numbers are matched on the last 9 digits, so
`0771234567` also blocks `+94771234567`. A speed dial code wins over a phone code with the same
digits, so a speed dial saved under `66` replaces redial.

## Ringing the phone from a script

```
ubus call fxs ring '{"seconds":20}'
```

This rings the phone for 1 to 60 seconds without caller ID, also during do not disturb. It does
nothing while the phone is in use. To call a number when the handset is lifted:

```
ubus call fxs click_call '{"number":"0771234567"}'
```

## Call events

If `/etc/fxs/hook.sh` exists and can be run, fxsd starts it on every call event with the event and
the number. The events are `incoming`, `answered`, `missed`, `elsewhere`, `blocked`, `waiting`,
`dial`, `connected`, `failed`, `hangup`, `ended`, `recorded`, which gets the file name instead
of the number, and `quiet` with `on` or `off`. The script should finish quickly and keep the
number in quotes, it comes from the network. It is not part of the backup. For example, a
message through ntfy on a missed call:

```
#!/bin/sh
[ "$1" = missed ] && wget -q -O /dev/null --post-data "Missed call from ${2:-a hidden number}" https://ntfy.sh/your-topic
```

Then run `chmod +x /etc/fxs/hook.sh`.

## Call recording

The Recording tab can save answered calls on the RJ11 phone and on SIP phones: every call, every
call except a list of numbers, or only a list of numbers. The lists stay saved when you switch, and
a number can be paused. Recording is off until you turn it on. Calls play back
from the call history, where each recording can also be downloaded, kept or deleted.

Recordings are WAV files named by the date and time, never by the number. Each side of the call is
on its own channel unless you choose mixed sound, which takes half the space. An 8 kHz call takes
about 2 MB a minute, an HD call twice that.

With a USB stick plugged in, recordings go to a `phone-recordings` folder on it. Without one they
go to `/root/phone-recordings` on the router. Each place has its own size limit, and when it is
used up the oldest recordings are removed, except the ones marked Keep. The router always keeps
20 MB free. Recordings stay after a restart, but a firmware upgrade that does not keep settings
deletes the ones on the router. The config backup does not include recordings.

Recording a call may need the other person's consent where you live.

## Emergency calls

Numbers like 112, 911 and 119 are never hidden, and blocked numbers and do not disturb do not
apply to them. Whether an emergency call goes through depends on the modem and the network, and
this has not been tested with a real emergency call. Don't rely on this port for them.

Logs: `logread -e fxsd`. Call log: `/etc/fxs/calls.log`. Phone numbers and dialed digits
only go to the call log, not to the system log.

If the router reboots while fxsd is starting up, it won't start again on that boot, just in
case it was the cause. Run `/etc/init.d/fxsd clear_guard` or press the button in LuCI.

If fxsd ever gets stuck for 30 seconds it stops itself, the driver puts the line back to idle
and fxsd starts again.

## SIP calling (experimental)

With this on, SIP phones and apps on your network can make and answer calls through the SIM
as well as the RJ11 phone. The SIP side is done by a SIP service you choose on the SIP tab.
QModem voice (qmodem-voip and qmodem-sipd) comes as the first choice. fxsd hands the service
the modem audio and takes the RJ11 phone's calls through it, so both sides know about each
other: an incoming call rings the RJ11 phone and the SIP phones, and whichever answers first
gets the call.

What you need for QModem voice:

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
2. Services > Phone (RJ11) > SIP: tick Turn on SIP, choose QModem voice and save. fxsd turns
   on QModem voice and its SIP service by itself.

The SIP tab is the on/off switch for SIP. Turning it off also stops the service's SIP side,
so SIP phones can't connect to a call that has no sound. You can't change it during a call.

The SIP tab shows whether fxsd is connected to the service, and lists anything the service
still needs. If the service is not running, the RJ11 phone works on its own as before.

In the SIP app (Linphone, Zoiper and so on) use the account name and password from step 1,
domain `192.168.1.1` (your router's LAN address), port 5060, transport UDP. In the app's audio
codecs turn on PCMA or PCMU, other codecs don't work. The phone has to be on the router's
Wi-Fi or LAN. Keep the app running in the background or it can't ring.
If it was closed, open it while the RJ11 phone is ringing and the call shows up.

Instead of typing the account in, press Show QR code on the SIP tab and scan it with Linphone.
The code points to a setup file on the router that holds the SIP password, and it is deleted
after 5 minutes. The phone has to be on the router's network to load it.

To use SIP from outside your home, set up WireGuard or Tailscale on the router, then on the SIP
tab tick Allow SIP over the VPN and choose the connection. This opens the SIP ports on that VPN
only. On the phone the VPN has to route your router's LAN address, and with Tailscale the
router's subnet route has to be approved. Never open SIP to the internet, anyone who gets in
can make calls on your SIM.

FLASH (hold and call waiting) on the RJ11 phone doesn't work while it is on. QModem voice
uses 8 kHz audio, even with HD calling on. A call answered on a SIP phone shows as
"Answered on SIP" in the call history.

This is new and still being tested. If calls drop, have no sound or the phone stops
ringing, turn it off and open an [issue](https://github.com/Perz3us/openwrt-aw1000-phone/issues)
with the output of `logread | grep -E 'fxsd|qmodem_voip'`.

To add another SIP service, see [Adding a SIP service](docs/sip-services.md).

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

Building the same commit twice gives the same packages, and `scripts/check_repro.sh` checks this.

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

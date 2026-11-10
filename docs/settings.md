# Settings

Everything here can be changed in LuCI under Services > Phone (RJ11). The same settings live
in `/etc/config/fxs` if you would rather use uci.

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
| `led_quiet` | `blip` | LED while do not disturb is on |
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

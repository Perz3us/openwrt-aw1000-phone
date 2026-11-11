# Problems

Logs are in `logread -e fxsd` and calls in `/etc/fxs/calls.log`. Numbers only go to the call
log, never the system log.

## Calls fail straight away

Some modems come with `call_control` set to `1,1`, which blocks every call. The modem answers
each dial with `NO CARRIER` within a few milliseconds and fxsd warns about it on the Status
page. Set it to `0,0`, the modem keeps it:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\"call_control\",0,0","timeout":3}'
```

If calls still fail, restart the modem with `AT+CFUN=1,1`.

## VoLTE never registers

`AT+QCFG="ims"` shows `1,0` even though the SIM does VoLTE in a phone. Some units, like one from
Australia, have `sms_only` set, so the modem only asks the network for data and SMS. A firmware
flash doesn't reset it. Clear it and restart the modem:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QNVFW=\"/nv/item_files/modem/mmode/sms_only\",00","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

The restart moves the AT port off `ttyUSB2`, so reboot the router after it. Then
`AT+QCFG="ims"` should show `1,1`.

## Calls are not picked up with ModemManager

When ModemManager runs the modem, fxsd sends its AT commands through `mmcli`. Some builds of
ModemManager only allow that in debug mode, and then fxsd falls back to the AT port, where
ModemManager takes the answers meant for fxsd. Stop ModemManager, or use a build with AT commands
over D-Bus turned on.

## Calls connect but nobody hears anything

The phone rings and calls connect, but both ends are silent, and the log shows `peak modem 1`
after every call. The Status page shows a warning after such a call. The modem is sending its
audio somewhere other than the phone.

### PCM interface 2

The modem is sending its audio out of PCM interface 2, and only interface 1 is wired to the
phone. Setups that carry call audio over USB (`AT+QPCMV`, like the old QModem RJ11 scripts)
set `AT+QAUDCFG="pcmidx",2`, and the modem keeps it.

fxsd checks it when it starts, moves the modem back to interface 1, sets slave mode again and
restarts the modem. By hand:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QAUDCFG=\"pcmidx\",1","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QDAI=x,1,0,4,0,0,1,1","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

Don't skip the QDAI and the restart. Slave mode is set per interface, so after only the
`pcmidx` change interface 1 is still master and the sound is loud and distorted.

### USB voice

`AT+QPCMV?` shows `1,...`: the modem sends call audio over USB instead of PCM. fxsd turns it
off when it starts. The modem forgets it on restart, but the old QModem `rj11` service turns it
on again before every call. fxsd warns when `/etc/init.d/rj11` is there. Remove that package.

## The phone doesn't ring, or LuCI says the modem audio is not in slave mode

fxsd sets this when it starts, but the modem didn't keep it. Set it by hand and restart the
modem:

```
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QDAI=x,1,0,4,0,0,1,1","timeout":3}'
ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB2","at_cmd":"AT+CFUN=1,1","timeout":3}'
```

To go back to the factory setting use `AT+QDAI=x,0,0,4,0,0,1,1` and restart the modem again.

## fxsd won't start after a reboot

If the router rebooted while fxsd was starting, it stays off for that boot in case it was the
cause. Press the button in LuCI or run `/etc/init.d/fxsd clear_guard`.

If fxsd ever hangs for 30 seconds it stops itself, the driver puts the line back to idle and
fxsd starts again.

## New tabs don't show after an update

The browser has the old pages cached. Press Ctrl+F5, or log out of LuCI and back in.

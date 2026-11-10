# SIP calling

This is experimental. With SIP on, phones and apps on your network can make and answer calls
through the SIM alongside the RJ11 phone. An incoming call rings all of them and whichever
answers first gets it. The SIP side comes from a service you pick on the SIP tab. There are two,
Modem SIP and QModem voice, and [sip-services.md](sip-services.md) explains how to add another.

## Modem SIP

[Modem SIP](https://github.com/Perz3us/openwrt-modem-sip) is my own small SIP server for this.
It needs no QModem, so it works on the new NoobWRT too. Each phone gets its own login with a QR
code for Linphone, and HD calls use Opus or G.722. Install its packages, open Modem SIP under
Services and press Turn on. Phones are added on that page, and the call history shows which
one answered.

## QModem voice

You need qmodem-voip, qmodem-sipd and luci-app-qmodem-voip from QModem main (AW1000 support
came in [FUjr/QModem#290](https://github.com/FUjr/QModem/pull/290)), at-daemon 3.4.0 or newer
and the 0.3.0 kernel module or newer.

QModem has to send its AT commands through the at-daemon. `4_1` is the modem section, check
yours with `uci show qmodem | grep modem-device`:

```
uci set qmodem.4_1.use_ubus=1
uci commit qmodem
reboot
```

Some NoobWRT images are missing `kmod-sound-core`, which alsa-lib asks for. Nothing uses it,
so an empty package with that name is enough.

Then in LuCI:

1. QModem > VoIP: turn on Inbound SIP, set an account name, press Generate for the password
   and save. Write the password down, it is only shown once.
2. Services > Phone (RJ11) > SIP: tick Turn on SIP, pick QModem voice and save. fxsd starts
   the service by itself.

The SIP tab is the on/off switch. It shows whether fxsd is connected to the service and lists
anything still missing. If the service isn't running, the RJ11 phone works on its own as
before.

## Phones and apps

In Linphone, Zoiper and so on, use the account from step 1, domain `192.168.1.1` (your router's
LAN address), port 5060 and UDP. Turn on the PCMA or PCMU codec, the others don't work. Keep
the app running in the background or it can't ring.

With Linphone you can press Show QR code on the SIP tab and scan it instead of typing the
account. The code points to a setup file on the router that holds the password, and it is
deleted after 5 minutes.

## Away from home

Set up WireGuard or Tailscale on the router, then on the SIP tab tick Allow SIP over the VPN and
choose the connection. That opens the SIP ports on the VPN only. On the phone the VPN has to
route your router's LAN address, and with Tailscale the subnet route has to be approved.

Never open SIP to the internet. Anyone who gets in can make calls on your SIM.

## Limits

While SIP is on, FLASH (hold and call waiting) doesn't work on the RJ11 phone and a second
caller hears busy. QModem voice stays at 8 kHz even with HD calling on. A call answered on a
SIP phone shows as Answered on SIP in the call history.

If calls drop, have no sound or stop ringing, turn SIP off and open an
[issue](https://github.com/Perz3us/openwrt-aw1000-phone/issues) with the output of
`logread | grep -E 'fxsd|qmodem_voip'`.

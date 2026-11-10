# Adding a SIP service

fxsd does not contain any SIP code. It talks to a SIP service over ubus and passes call audio
over a socket. A service is one folder in `sip/`, and to add one you open a pull request with
that folder. Nothing needs to change in fxsd or in the other project.

Services run as root and can make calls on the SIM. Only install ones you trust, and in a pull
request say where the service's code comes from.

## What a service does

While a service is connected, fxsd stops calling the modem. The service has to place, answer
and end the modem calls itself, and report what is happening. If the service stops, fxsd goes
back to the modem directly as soon as the line is idle.

## The folder

| File | Needed | What it does |
| --- | --- | --- |
| `info` | yes | Shell variables `TITLE`, `PACKAGE`, `URL` and, if the service has its own LuCI page, `PAGE` (like `admin/services/modem-sip`). The SIP tab shows the title, links to the URL when the service is not installed, and links to the page when the service is chosen |
| `map` | yes | Tells fxsd the names your service uses. It can be empty if you use ours |
| `start` | yes | Starts the service. Called when fxsd starts with this service chosen |
| `stop` | yes | Stops it. Called when SIP is off or another service is chosen, so it must be safe to run when nothing is running or installed |
| `status` | yes | Prints one word: `running`, `stopped` or `not-installed` |
| `setup` | no | Prints one line for each thing the user still has to do, and nothing when all is ready. The SIP tab shows these lines. Never print passwords |
| `ports` | no | Prints `sip PORT` and `rtp FROM-TO`, the ports your service listens on. Without it the SIP tab can't let the service be used over a VPN |
| `account` | no | Prints `host=`, `port=`, `user=`, `password=` and `transport=` for the account a phone app should use. It may take a phone name as its first argument, for services with a login per phone. Without it the SIP tab can't make a QR code for Linphone. Only fxsd's own scripts read this, it is never shown on the page |

The folder is installed as `/usr/share/fxs/sip/<name>/`. The name can use lowercase letters,
digits, `-` and `_`.

To ship a service in the fxsd package, add its files to the install section of
`openwrt/fxsd/Makefile`, next to the `qmodem` lines. The program it starts can stay in its own
package, with `status` printing `not-installed` until that package is there.

## The ubus interface

The object is `fxs_sip`. Every method replies with the same fields as `status`.

| Method | Arguments | What it asks for |
| --- | --- | --- |
| `status` | | The current state |
| `dial` | `line`, `number` | Call this number |
| `answer` | `line` | Answer the ringing call |
| `reject` | `line` | Refuse the ringing call |
| `hangup` | `line` | End the call |
| `digit` | `line`, `digit` | Send this DTMF digit |

`line` is the name of who is asking, which is always `phone` for the RJ11 port. A reply has to
come within 4 seconds, so return straight away and report progress later.

| Field | What it holds |
| --- | --- |
| `state` | `off`, `idle`, `dialing`, `alerting`, `ringing`, `active` or `ending` |
| `origin` | Who started the call: `phone`, `network`, or the name of a SIP endpoint. `none` when there is no call |
| `answered_by` | Who has the call: `phone`, the name of a SIP endpoint, or `none` |
| `number` | The other side's number |
| `enabled` | `false` means the service is not ready, and fxsd uses the modem directly. `off` means the same |

Send the same fields in the event `fxs_sip.call` each time something changes. fxsd also asks
for `status` every few seconds, in case an event gets lost.

An incoming call for the RJ11 phone goes `ringing` with origin `network`, then `active` with
`answered_by` set to `phone`, then `idle`. A call from the phone goes `dialing`, `alerting`,
`active` with origin `phone`, then `idle`. A call answered on a SIP phone goes `active` with
`answered_by` set to your endpoint's name, which is what gives the service the audio. Use the phone's name as the endpoint name: the call history shows it, as in Answered on Pixel, and a call started from a SIP phone shows as Called from that name.

## The map

If your service already has its own names, the map translates them, one per line, ours first:

```
object my_voice
event my_voice.call
audio 8000
method dial originate
arg line endpoint
field number remote_number
state ringing incoming_ringing
owner phone rj11
```

Anything you leave out keeps our name. `method`, `arg`, `field`, `state` and `owner` can have up
to 24 lines in total, and a name can be up to 31 characters. `sip/qmodem/map` is a full example,
and fxsd refuses a map it cannot read and stays on the modem directly.

## Audio

Connect to the unix socket `/var/run/fxsd/pcm.sock` as root, type `SOCK_SEQPACKET`. Each message
is one 20 ms frame of signed 16-bit mono samples, little endian. Audio flows while the call
belongs to one of your endpoints, and your frames go back the same way. A longer frame is
dropped. Only one client can connect, and a new one replaces the old one.

| `audio` in the map | HD calling | Rate | Samples in a frame |
| --- | --- | --- | --- |
| `8000` or left out | off or on | 8 kHz | 160 |
| `16000` | off | 8 kHz | 160 |
| `16000` | on | 16 kHz | 320 |

So a service with `audio 16000` has to go by the size of the frames it gets. With HD on and
`audio 8000`, fxsd converts the audio for you.

## Starting from the example

`sip/example` is a service with no SIP in it, written in shell. Its map only has `audio 16000`,
so it uses our names, and you drive calls by hand. Copy the folder and replace the hand
commands with real ones.

```bash
scp -O -r sip/example root@192.168.1.1:/usr/share/fxs/sip/
```

Then on the router:

```bash
/usr/share/fxs/sip/example/example.rpcd install
```

Choose Example service on the SIP tab and turn on SIP. These commands drive a call, and every
request from fxsd is written to `/tmp/fxs-example/log`:

| Command | What it does |
| --- | --- |
| `example.rpcd ring 0771234567` | A call comes in, and the RJ11 phone rings |
| `example.rpcd alert` | The call you dialed is ringing at the other side |
| `example.rpcd connect` | The call you dialed was answered |
| `example.rpcd take` | A SIP endpoint takes the call, and audio goes to the socket |
| `example.rpcd end` | The other side hung up |

`example.rpcd remove` takes it off rpcd again.

`tools/pcm-probe.c` connects to the audio socket, prints the frames it gets each second, sends
them back as an echo, and with `-o FILE` saves the first 20 seconds. Build it with
`make -C tools build/pcm-probe`, using the compiler for your router.

## Before you open a pull request

Run this on the router with the service running:

```bash
sh sip/check.sh /usr/share/fxs/sip/<name>
```

It has to print `OK`. Then test these with a real phone and say the result in the pull request:

- An incoming call rings the RJ11 phone and your SIP side, and either one can answer
- A call from the RJ11 phone and one from the SIP side both connect, with sound both ways
- Hanging up from either side ends the call
- Keypad digits reach the other side
- With your service stopped during a call, the phone does not get stuck, and it works directly
  once idle
- With SIP turned off on the SIP tab, the phone works as normal

Say which router firmware and modem you tested on, and whether the audio is 8 or 16 kHz.

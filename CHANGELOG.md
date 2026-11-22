# Changelog

## 0.10.0 (2026-11-22)

- The LuCI pages are redone for phones, with a page menu instead of the tab row
- Update from the Status page, which also says when a new version is out
- Call history is grouped by day, and a number can be blocked or added to the phonebook from there
- Call a number from the Status page, the phone rings and dials it when you lift the handset
- The menu shows how many calls were missed since you last opened the call history
- Status shows the mobile network and signal on NoobWRT, and when the line was last tested
- The kernel module is still 0.4.0

## 0.9.0 (2026-11-16)

- Choose the folder for call recordings, on a USB stick, drive or network share under /mnt, or on the router under /root (#18)
- If that folder is not there, for example when the stick is unplugged, calls are still recorded in the usual place and the Call recording page says so
- The kernel module is still 0.4.0

## 0.8.1 (2026-11-11)

- Fixed calls with no sound on routers that used the old QModem RJ11 setup
- The Status page warns when a call connects but no sound comes from the modem

## 0.8.0 (2026-11-10)

- Call recording for the RJ11 phone and SIP phones, kept on a USB stick or on the router
- In-call keypad, so bank and carrier menus work
- Do not disturb with holidays, contact priorities and a Quiet button
- SIP tab with SIP services as add-ons, and Modem SIP support for SIP without QModem
- Works with NoobWRT builds that use ModemManager
- Call waiting, contacts, alarm calls, outgoing call rules, redial and call back codes, handset hotline, SIP over VPN, MQTT for Home Assistant, an event script and backup

## 0.7.0 (2026-11-01)

- QModem is optional, fxsd talks to the modem directly without it
- Use only SIP phones by switching the phone port off
- More do not disturb options
- Updating fxsd restarts it

## 0.6.1 (2026-10-31)

- Caller ID can show local numbers
- Fixed the voice calls turned off warning

## 0.6.0 (2026-10-30)

- FSK caller ID works, and a new RP-AS timing
- Do not disturb
- Phone LED stays dark in NoobWRT's LED night mode
- Warning when no phone seems to be plugged in

## 0.5.0 (2026-10-28)

- SIP calling through QModem voice (experimental)
- Ring patterns
- Fixed calls losing sound while the other side stays connected (#7)
- Phone numbers are no longer written to the system log

## 0.4.1 (2026-10-24)

- fxsd puts the modem audio in slave mode by itself
- The modem AT port is found automatically
- Modem restarts no longer move the AT port to another name

## 0.4.0 (2026-10-24)

- HD calling with 16 kHz audio

## 0.3.2 (2026-10-24)

- Fixed fxsd staying off after saving settings twice soon after boot

## 0.3.1 (2026-10-23)

- Caller ID before the first ring
- Phone LED follows the line
- Call timer, deleting single calls, and a warning when the modem has voice calls turned off

## 0.3.0 (2026-10-22)

- Call history tab with search, filters, CSV export and automatic deleting

## 0.2.0 (2026-10-11)

- Caller ID, ringing tone, hold, speed dial, blocked numbers, line test and the phone LED

## 0.1.0 (2026-10-10)

- First release

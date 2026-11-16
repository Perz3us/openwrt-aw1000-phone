# Call recording

Turn it on in the Recording tab. It saves answered calls on the RJ11 phone and on SIP phones.
You can record every call, every call except some numbers, or only some numbers. The number
lists stay saved when you switch, and a number can be paused without removing it.

Recordings are WAV files named by date and time, never by the number. Each side of the call is
on its own channel, about 2 MB a minute, or mixed into one for half the size. HD calls take
twice as much. You play, download, keep or delete them from the call history.

With a USB stick plugged in they go to a `phone-recordings` folder on it, otherwise to
`/root/phone-recordings` on the router. Each place has its own size limit, and when it fills up
the oldest recordings are removed, except the ones you keep. The router always keeps 20 MB free.

You can pick another folder in the Folder setting: one under `/mnt` on a USB stick, drive or
network share, like `/mnt/sda1/calls`, or one under `/root` on the router. A folder on a drive
uses the USB size limit, one under `/root` the router limit. If the folder can't be used, for
example because the stick is unplugged, calls are still recorded in the usual place and the
Recording tab tells you.

Recordings survive a restart, but a firmware upgrade that doesn't keep settings deletes the
ones on the router, and the settings backup doesn't include them.

Recording on the RJ11 phone needs the 0.4.0 kernel module.

Check whether recording a call needs the other person's consent where you live.

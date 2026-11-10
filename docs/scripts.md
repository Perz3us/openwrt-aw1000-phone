# Scripts

## Ring the phone

For a doorbell or an alarm, ring the phone for 1 to 60 seconds. It rings without caller ID,
also during do not disturb, and does nothing while the phone is in use:

```
ubus call fxs ring '{"seconds":20}'
```

To call a number as soon as the handset is lifted:

```
ubus call fxs click_call '{"number":"0771234567"}'
```

## Call events

If `/etc/fxs/hook.sh` exists and can be run, fxsd starts it on each call event with the event
and the number. The events are `incoming`, `answered`, `missed`, `elsewhere`, `blocked`,
`waiting`, `dial`, `connected`, `failed`, `hangup`, `ended`, `recorded` (with the file name
instead of the number) and `quiet` (with `on` or `off`).

The script should finish quickly and keep the number in quotes, it comes from the network. It
is not part of the backup. A missed call message through ntfy:

```
#!/bin/sh
[ "$1" = missed ] && wget -q -O /dev/null --post-data "Missed call from ${2:-a hidden number}" https://ntfy.sh/your-topic
```

Then run `chmod +x /etc/fxs/hook.sh`.

## MQTT

The MQTT tab sends phone events to Home Assistant or another broker, and Home Assistant finds
the phone and its Quiet switch by itself. It needs `mosquitto-client-nossl`.

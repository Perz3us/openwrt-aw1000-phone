#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

[ -f /.dockerenv ] || { echo "run this inside a container"; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
stubs=/tmp/mqtt-stubs
store=/tmp/mqtt.store
args=/tmp/mqtt.args
err=/var/run/fxs-mqtt.err
script=$repo/openwrt/fxsd/files/fxs-mqtt
fails=0

mkdir -p "$stubs" /var/run
cat > "$stubs/uci" <<'EOF'
#!/bin/sh
[ "$1" = -q ] && shift
v=$(grep "^$2=" /tmp/mqtt.store | cut -d= -f2-)
[ -n "$v" ] && echo "$v"
exit 0
EOF
cat > "$stubs/mosquitto_pub" <<'EOF'
#!/bin/sh
for a in "$@"; do echo "$a"; done > /tmp/mqtt.args
for a in "$@"; do echo "$a"; done >> /tmp/mqtt.all
[ ! -f /tmp/mqtt.fail ] || { echo "refused" >&2; exit 1; }
EOF
cat > "$stubs/ubus" <<'EOF'
#!/bin/sh
echo "$*" >> /tmp/mqtt.ubus
EOF
chmod +x "$stubs/uci" "$stubs/mosquitto_pub" "$stubs/ubus"
PATH=$stubs:$PATH

check() {
	if eval "$2"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		fails=$((fails + 1))
	fi
}

reset() {
	printf '%s\n' fxs.main.mqtt=1 fxs.main.mqtt_host=10.0.0.5 > "$store"
	rm -f "$args" "$err" /tmp/mqtt.fail /tmp/mqtt.all /tmp/mqtt.ubus
}

reset
sh "$script" missed 0771234567
check "sends host and default port" 'grep -qx 10.0.0.5 "$args" && grep -qx 1883 "$args"'
check "default topic" 'grep -qx aw1000/phone/event "$args"'
check "json has event and number" 'grep -qxF "{\"event\":\"missed\",\"number\":\"0771234567\"}" "$args"'
check "no error left" '[ ! -f "$err" ]'

reset
echo fxs.main.mqtt=0 > "$store"
sh "$script" missed 0771
check "off sends nothing" '[ ! -f "$args" ]'

reset
echo fxs.main.mqtt_numbers=0 >> "$store"
sh "$script" missed 0771234567
check "numbers can be left out" 'grep -qxF "{\"event\":\"missed\",\"number\":\"\"}" "$args"'

reset
sh "$script" 'missed"x' '077"; touch /tmp/mqtt.hack; "'
check "odd characters cannot break the json" 'grep -qxF "{\"event\":\"missedx\",\"number\":\"077\"}" "$args"'
check "odd characters cannot run commands" '[ ! -f /tmp/mqtt.hack ]'

reset
echo fxs.main.mqtt_user=me >> "$store"
echo fxs.main.mqtt_password=se\ cret >> "$store"
sh "$script" answered 0771
check "user and password are passed" 'grep -qx me "$args" && grep -qx "se cret" "$args"'

reset
touch /tmp/mqtt.fail
sh "$script" missed 0771
check "a broker error is saved" 'grep -q refused "$err"'
rm -f /tmp/mqtt.fail
sh "$script" missed 0771
check "a good send clears the error" '[ ! -f "$err" ]'

reset
sh "$script" set 60
check "minutes from Home Assistant set quiet" 'grep -qxF "call fxs quiet_set {\"minutes\":60}" /tmp/mqtt.ubus'
rm -f /tmp/mqtt.ubus
sh "$script" set off
check "off ends quiet" 'grep -qxF "call fxs quiet_set {\"minutes\":0}" /tmp/mqtt.ubus'
rm -f /tmp/mqtt.ubus
sh "$script" set on
check "on turns quiet on with no end" 'grep -qxF "call fxs quiet_set {\"minutes\":-1}" /tmp/mqtt.ubus'
rm -f /tmp/mqtt.ubus
sh "$script" set abc
sh "$script" set 5000
check "anything else does nothing" '[ ! -f /tmp/mqtt.ubus ]'

reset
printf 'state=idle\nquiet=1\nquiet_until=1762700000\n' > /var/run/fxsd.state
sh "$script" quiet on
check "a quiet change is published and kept" 'grep -qx aw1000/phone/quiet /tmp/mqtt.all && grep -qx on /tmp/mqtt.all && grep -qx -- -r /tmp/mqtt.all'
check "the end time is published" 'grep -qx aw1000/phone/quiet/until /tmp/mqtt.all && grep -qx 1762700000 /tmp/mqtt.all'
check "a quiet change is not sent as a call event" '! grep -qx aw1000/phone/event /tmp/mqtt.all'
rm -f /var/run/fxsd.state

reset
cat > "$stubs/mosquitto_sub" <<'EOF'
#!/bin/sh
echo 30
exit 1
EOF
cat > "$stubs/sleep" <<'EOF'
#!/bin/sh
kill -TERM $PPID
exit 0
EOF
chmod +x "$stubs/mosquitto_sub" "$stubs/sleep"
sh "$script" listen
check "discovery has the switch" 'grep -q "^homeassistant/switch/aw1000_phone_quiet/config$" /tmp/mqtt.all'
check "discovery has the minutes box" 'grep -q "^homeassistant/number/aw1000_phone_quiet_minutes/config$" /tmp/mqtt.all'
check "listen passes commands on" 'grep -qxF "call fxs quiet_set {\"minutes\":30}" /tmp/mqtt.ubus'
rm -f "$stubs/mosquitto_sub" "$stubs/sleep"

reset
rm -f "$stubs/mosquitto_pub"
sh "$script" missed 0771
check "missing client is reported" 'grep -q "not installed" "$err"'

[ "$fails" = 0 ] && echo ALL_OK
[ "$fails" = 0 ]

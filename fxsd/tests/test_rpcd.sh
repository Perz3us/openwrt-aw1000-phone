#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

[ -f /.dockerenv ] || { echo "run this inside a container"; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
stubs=/tmp/stubs
store=/tmp/uci.store
rpcd=$repo/openwrt/luci-app-fxs/files/fxs.rpcd
fails=0

mkdir -p "$stubs" /usr/share/libubox /etc/init.d /var/run

cat > "$stubs/uci" <<'EOF'
#!/bin/sh
store=/tmp/uci.store
[ "$1" = -q ] && shift
case "$1" in
get) v=$(grep "^$2=" "$store" | cut -d= -f2-); [ -n "$v" ] || exit 1; echo "$v" ;;
set) k=${2%%=*}; grep -v "^$k=" "$store" > "$store.n"; echo "$2" >> "$store.n"; mv "$store.n" "$store" ;;
show) grep "^$2[.=]" "$store" ;;
esac
exit 0
EOF
cat > "$stubs/ubus" <<'EOF'
#!/bin/sh
[ -f /tmp/at-daemon.on ]
EOF
chmod +x "$stubs/uci" "$stubs/ubus"
export PATH="$stubs:$PATH"

cat > /usr/share/libubox/jshn.sh <<'EOF'
json_init() { J=; }
json_add_string() { J="$J$1=$2;"; }
json_add_boolean() { J="$J$1=$2;"; }
json_add_int() { J="$J$1=$2;"; }
json_add_array() { J="$J$1[;"; }
json_close_array() { J="$J];"; }
json_add_object() { J="$J{;"; }
json_close_object() { J="$J};"; }
json_dump() { echo "$J"; }
json_load() { JIN=$1; }
json_get_var() {
	eval "$1=\$(echo \"\$JIN\" | sed -n 's/.*\"$2\": *\"\\{0,1\\}\\([^\",}]*\\).*/\\1/p')"
}
EOF

printf '#!/bin/sh\necho "$1" >> /tmp/init.log\n' > /etc/init.d/fxsd
chmod +x /etc/init.d/fxsd

addon() {
	d=/usr/share/fxs/sip/$1
	mkdir -p "$d"
	printf 'TITLE="%s"\nPACKAGE=%s\nURL=https://example.test/%s\n' "$2" "$1" "$1" > "$d/info"
	echo "audio $3" > "$d/map"
	printf '#!/bin/sh\necho %s\n' "$4" > "$d/status"
	chmod +x "$d/status"
}

check() {
	if eval "$2"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		fails=$((fails + 1))
	fi
}

call() {
	echo "$2" | sh "$rpcd" call "$1"
}

state() {
	printf 'state=%s\n' "$1" > /var/run/fxsd.state
}

reset() {
	printf 'fxs.main.sip_enabled=0\nfxs.main.sip_service=one\n' > "$store"
	: > /tmp/init.log
	state idle
}

rm -rf /usr/share/fxs/sip
addon one "First service" 8000 running
addon two "Second service" 16000 stopped
addon gone "Not here" 8000 not-installed

reset
out=$(sh "$rpcd" call sip_services)
check "lists every service" 'echo "$out" | grep -q "name=one" && echo "$out" | grep -q "name=two" && echo "$out" | grep -q "name=gone"'
check "lists titles" 'echo "$out" | grep -q "title=Second service"'
check "lists status" 'echo "$out" | grep -q "name=gone;title=Not here;url=https://example.test/gone;status=not-installed"'
check "lists audio from the map" 'echo "$out" | grep -q "name=two;title=Second service;url=https://example.test/two;status=stopped;audio=16000"'
check "defaults to 8000 with no audio line" 'echo "object x" > /usr/share/fxs/sip/two/map; sh "$rpcd" call sip_services | grep -q "status=stopped;audio=8000"'
echo 'audio 16000' > /usr/share/fxs/sip/two/map
check "shows enabled and current" 'echo "$out" | grep -q "enabled=0;current=one"'

reset
out=$(call set_sip '{"enabled": 1, "name": "two"}')
check "valid save works" 'echo "$out" | grep -q "result=1"'
check "valid save stores both" 'grep -qx fxs.main.sip_enabled=1 "$store" && grep -qx fxs.main.sip_service=two "$store"'
check "valid save restarts" 'grep -qx restart /tmp/init.log'

for s in talking calling ringing dialtone digits busy; do
	reset
	state "$s"
	out=$(call set_sip '{"enabled": 1, "name": "two"}')
	check "refused while $s" 'echo "$out" | grep -q "result=0;error=busy"'
	check "nothing saved while $s" 'grep -qx fxs.main.sip_enabled=0 "$store" && [ ! -s /tmp/init.log ]'
done

reset
rm -f /var/run/fxsd.state
out=$(call set_sip '{"enabled": 1, "name": "two"}')
check "works while the service is stopped" 'echo "$out" | grep -q "result=1"'

reset
out=$(call set_sip '{"enabled": 1, "name": "../x"}')
check "odd name refused" 'echo "$out" | grep -q "result=0;error=name"'
out=$(call set_sip '{"enabled": 1, "name": ""}')
check "empty name refused" 'echo "$out" | grep -q "result=0;error=name"'
out=$(call set_sip '{"enabled": 1, "name": "nope"}')
check "unknown name refused" 'echo "$out" | grep -q "result=0;error=not-installed"'
out=$(call set_sip '{"enabled": 1, "name": "gone"}')
check "cannot turn on a service that is not installed" 'echo "$out" | grep -q "result=0;error=not-installed"'
out=$(call set_sip '{"enabled": 0, "name": "gone"}')
check "can turn SIP off with it selected" 'echo "$out" | grep -q "result=1" && grep -qx fxs.main.sip_enabled=0 "$store"'

reset
printf 'fxs.main.sip_enabled=1\nfxs.main.sip_service=two\n' > "$store"
out=$(sh "$rpcd" call status)
check "status shows SIP on and the title" 'echo "$out" | grep -q "sipon=1" && echo "$out" | grep -q "siptitle=Second service"'
printf 'fxs.main.sip_service=two\n' > "$store"
out=$(sh "$rpcd" call status)
check "status shows SIP off" 'echo "$out" | grep -q "sipon=0"'
printf 'fxs.main.sip=1\n' > "$store"
out=$(sh "$rpcd" call status)
check "status reads an old backup" 'echo "$out" | grep -q "sipon=1"'

reset
out=$(sh "$rpcd" call status)
check "status service is unknown with no value" 'echo "$out" | grep -q "service=-1"'
printf 'state=idle\nservice=0\n' > /var/run/fxsd.state
out=$(sh "$rpcd" call status)
check "status shows no service" 'echo "$out" | grep -q "service=0"'
printf 'state=idle\nservice=1\n' > /var/run/fxsd.state
out=$(sh "$rpcd" call status)
check "status shows service" 'echo "$out" | grep -q "service=1"'

reset
printf '#!/bin/sh\necho "Do the first thing"\necho "Do the second thing"\n' > /usr/share/fxs/sip/one/setup
chmod +x /usr/share/fxs/sip/one/setup
out=$(sh "$rpcd" call sip_services)
check "lists what is missing" 'echo "$out" | grep -q "audio=8000;setup\[;=Do the first thing;=Do the second thing;\];}"'
check "a service with no setup script lists nothing" 'echo "$out" | grep -q "name=two;.*audio=16000;setup\[;\];}"'

setup=$repo/sip/qmodem/setup
qm() {
	printf '%s\n' "$@" > "$store"
	: > /tmp/at-daemon.on
	: > /dev/fxs0audio
	printf '#!/bin/sh\n' > /etc/init.d/qmodem_voip
	printf '#!/bin/sh\n' > /etc/init.d/qmodem_voip_sipd
	chmod +x /etc/init.d/qmodem_voip /etc/init.d/qmodem_voip_sipd
}
ready="qmodem.4_1=modem-device qmodem.4_1.use_ubus=1 qmodem_sip.inbound.username=me qmodem_sip.inbound.password=secret"

qm $ready
check "QModem ready prints nothing" '[ -z "$(sh "$setup")" ]'
check "QModem setup never prints the password" '! sh "$setup" | grep -q secret'
qm qmodem.4_1=modem-device qmodem.4_1.use_ubus=1 qmodem_sip.inbound.password=secret
check "no SIP account name" 'sh "$setup" | grep -q "SIP account"'
qm qmodem.4_1=modem-device qmodem.4_1.use_ubus=1 qmodem_sip.inbound.username=me
check "no SIP password" 'sh "$setup" | grep -q "SIP account"'
qm qmodem.4_1=modem-device qmodem_sip.inbound.username=me qmodem_sip.inbound.password=secret
check "use_ubus off names the section" 'sh "$setup" | grep -q "qmodem.4_1.use_ubus=1"'
qm $ready
rm -f /tmp/at-daemon.on
check "no at-daemon" 'sh "$setup" | grep -q "at-daemon"'
qm $ready
rm -f /dev/fxs0audio
check "old kernel module" 'sh "$setup" | grep -q "kernel module"'
qm $ready
rm -f /etc/init.d/qmodem_voip_sipd
check "no qmodem-sipd" 'sh "$setup" | grep -q "qmodem-sipd"'
qm $ready
rm -f /etc/init.d/qmodem_voip
check "no qmodem-voip" 'sh "$setup" | grep -q "qmodem-voip"'

out=$(sh "$rpcd" list)
check "methods are listed" 'echo "$out" | grep -q "\"sip_services\"" && echo "$out" | grep -q "\"set_sip\""'

cat > "$stubs/pidof" <<'EOF'
#!/bin/sh
[ -s /tmp/fake.pid ] && cat /tmp/fake.pid
EOF
chmod +x "$stubs/pidof"
cat > "$stubs/fakefxsd" <<'EOF'
#!/bin/sh
handle() {
	[ -f /tmp/fake.silent ] && return
	read -r tag id kind arg < /var/run/fxsd.cmd
	case "$kind" in
	ussd) [ "$arg" = 1 ] && { printf 'ok=1\nmore=1\ntext=Pick\n' > "/var/run/fxsd.reply.$id"; return; }
		[ "$arg" = 999 ] && printf 'text=x
' > "/var/run/fxsd.reply.$id" || printf 'ok=1
text=Balance 5
' > "/var/run/fxsd.reply.$id" ;;
	*) printf 'ok=0
error=unsupported
' > "/var/run/fxsd.reply.$id" ;;
	esac
}
trap handle USR2
while :; do sleep 0.1; done
EOF
chmod +x "$stubs/fakefxsd"

mreq() {
	(k=$1; a=$2; set --; . "$rpcd"; modem_request "$k" "$a")
}

rm -f /var/run/fxsd.cmd /tmp/fake.pid /tmp/fake.silent
out=$(MODEM_WAIT=2 mreq ussd '*100#')
check "request fails when fxsd is not running" 'echo "$out" | grep -q "ok=0;error=stopped"'
check "no stale command file is left when fxsd is stopped" '[ ! -e /var/run/fxsd.cmd ]'
check "no reply file is left after stopped" '! ls /var/run/fxsd.reply.* >/dev/null 2>&1'

"$stubs/fakefxsd" &
echo $! > /tmp/fake.pid
sleep 0.3
out=$(MODEM_WAIT=3 mreq ussd '*100#')
check "request returns the reply" 'echo "$out" | grep -q "ok=1;text=Balance 5;"'
check "request line has id kind and arg" 'grep -qE "^at-req [0-9]+ ussd [*]100#$" /var/run/fxsd.cmd'
check "reply file is removed" '! ls /var/run/fxsd.reply.* >/dev/null 2>&1'
out=$(MODEM_WAIT=3 mreq ussd 1)
check "a menu reply says more is coming" 'echo "$out" | grep -q "more=1"'
out=$(MODEM_WAIT=3 mreq ussd 999)
check "a reply with no ok line is an error" 'echo "$out" | grep -q "ok=0;error=reply;"'
out=$(MODEM_WAIT=3 mreq fwd-get '')
check "unsupported kinds say so" 'echo "$out" | grep -q "ok=0;error=unsupported;"'
out=$(MODEM_WAIT=3 mreq fwd-set '+94771234567,20')
check "fwd-set is sent" 'echo "$out" | grep -q "error=unsupported" && grep -q " fwd-set +94771234567,20$" /var/run/fxsd.cmd'
out=$(MODEM_WAIT=3 mreq ussd '*1;rm')
check "bad arg is refused before sending" 'echo "$out" | grep -q "ok=0;error=arg;"'
out=$(MODEM_WAIT=3 mreq dial '1')
check "unknown kind is refused" 'echo "$out" | grep -q "ok=0;error=kind;"'
touch /tmp/fake.silent
out=$(MODEM_WAIT=2 mreq ussd '*100#')
check "silent fxsd gives a timeout" 'echo "$out" | grep -q "ok=0;error=timeout;"'
check "no reply file is left after timeout" '! ls /var/run/fxsd.reply.* >/dev/null 2>&1'
rm -f /tmp/fake.silent
kill "$(cat /tmp/fake.pid)" 2>/dev/null
rm -f /tmp/fake.pid /var/run/fxsd.cmd

out=$(sh "$rpcd" list)
check "modem_request is not a listed method" '! echo "$out" | grep -q modem_request'

out=$(call forward_get '{"reason":"sideways"}')
check "bad forward reason is refused" 'echo "$out" | grep -q reason'
out=$(call forward_set '{"reason":"busy","number":"12a45"}')
check "forward number with letters is refused" 'echo "$out" | grep -q number'
out=$(call forward_set '{"reason":"noanswer","number":"0771234567","seconds":"99"}')
check "forward seconds out of range are refused" 'echo "$out" | grep -q seconds'
out=$(call ussd '{"code":"*1;rm#"}')
check "bad USSD code is refused" 'echo "$out" | grep -q code'
out=$(call ussd '{"code":"123"}')
check "USSD code must start with a star or hash" 'echo "$out" | grep -q code'
out=$(call ussd '{"reply":"1a"}')
check "USSD reply must be digits" 'echo "$out" | grep -q code'
check "new methods are listed" 'sh "$rpcd" list | grep -q forward_set && sh "$rpcd" list | grep -q ussd_end'

mkdir -p /root/phone-recordings
: > /root/phone-recordings/20261109-104215-1762685535.wav
: > /root/phone-recordings/20261109-110000-1762686800.wav
: > /root/secret.wav
out=$(call rec_delete '{"name":"../secret.wav"}')
check "a path in a recording name is refused" 'echo "$out" | grep -q "result=0;error=name;" && [ -f /root/secret.wav ]'
out=$(call rec_keep '{"name":"20261109-104215-1762685535.wav","keep":true}')
check "keep marks the recording" 'echo "$out" | grep -q "result=1;" && [ -f /root/phone-recordings/20261109-104215-1762685535.wav.keep ]'
out=$(call rec_status)
check "status lists recordings with keep" 'echo "$out" | grep -q "name=20261109-104215-1762685535.wav;path=/root/phone-recordings/20261109-104215-1762685535.wav;size=0;keep=1;usb=0;"'
printf 'last=20261109-110000-1762686800.wav\nactive=1\n' > /var/run/fxsd.rec.state
out=$(call rec_delete '{"name":"20261109-110000-1762686800.wav"}')
check "the recording in progress is not deleted" 'echo "$out" | grep -q "error=busy;" && [ -f /root/phone-recordings/20261109-110000-1762686800.wav ]'
out=$(call rec_delete '{"name":"20261109-104215-1762685535.wav"}')
check "delete removes the file and its keep mark" 'echo "$out" | grep -q "result=1;" && [ ! -e /root/phone-recordings/20261109-104215-1762685535.wav.keep ] && [ ! -e /root/phone-recordings/20261109-104215-1762685535.wav ]'
rm -rf /root/phone-recordings /root/secret.wav /var/run/fxsd.rec.state

for bad in 1441 abc -2 '1;reboot'; do
	out=$(call quiet_set "{\"minutes\":\"$bad\"}")
	check "quiet_set refuses $bad" 'echo "$out" | grep -q "result=0;error=minutes;"'
done

[ "$fails" = 0 ] && echo ALL_OK
[ "$fails" = 0 ]

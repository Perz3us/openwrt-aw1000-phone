#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

[ -f /.dockerenv ] || { echo "run this inside a container"; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
stubs=/tmp/stubs-example
rpc=$repo/sip/example/example.rpcd
dir=$repo/sip/example
fails=0

rm -rf "$stubs" /tmp/fxs-example /tmp/events
mkdir -p "$stubs" /usr/share/libubox /usr/libexec/rpcd /etc/init.d

cat > "$stubs/ubus" <<'EOF'
#!/bin/sh
echo "$3" >> /tmp/events
EOF
chmod +x "$stubs/ubus"
export PATH="$stubs:$PATH"
printf '#!/bin/sh\nexit 0\n' > /etc/init.d/rpcd
chmod +x /etc/init.d/rpcd

cat > /usr/share/libubox/jshn.sh <<'EOF'
json_init() { J=; }
json_add_string() { J="$J$1=$2;"; }
json_add_boolean() { J="$J$1=$2;"; }
json_dump() { echo "$J"; }
json_load() { JIN=$1; }
json_get_var() {
	eval "$1=\$(echo \"\$JIN\" | sed -n 's/.*\"$2\": *\"\\([^\"]*\\)\".*/\\1/p')"
}
EOF

check() {
	if [ "$2" = "$3" ]; then
		echo "ok   $1"
	else
		echo "FAIL $1: wanted '$3', got '$2'"
		fails=$((fails + 1))
	fi
}

has() {
	case "$2" in
	*"$3"*) echo "ok   $1" ;;
	*) echo "FAIL $1: no '$3' in '$2'"; fails=$((fails + 1)) ;;
	esac
}

call() {
	printf '%s\n' "${2:-{\}}" | "$rpc" call "$1"
}

rm -f /usr/libexec/rpcd/fxs_sip
check "not installed" "$("$dir/status")" not-installed
has "setup says how" "$("$dir/setup")" "example.rpcd install"
check "stop before anything is safe" "$("$dir/stop"; echo $?)" 0
[ -e /tmp/fxs-example/state ] && check "stop made no state" present absent

"$rpc" install
check "installed link" "$(readlink /usr/libexec/rpcd/fxs_sip)" "$rpc"
check "setup is quiet" "$("$dir/setup")" ""
check "stopped" "$("$dir/status")" stopped
has "status when off" "$(call status)" "state=off;"
has "off is not enabled" "$(call status)" "enabled=0;"

"$dir/start"
check "running" "$("$dir/status")" running
has "idle after start" "$(call status)" "state=idle;"
has "start sent an event" "$(tail -1 /tmp/events)" "state=idle;"

r=$(call dial '{ "line": "phone", "number": "0771234567" }')
has "dial reply" "$r" "state=dialing;origin=phone;answered_by=none;number=0771234567;"
has "dial event" "$(tail -1 /tmp/events)" "state=dialing;"
"$rpc" alert
has "alerting" "$(call status)" "state=alerting;origin=phone;"
"$rpc" connect
has "connected" "$(call status)" "state=active;origin=phone;"
r=$(call digit '{ "line": "phone", "digit": "5" }')
has "digit leaves the state" "$r" "state=active;"
has "digit logged" "$(cat /tmp/fxs-example/log)" "digit phone 5"
has "hangup" "$(call hangup '{ "line": "phone" }')" "state=idle;origin=none;answered_by=none;number=;"

has "bad number refused" "$(call dial '{ "line": "phone", "number": "1;reboot" }')" "state=idle;"

"$rpc" ring 0771234567
has "ringing" "$(call status)" "state=ringing;origin=network;answered_by=none;number=0771234567;"
has "answer" "$(call answer '{ "line": "phone" }')" "state=active;origin=network;answered_by=phone;"
has "hangup again" "$(call hangup '{ "line": "phone" }')" "state=idle;"

"$rpc" ring 0771234567
has "reject" "$(call reject '{ "line": "phone" }')" "state=idle;"

"$rpc" ring 0771234567
"$rpc" take
has "taken by the service" "$(call status)" "state=active;origin=network;answered_by=example;"
"$rpc" end

has "answer while idle does nothing" "$(call answer '{ "line": "phone" }')" "state=idle;"

"$dir/stop"
check "stopped again" "$("$dir/status")" stopped
has "dial while off does nothing" "$(call dial '{ "line": "phone", "number": "077" }')" "state=off;"

has "list names the methods" "$("$rpc" list)" '"digit"'
check "check.sh passes" "$(sh "$repo/sip/check.sh" "$dir" | tail -1)" "OK: $dir"

"$rpc" remove
check "removed" "$("$dir/status")" not-installed

[ "$fails" = 0 ] && echo ALL_OK
[ "$fails" = 0 ]

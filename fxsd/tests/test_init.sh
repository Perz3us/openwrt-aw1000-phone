#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us
# Writes to /etc, /usr/share and /var/run, so it only runs inside a throwaway container.

[ -f /.dockerenv ] || { echo "run this inside a container"; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
stubs=/tmp/stubs
store=/tmp/uci.store
log=/tmp/test.log
fails=0

mkdir -p "$stubs" /etc/fxs /var/run
: > /etc/fxs/si3226x_patch.bin
echo x > /etc/fxs/si3226x_patch.bin

cat > "$stubs/uci" <<'EOF'
#!/bin/sh
store=/tmp/uci.store
[ "$1" = -q ] && shift
case "$1" in
get) v=$(grep "^$2=" "$store" | cut -d= -f2-); [ -n "$v" ] && echo "$v" ;;
set) k=${2%%=*}; grep -v "^$k=" "$store" > "$store.n"; echo "$2" >> "$store.n"; mv "$store.n" "$store" ;;
delete) grep -v "^$2=" "$store" > "$store.n"; mv "$store.n" "$store" ;;
esac
exit 0
EOF
cat > "$stubs/logger" <<'EOF'
#!/bin/sh
shift 2
echo "$*" >> /tmp/test.log
EOF
printf '#!/bin/sh\nexit 0\n' > "$stubs/insmod"
chmod +x "$stubs"/*
export PATH="$stubs:$PATH"

check() {
	if eval "$2"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		fails=$((fails + 1))
	fi
}

addon() {
	mkdir -p "/usr/share/fxs/sip/$1"
	echo 'audio 8000' > "/usr/share/fxs/sip/$1/map"
	printf '#!/bin/sh\necho "start %s" >> /tmp/test.log\n' "$1" > "/usr/share/fxs/sip/$1/start"
	printf '#!/bin/sh\necho "stop %s" >> /tmp/test.log\n' "$1" > "/usr/share/fxs/sip/$1/stop"
	chmod +x "/usr/share/fxs/sip/$1/start" "/usr/share/fxs/sip/$1/stop"
}

run_init() {
	: > "$log"
	(
		config_load() { :; }
		config_get() {
			local v
			v=$(uci -q get "fxs.$2.$3")
			eval "$1=\${v:-\$4}"
		}
		config_get_bool() { config_get "$@"; }
		config_foreach() { :; }
		config_list_foreach() { :; }
		procd_open_instance() { :; }
		procd_close_instance() { :; }
		procd_set_param() {
			[ "$1" = command ] && { shift; echo "cmd $*" >> "$log"; }
			return 0
		}
		. "$repo/openwrt/fxsd/files/fxsd.init"
		start_service
	)
}

rm -rf /usr/share/fxs/sip
addon qmodem
addon other

printf 'fxs.main.enabled=1\n' > "$store"
printf 'fxs.main.sip=1\nfxs.main.enabled=1\n' > "$store"
run_init
check "old sip=1 still works before migration" 'grep -q -- "-S /usr/share/fxs/sip/qmodem" "$log"'

sh "$repo/openwrt/fxsd/files/fxs.defaults"
check "sip=1 migrates to sip_enabled=1" 'grep -qx fxs.main.sip_enabled=1 "$store"'
check "migration removes the old option" '! grep -q "^fxs.main.sip=" "$store"'
check "migration sets the service" 'grep -qx fxs.main.sip_service=qmodem "$store"'
cp "$store" /tmp/uci.once
sh "$repo/openwrt/fxsd/files/fxs.defaults"
check "a second migration changes nothing" 'cmp -s "$store" /tmp/uci.once'
run_init
check "enabled service gets -S" 'grep -q -- "-S /usr/share/fxs/sip/qmodem" "$log"'
check "enabled service is started" 'grep -q "^start qmodem" "$log"'
check "the other add-on is stopped" 'grep -q "^stop other" "$log"'
check "the chosen add-on is not stopped" '! grep -q "^stop qmodem" "$log"'

printf 'fxs.main.sip=0\nfxs.main.enabled=1\n' > "$store"
sh "$repo/openwrt/fxsd/files/fxs.defaults"
check "sip=0 migrates to sip_enabled=0" 'grep -qx fxs.main.sip_enabled=0 "$store"'
run_init
check "off runs with no -S" 'grep -q "^cmd " "$log" && ! grep -q -- " -S " "$log"'
check "off stops every add-on" 'grep -q "^stop qmodem" "$log" && grep -q "^stop other" "$log"'
check "off starts none" '! grep -q "^start " "$log"'

printf 'fxs.main.sip_enabled=1\nfxs.main.sip_service=nope\nfxs.main.enabled=1\n' > "$store"
run_init
check "missing service runs with no -S" 'grep -q "^cmd " "$log" && ! grep -q -- " -S " "$log"'
check "missing service is logged" 'grep -q "SIP service nope is not installed" "$log"'

printf 'fxs.main.sip_enabled=1\nfxs.main.sip_service=../x\nfxs.main.enabled=1\n' > "$store"
mkdir -p /usr/share/fxs/x
echo 'audio 8000' > /usr/share/fxs/x/map
run_init
check "odd name is refused" 'grep -q "name is not valid" "$log" && ! grep -q -- " -S " "$log"'

printf 'fxs.main.sip_enabled=1\nfxs.main.sip_service=other\nfxs.main.enabled=0\n' > "$store"
run_init
check "port off with SIP on still runs, with -o" 'grep -q -- "-S /usr/share/fxs/sip/other.* -o" "$log"'
check "switching service stops the old one" 'grep -q "^stop qmodem" "$log" && grep -q "^start other" "$log"'

printf 'fxs.main.sip_enabled=0\nfxs.main.enabled=0\n' > "$store"
run_init
check "port off with SIP off does not run" '! grep -q "^cmd " "$log"'

printf 'fxs.main.enabled=1\n' > "$store"
sh "$repo/openwrt/fxsd/files/fxs.defaults"
check "new install gets a default service" 'grep -qx fxs.main.sip_service=qmodem "$store"'
run_init
check "new install keeps SIP off" '! grep -q -- " -S " "$log"'

cp -r "$repo/sip/qmodem" /tmp/bad
sed -i '/^TITLE/d' /tmp/bad/info
sh "$repo/sip/check.sh" /tmp/bad > /dev/null
check "check.sh fails on an info with no title" '[ $? -ne 0 ]'
chmod +x "$repo"/sip/qmodem/start "$repo"/sip/qmodem/stop "$repo"/sip/qmodem/status 2>/dev/null
sh "$repo/sip/check.sh" "$repo/sip/qmodem" > /dev/null
check "check.sh passes on the QModem add-on" '[ $? -eq 0 ]'

[ "$fails" = 0 ] && echo ALL_OK
[ "$fails" = 0 ]

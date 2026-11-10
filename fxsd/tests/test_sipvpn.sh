#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

[ -f /.dockerenv ] || { echo "run this inside a container"; exit 2; }

repo=$(cd "$(dirname "$0")/../.." && pwd)
vpn=$repo/openwrt/fxsd/files/fxs-sip-vpn
qr=$repo/openwrt/fxsd/files/fxs-sip-qr
stubs=/tmp/vpn-stubs
store=/tmp/vpn.store
fails=0

export SIP_ROOT=/tmp/vpn-sip
export RULES=/tmp/vpn-rules/fxs-sip-vpn.nft
export RELOAD=fake_reload
export PROV=/tmp/vpn-www/fxs-prov

mkdir -p "$stubs" "$SIP_ROOT/test"
cat > "$stubs/uci" <<'EOF'
#!/bin/sh
[ "$1" = -q ] && shift
[ "$1" = get ] || exit 0
v=$(grep "^$2=" /tmp/vpn.store | cut -d= -f2-)
[ -n "$v" ] || exit 1
echo "$v"
EOF
cat > "$stubs/fake_reload" <<'EOF'
#!/bin/sh
echo reload >> /tmp/vpn.reloads
EOF
chmod +x "$stubs/uci" "$stubs/fake_reload"
PATH=$stubs:$PATH

: > "$SIP_ROOT/test/map"
cat > "$SIP_ROOT/test/ports" <<'EOF'
#!/bin/sh
echo "sip 5060"
echo "rtp 40000-40031"
echo "rtp 1-2"
EOF
cat > "$SIP_ROOT/test/account" <<'EOF'
#!/bin/sh
echo "host=192.168.1.1"
echo "port=5060"
echo "user=phone"
echo "password=$(cat /tmp/vpn.pass)"
echo "transport=udp"
EOF
chmod +x "$SIP_ROOT/test/ports" "$SIP_ROOT/test/account"

check() {
	if eval "$2"; then
		echo "ok   $1"
	else
		echo "FAIL $1"
		fails=$((fails + 1))
	fi
}

reset() {
	printf '%s\n' fxs.main.sip_enabled=1 fxs.main.sip_service=test fxs.main.sip_vpn=1 fxs.main.sip_vpn_if=wg0 > "$store"
	printf 'a&b<c"d' > /tmp/vpn.pass
	rm -f /tmp/vpn.reloads
	chmod +x "$SIP_ROOT/test/ports" "$SIP_ROOT/test/account"
}

reloads() {
	[ -f /tmp/vpn.reloads ] && wc -l < /tmp/vpn.reloads || echo 0
}

reset
rm -f "$RULES"
sh "$vpn" apply
check "apply writes rules" '[ -f "$RULES" ]'
check "sip port on udp and tcp" 'grep -qx "iifname \"wg0\" udp dport 5060 accept comment \"fxs_sip_vpn\"" "$RULES" && grep -qx "iifname \"wg0\" tcp dport 5060 accept comment \"fxs_sip_vpn\"" "$RULES"'
check "rtp range" 'grep -qx "iifname \"wg0\" udp dport 40000-40031 accept comment \"fxs_sip_vpn\"" "$RULES"'
check "a range below 1024 is left out" '[ "$(wc -l < "$RULES")" = 3 ]'
check "firewall reloaded once" '[ "$(reloads)" = 1 ]'

sh "$vpn" apply
check "apply twice keeps one set and does not reload again" '[ "$(wc -l < "$RULES")" = 3 ] && [ "$(reloads)" = 1 ]'

sh "$vpn" remove
check "remove deletes the rules" '[ ! -f "$RULES" ] && [ "$(reloads)" = 2 ]'
sh "$vpn" remove
check "remove with nothing there does not reload" '[ "$(reloads)" = 2 ]'

reset
: > /tmp/vpn.pass
sh "$vpn" apply
check "no password, no rules" '[ ! -f "$RULES" ]'
check "no password is reported" 'sh "$vpn" status | grep -qx "need=The SIP service needs a password."'

reset
echo fxs.main.sip_enabled=0 >> "$store"
sed -i '/^fxs.main.sip_enabled=1$/d' "$store"
sh "$vpn" apply
check "SIP off, no rules" '[ ! -f "$RULES" ]'
check "SIP off is reported" 'sh "$vpn" status | grep -qx "need=Turn on SIP first."'

reset
chmod -x "$SIP_ROOT/test/ports"
sh "$vpn" apply
check "no ports script, no rules" '[ ! -f "$RULES" ]'
check "no ports script is reported" 'sh "$vpn" status | grep -qx "need=This SIP service cannot be used over a VPN."'

reset
sed -i 's/^fxs.main.sip_vpn_if=.*/fxs.main.sip_vpn_if=wg0;reboot/' "$store"
sh "$vpn" apply
check "bad interface name, no rules" '[ ! -f "$RULES" ]'

reset
sh "$vpn" apply
sed -i 's/^fxs.main.sip_vpn=1$/fxs.main.sip_vpn=0/' "$store"
sh "$vpn" apply
check "turning it off removes the rules" '[ ! -f "$RULES" ]'

reset
sed -i 's/^fxs.main.sip_vpn_if=.*/fxs.main.sip_vpn_if=nosuchvpn0/' "$store"
check "a missing device shows as down" 'sh "$vpn" status | grep -qx down=1'

reset
rm -rf "$PROV"
out=$(sh "$qr" make)
f=$(ls "$PROV"/*.xml 2>/dev/null)
check "qr writes one file" '[ "$(echo "$f" | wc -w)" = 1 ]'
check "file name is 32 hex" 'basename "$f" .xml | grep -qxE "[0-9a-f]{32}"'
check "file can be read by the web server" '[ "$(stat -c %a "$f")" = 644 ]'
check "folder cannot be listed" '[ -f "$PROV/index.html" ]'
check "url points at the file" 'echo "$out" | grep -qx "url=http://192.168.1.1/fxs-prov/$(basename "$f")"'
check "password is escaped" 'grep -qF "a&amp;b&lt;c&quot;d" "$f"'
check "account is in the file" 'grep -qF "&lt;sip:phone@192.168.1.1&gt;" "$f" && grep -qF "&lt;sip:192.168.1.1:5060;transport=udp&gt;" "$f"'

printf '#!/bin/sh\nexit 1\n' > "$stubs/od"
chmod +x "$stubs/od"
check "a code is made on a router without od" 'sh "$qr" make | grep -q "^url=http://192.168.1.1/fxs-prov/[0-9a-f]\{32\}\.xml$"'
rm -f "$stubs/od"
f=$(grep -l passwd "$PROV"/*.xml 2>/dev/null)

sh "$qr" make > /dev/null
check "a new code empties the old one" '[ -f "$f" ] && ! grep -q passwd "$f" && grep -q "<config" "$f" && [ "$(grep -l passwd "$PROV"/*.xml | wc -l)" = 1 ]'

sh "$qr" clean
KEEP=1 sh "$qr" make > /dev/null
sleep 2
check "the password is gone when the time is up" '[ -n "$(ls "$PROV"/*.xml 2>/dev/null)" ] && ! grep -q passwd "$PROV"/*.xml'
touch -d "40 days ago" "$PROV"/*.xml
old=$(ls "$PROV"/*.xml)
sh "$qr" make > /dev/null
check "old empty files are removed" '[ ! -f "$old" ] && [ "$(ls "$PROV"/*.xml | wc -l)" = 1 ]'
sh "$qr" clean
check "clean empties every code" '! grep -q passwd "$PROV"/*.xml'

: > /tmp/vpn.pass
check "empty password gives the reason" 'sh "$qr" make | grep -q "^need=The SIP service needs an account name and a password"'
reset
chmod -x "$SIP_ROOT/test/account"
check "no account script gives the reason" 'sh "$qr" make | grep -q "^need=This SIP service cannot set up phone apps"'
reset
sed -i 's/^fxs.main.sip_enabled=1$/fxs.main.sip_enabled=0/' "$store"
check "SIP off gives the reason" 'sh "$qr" make | grep -qx "need=Turn on SIP first."'

check "the QModem add-on passes check.sh" 'sh "$repo/sip/check.sh" "$repo/sip/qmodem" >/dev/null'

[ "$fails" = 0 ] && echo ALL_OK
[ "$fails" = 0 ]

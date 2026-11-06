#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

dir=$1
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

[ -n "$dir" ] && [ -d "$dir" ] || {
	echo "usage: check.sh SERVICE_FOLDER"
	exit 2
}

if [ -f "$dir/info" ]; then
	for v in TITLE PACKAGE URL; do
		[ -n "$(. "$dir/info"; eval echo "\$$v")" ] || fail "info has no $v"
	done
else
	fail "no info file"
fi

[ -f "$dir/map" ] || fail "no map file"

for s in start stop status; do
	[ -x "$dir/$s" ] || fail "$s is missing or cannot be run"
done

if [ -x "$dir/status" ]; then
	st=$("$dir/status")
	case "$st" in
	running|stopped|not-installed) ;;
	*) fail "status printed '$st', it must print running, stopped or not-installed" ;;
	esac
fi

if [ "$st" = running ] && command -v ubus >/dev/null; then
	mapped() {
		awk -v k="$1" -v o="$2" '$1 == k && $2 == o { print $3; f = 1 } END { if (!f) print o }' "$dir/map"
	}
	object=$(awk '$1 == "object" { print $2 }' "$dir/map")
	list=$(ubus -v list "${object:-fxs_sip}" 2>/dev/null)
	[ -n "$list" ] || fail "object ${object:-fxs_sip} is not on ubus"
	for m in status dial answer reject hangup digit; do
		echo "$list" | grep -q "\"$(mapped method "$m")\"" || fail "object has no method for $m"
	done
fi

[ "$fails" = 0 ] && echo "OK: $dir"
[ "$fails" = 0 ]

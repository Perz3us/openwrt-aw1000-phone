#!/bin/sh
# Reads the kernel hash of an official OpenWrt release for qualcommax/ipq807x.
#   official_kernel.sh [version]
# Needs curl. Prints version=, kernel= and hash= lines.
set -e
BASE=https://downloads.openwrt.org
VER=${1:-$(curl -fsS $BASE/.versions.json | sed -n 's/.*"stable_version": *"\([^"]*\)".*/\1/p')}
echo "$VER" | grep -qx '[0-9][0-9.]*[0-9]' || { echo "bad OpenWrt version: $VER" >&2; exit 1; }
K=$(curl -fsS "$BASE/releases/$VER/targets/qualcommax/ipq807x/openwrt-$VER-qualcommax-ipq807x.manifest" |
	sed -n 's/^kernel - //p')
[ -n "$K" ] || { echo "no kernel in the $VER manifest" >&2; exit 1; }
H=${K#*~}
H=${H%-*}
echo "$H" | grep -qx '[0-9a-f]\{32\}' || { echo "bad kernel hash: $H" >&2; exit 1; }

echo "version=$VER"
echo "kernel=${K%%~*}"
echo "hash=$H"

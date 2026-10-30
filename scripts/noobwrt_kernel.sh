#!/bin/sh
# Reads the OpenWrt version and kernel hash from a NoobWRT release image.
#   noobwrt_kernel.sh [release tag]
# Needs gh, tar and unsquashfs. Prints tag=, openwrt=, kernel=, hash= and fxs= lines.
set -e
REPO=nooblk-98/noobwrt-arcadyan-aw1k
TAG=${1:-$(gh release view --repo $REPO --json tagName -q .tagName)}
echo "$TAG" | grep -qx '[A-Za-z0-9._-]\+' || { echo "bad tag: $TAG" >&2; exit 1; }
IMG=$(gh release view "$TAG" --repo $REPO --json assets -q '[.assets[].name | select(endswith("sysupgrade.bin"))][0]')
[ -n "$IMG" ] || { echo "no sysupgrade image in $TAG" >&2; exit 1; }

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
gh release download "$TAG" --repo $REPO -p "$IMG" -D "$D"
tar -C "$D" -xf "$D/$IMG" --wildcards '*/root'
unsquashfs -no-progress -d "$D/r" "$D"/*/root lib/apk/db/installed etc/openwrt_release >/dev/null

ver() { grep -A1 "^P:$1\$" "$D/r/lib/apk/db/installed" | sed -n 's/^V://p'; }
K=$(ver kernel)
[ -n "$K" ] || { echo "no kernel package in $IMG" >&2; exit 1; }
H=${K#*~}
H=${H%-*}
O=$(sed -n "s/^DISTRIB_RELEASE='\(.*\)'/\1/p" "$D/r/etc/openwrt_release")
echo "$H" | grep -qx '[0-9a-f]\{32\}' || { echo "bad kernel hash: $H" >&2; exit 1; }
echo "$O" | grep -qx '[0-9][0-9.]*[0-9]' || { echo "bad OpenWrt version: $O" >&2; exit 1; }

echo "tag=$TAG"
echo "openwrt=$O"
echo "kernel=${K%%~*}"
echo "hash=$H"
echo "fxs=$(ver kmod-aw1000-fxs)"

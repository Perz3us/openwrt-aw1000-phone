#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DIST=$(dirname "$HERE")/dist
A=/tmp/repro.first
B=/tmp/repro.second

ls "$DIST"/*.apk >/dev/null 2>&1 || sh "$HERE/build_pkgs.sh" "$@"
(cd "$DIST" && sha256sum *.apk) > "$A"
sh "$HERE/build_pkgs.sh" "$@"
(cd "$DIST" && sha256sum *.apk) > "$B"
if ! diff "$A" "$B"; then
	echo "packages differ between two builds"
	exit 1
fi
echo "packages are the same in both builds"

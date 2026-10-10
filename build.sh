#!/bin/sh
# usage: ./build.sh [noobwrt kernel hash]
set -e
cd "$(dirname "$0")"
export MSYS_NO_PATHCONV=1
SRC=$(pwd -W 2>/dev/null || pwd)

sdk() {
	docker run --rm -v "$SRC:/work" -v owrt-sdk-cache:/cache owrt-sdk-env sh "$@"
}

docker image inspect owrt-sdk-env >/dev/null 2>&1 || docker build -t owrt-sdk-env -f scripts/Dockerfile.sdk scripts

if [ -n "$1" ]; then
	sdk /work/scripts/build.sh /work/kernel/aw1000-fxs noobwrt
fi
sdk /work/scripts/build_pkgs.sh "$1"

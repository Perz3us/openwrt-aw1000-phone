#!/bin/sh
# Builds the kmod-aw1000-fxs, fxsd and luci-app-fxs apk packages with the OpenWrt SDK.
#   build_pkgs.sh [noobwrt-vermagic]
# With a vermagic the kernel module is built against the NoobWRT kernel copy (CONFIG_KALLSYMS)
# and its kernel dependency is set to that hash.
set -e
SDK=$(ls -d /cache/openwrt-sdk-*/ | head -1)
SDK=${SDK%/}
VERMAGIC=$1
cd "$SDK"
command -v make >/dev/null || { apt-get update -qq && apt-get install -y -qq make gcc g++ python3 file rsync libncurses-dev gawk unzip bzip2 xz-utils zstd bison flex >/dev/null; }
command -v rsync >/dev/null || { apt-get update -qq && apt-get install -y -qq rsync gawk python3 file bison flex libncurses-dev >/dev/null; }

echo "src-link aw1000 /work/openwrt" > feeds.conf
./scripts/feeds update aw1000 >/dev/null
./scripts/feeds install -p aw1000 -a >/dev/null 2>&1 || true
[ -f .config ] || make defconfig >/dev/null
for p in kmod-aw1000-fxs fxsd luci-app-fxs; do
	sed -i "/CONFIG_PACKAGE_$p[= ]/d" .config
	echo "CONFIG_PACKAGE_$p=m" >> .config
done
make defconfig >/dev/null

KOPT=
if [ -n "$VERMAGIC" ]; then
	KDIR=$(ls -d build_dir/target-*/linux-*/linux-6.12.* | head -1)
	NK=$(dirname "$KDIR")/linux-noobwrt
	[ -f "$NK/.noobwrt-ready" ] || { echo "run scripts/build.sh <dir> noobwrt once first"; exit 1; }
	echo "$VERMAGIC" > "$NK/.vermagic"
	KOPT="LINUX_DIR=$SDK/$NK"
fi
make package/feeds/aw1000/aw1000-fxs/clean >/dev/null 2>&1 || true
make package/feeds/aw1000/aw1000-fxs/compile $KOPT -j4 || make package/feeds/aw1000/aw1000-fxs/compile $KOPT V=s
make package/feeds/aw1000/fxsd/compile -j4 || make package/feeds/aw1000/fxsd/compile V=s
make package/feeds/aw1000/luci-app-fxs/compile -j4 || make package/feeds/aw1000/luci-app-fxs/compile V=s

mkdir -p /work/dist
rm -f /work/dist/*.apk
find bin -name 'kmod-aw1000-fxs*.apk' -o -name 'fxsd*.apk' -o -name 'luci-app-fxs*.apk' | while read -r f; do
	cp "$f" /work/dist/
done
ls -la /work/dist

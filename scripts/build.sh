#!/bin/sh
# Builds an out-of-tree kernel module against the official OpenWrt 25.12.5
# qualcommax/ipq807x SDK. Runs inside a Debian container; SDK is cached in /cache.
#   build.sh <module dir> [official|noobwrt]
# "noobwrt" = official kernel config + CONFIG_KALLSYMS=y (NoobWRT's struct module is 704 bytes).
set -e
SDK_NAME=openwrt-sdk-25.12.5-qualcommax-ipq807x_gcc-14.3.0_musl.Linux-x86_64
SDK_URL=https://downloads.openwrt.org/releases/25.12.5/targets/qualcommax/ipq807x/$SDK_NAME.tar.zst
MOD_DIR=${1:-/work/kernel/aw1000-fxs}
FLAVOR=${2:-official}

if [ ! -d /cache/$SDK_NAME ]; then
	apt-get update -qq && apt-get install -y -qq wget zstd make gcc g++ python3 file rsync libncurses-dev gawk unzip bzip2 xz-utils >/dev/null
	wget -q -O /cache/sdk.tar.zst "$SDK_URL"
	tar -C /cache --zstd -xf /cache/sdk.tar.zst
	rm /cache/sdk.tar.zst
fi
command -v make >/dev/null || { apt-get update -qq && apt-get install -y -qq make gcc python3 file zstd bison flex rsync >/dev/null; }

SDK=/cache/$SDK_NAME
KDIR=$(ls -d $SDK/build_dir/target-*/linux-*/linux-6.12.* | head -1)
TC=$(ls -d $SDK/staging_dir/toolchain-* | head -1)
export STAGING_DIR=$SDK/staging_dir
export PATH=$TC/bin:$PATH
KMAKE="make ARCH=arm64 CROSS_COMPILE=aarch64-openwrt-linux-musl-"

if [ "$FLAVOR" = noobwrt ]; then
	# keep the copy at the same depth as KDIR: SDK host tools use relative paths
	NK=$(dirname "$KDIR")/linux-noobwrt
	rm -rf /cache/kernel-noobwrt
	if [ ! -f $NK/.noobwrt-ready ]; then
		command -v bison >/dev/null || { apt-get update -qq && apt-get install -y -qq bison flex rsync >/dev/null; }
		rm -rf $NK && cp -a "$KDIR" $NK
		# SDK kernel tree has no Kconfig files, so patch the generated config directly
		sed -i 's/^# CONFIG_KALLSYMS is not set/CONFIG_KALLSYMS=y/' $NK/.config
		echo 'CONFIG_KALLSYMS=y' >> $NK/include/config/auto.conf
		echo '#define CONFIG_KALLSYMS 1' >> $NK/include/generated/autoconf.h
		touch $NK/.noobwrt-ready
	fi
	KDIR=$NK
fi

echo "KDIR=$KDIR ($FLAVOR)"
$KMAKE -C "$KDIR" M="$MOD_DIR" clean >/dev/null
$KMAKE -C "$KDIR" M="$MOD_DIR" modules

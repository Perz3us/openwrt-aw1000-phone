# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=aw1000-fxs
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=perz3us

FXS_ROOT ?= $(abspath $(realpath $(CURDIR))/../..)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/aw1000-fxs
  SUBMENU:=Other modules
  TITLE:=Arcadyan AW1000 FXS (RJ11 phone port) driver
  DEPENDS:=@TARGET_qualcommax_ipq807x
  FILES:=$(PKG_BUILD_DIR)/aw1000-fxs.ko
endef

define KernelPackage/aw1000-fxs/description
  Clocks, SPI and PCM bridge for the Si32261 SLIC on the Arcadyan AW1000.
  Loaded by the fxsd service; it is not loaded automatically at boot.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(FXS_ROOT)/kernel/aw1000-fxs/*.c $(FXS_ROOT)/kernel/aw1000-fxs/*.h \
		$(FXS_ROOT)/kernel/aw1000-fxs/Makefile $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)" modules
endef

$(eval $(call KernelPackage,aw1000-fxs))

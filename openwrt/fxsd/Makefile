# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

include $(TOPDIR)/rules.mk

PKG_NAME:=fxsd
PKG_VERSION:=0.1.0
PKG_RELEASE:=2
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=perz3us

FXS_ROOT ?= $(abspath $(realpath $(CURDIR))/../..)

include $(INCLUDE_DIR)/package.mk

define Package/fxsd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=AW1000 RJ11 phone port daemon
  DEPENDS:=+kmod-aw1000-fxs
endef

define Package/fxsd/description
  Runs the RJ11 phone port of the Arcadyan AW1000: SLIC bring-up, dial tone,
  ringing, DTMF dialing and modem call control through at-daemon.
endef

define Package/fxsd/conffiles
/etc/config/fxs
/etc/fxs/
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)/fxsd $(PKG_BUILD_DIR)/kernel/aw1000-fxs
	$(CP) $(FXS_ROOT)/fxsd/src $(FXS_ROOT)/fxsd/Makefile $(PKG_BUILD_DIR)/fxsd/
	$(CP) $(FXS_ROOT)/kernel/aw1000-fxs/fxs_uapi.h $(PKG_BUILD_DIR)/kernel/aw1000-fxs/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/fxsd build/fxsd build/fxs-bringup \
		CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" WERROR=
endef

define Package/fxsd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config $(1)/etc/fxs
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fxsd/build/fxsd $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fxsd/build/fxs-bringup $(1)/usr/sbin/
	$(INSTALL_BIN) ./files/fxsd.init $(1)/etc/init.d/fxsd
	$(INSTALL_CONF) ./files/fxs.config $(1)/etc/config/fxs
endef

$(eval $(call BuildPackage,fxsd))

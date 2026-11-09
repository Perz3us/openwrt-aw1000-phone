# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

include $(TOPDIR)/rules.mk

PKG_NAME:=fxsd
PKG_VERSION:=0.8.0
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=perz3us

FXS_ROOT ?= $(abspath $(realpath $(CURDIR))/../..)

include $(INCLUDE_DIR)/package.mk

define Package/fxsd
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=AW1000 RJ11 phone port daemon
  DEPENDS:=+kmod-aw1000-fxs +kmod-usb-serial-option +libubus +libubox
endef

define Package/fxsd/description
  Runs the RJ11 phone port of the Arcadyan AW1000: SLIC bring-up, dial tone,
  ringing, DTMF dialing and modem call control (through at-daemon if
  installed, otherwise directly on the AT port), with
  optional SIP calling through a SIP service add-on.
endef

define Package/fxsd/conffiles
/etc/config/fxs
/etc/fxs/
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)/fxsd $(PKG_BUILD_DIR)/kernel/aw1000-fxs
	$(CP) $(FXS_ROOT)/fxsd/src $(FXS_ROOT)/fxsd/Makefile $(PKG_BUILD_DIR)/fxsd/
	$(CP) $(FXS_ROOT)/sip $(PKG_BUILD_DIR)/
	$(CP) $(FXS_ROOT)/kernel/aw1000-fxs/fxs_uapi.h $(PKG_BUILD_DIR)/kernel/aw1000-fxs/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR)/fxsd build/fxsd build/fxs-bringup \
		CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" WERROR=
endef

define Package/fxsd/install
	$(INSTALL_DIR) $(1)/usr/sbin $(1)/etc/init.d $(1)/etc/config $(1)/etc/fxs $(1)/etc/uci-defaults
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fxsd/build/fxsd $(1)/usr/sbin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fxsd/build/fxs-bringup $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/fxs-mqtt $(1)/usr/libexec/fxs-mqtt
	$(INSTALL_BIN) ./files/fxs-sip-vpn $(1)/usr/libexec/fxs-sip-vpn
	$(INSTALL_BIN) ./files/fxs-sip-qr $(1)/usr/libexec/fxs-sip-qr
	$(INSTALL_BIN) ./files/fxsd.init $(1)/etc/init.d/fxsd
	$(INSTALL_CONF) ./files/fxs.config $(1)/etc/config/fxs
	$(INSTALL_BIN) ./files/fxs.defaults $(1)/etc/uci-defaults/90-fxs-at-port
	$(INSTALL_DIR) $(1)/usr/share/fxs/sip/qmodem
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/sip/qmodem/info $(PKG_BUILD_DIR)/sip/qmodem/map $(1)/usr/share/fxs/sip/qmodem/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sip/qmodem/start $(PKG_BUILD_DIR)/sip/qmodem/stop $(PKG_BUILD_DIR)/sip/qmodem/status $(PKG_BUILD_DIR)/sip/qmodem/setup \
		$(PKG_BUILD_DIR)/sip/qmodem/ports $(PKG_BUILD_DIR)/sip/qmodem/account $(1)/usr/share/fxs/sip/qmodem/
endef

$(eval $(call BuildPackage,fxsd))

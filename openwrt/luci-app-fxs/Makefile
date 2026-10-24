# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026 perz3us

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-fxs
PKG_VERSION:=0.4.1
PKG_RELEASE:=1
PKG_LICENSE:=GPL-2.0-only
PKG_MAINTAINER:=perz3us

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-fxs
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI page for the AW1000 RJ11 phone port
  DEPENDS:=+fxsd +luci-base +rpcd
  PKGARCH:=all
endef

define Build/Compile
endef

define Package/luci-app-fxs/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/fxs $(1)/usr/share/luci/menu.d \
		$(1)/usr/share/rpcd/acl.d $(1)/usr/libexec/rpcd
	$(INSTALL_DATA) ./files/phone.js ./files/history.js ./files/settings.js ./files/speeddial.js ./files/blocked.js \
		$(1)/www/luci-static/resources/view/fxs/
	$(INSTALL_DATA) ./files/menu.json $(1)/usr/share/luci/menu.d/luci-app-fxs.json
	$(INSTALL_DATA) ./files/acl.json $(1)/usr/share/rpcd/acl.d/luci-app-fxs.json
	$(INSTALL_BIN) ./files/fxs.rpcd $(1)/usr/libexec/rpcd/fxs
endef

define Package/luci-app-fxs/postinst
#!/bin/sh
[ -n "$$IPKG_INSTROOT" ] || {
	touch /www/luci-static/resources/view/fxs/*.js
	rm -rf /tmp/luci-indexcache* /tmp/luci-modulecache
	/etc/init.d/rpcd reload
}
exit 0
endef

$(eval $(call BuildPackage,luci-app-fxs))

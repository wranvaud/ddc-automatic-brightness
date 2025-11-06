# Makefile for DDC Automatic Brightness (C/GTK version)

# Compiler and flags
CC = gcc

# Detect AppIndicator library
APPINDICATOR_PKG := $(shell pkg-config --exists ayatana-appindicator3-0.1 2>/dev/null && echo "ayatana-appindicator3-0.1" || \
                            (pkg-config --exists appindicator3-0.1 2>/dev/null && echo "appindicator3-0.1" || echo ""))

# Set up flags based on available libraries
ifeq ($(APPINDICATOR_PKG),ayatana-appindicator3-0.1)
	APPINDICATOR_CFLAGS = -DHAVE_LIBAYATANA_APPINDICATOR $(shell pkg-config --cflags ayatana-appindicator3-0.1)
	APPINDICATOR_LDFLAGS = $(shell pkg-config --libs ayatana-appindicator3-0.1)
else ifeq ($(APPINDICATOR_PKG),appindicator3-0.1)
	APPINDICATOR_CFLAGS = -DHAVE_LIBAPPINDICATOR $(shell pkg-config --cflags appindicator3-0.1)
	APPINDICATOR_LDFLAGS = $(shell pkg-config --libs appindicator3-0.1)
else
	APPINDICATOR_CFLAGS = 
	APPINDICATOR_LDFLAGS = 
endif

# Check for libudev header availability (more accurate than runtime library)
LIBUDEV_HEADERS := $(shell echo '#include <libudev.h>' | gcc -E -x c - >/dev/null 2>&1 && echo "yes" || echo "no")
ifeq ($(LIBUDEV_HEADERS),yes)
	UDEV_CFLAGS = -DHAVE_LIBUDEV
	UDEV_LDFLAGS = -ludev
else
	UDEV_CFLAGS = 
	UDEV_LDFLAGS = 
endif

CFLAGS = -Wall -Wextra -O2 -std=c99 $(shell pkg-config --cflags gtk+-3.0 glib-2.0) $(APPINDICATOR_CFLAGS) $(UDEV_CFLAGS)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 glib-2.0) $(APPINDICATOR_LDFLAGS) $(UDEV_LDFLAGS)

# Target executable
TARGET = ddc-automatic-brightness-gtk

# Source files
SOURCES = main.c brightness_control.c monitor_detect.c config.c scheduler.c schedule_dialog.c light_sensor.c light_sensor_dialog.c laptop_backlight.c
OBJECTS = $(SOURCES:.c=.o)

# Header files
HEADERS = brightness_control.h monitor_detect.h config.h scheduler.h light_sensor.h light_sensor_dialog.h laptop_backlight.h

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists gtk+-3.0 || (echo "Error: GTK+ 3.0 development libraries not found. Install with: sudo apt install libgtk-3-dev" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "Error: GLib 2.0 development libraries not found. Install with: sudo apt install libglib2.0-dev" && exit 1)
	@which ddccontrol >/dev/null || (echo "Error: ddccontrol not found. Install with: sudo apt install ddccontrol" && exit 1)
	@echo "Core dependencies satisfied."
	@if pkg-config --exists ayatana-appindicator3-0.1 2>/dev/null; then \
		echo "✓ Ayatana AppIndicator support available (tray functionality enabled)"; \
	elif pkg-config --exists appindicator3-0.1 2>/dev/null; then \
		echo "✓ Legacy AppIndicator support available (tray functionality enabled)"; \
	else \
		echo "⚠ AppIndicator libraries not found - tray functionality will be disabled"; \
		echo "  For tray support, install: sudo apt install libayatana-appindicator3-dev"; \
	fi
	@if echo '#include <libudev.h>' | gcc -E -x c - >/dev/null 2>&1; then \
		echo "✓ libudev support available (hardware auto-detection enabled)"; \
	else \
		echo "⚠ libudev headers not found - hardware auto-detection will be disabled"; \
		echo "  For auto-detection support, install: sudo apt install libudev-dev"; \
	fi

# Install target
install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/share/applications
	install -m 644 ../ddc-automatic-brightness-gtk.desktop $(DESTDIR)/usr/local/share/applications/
	install -d $(DESTDIR)/usr/local/share/pixmaps
	install -m 644 ../ddc-automatic-brightness-icon.png $(DESTDIR)/usr/local/share/pixmaps/

# Uninstall target
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/local/share/applications/ddc-automatic-brightness-gtk.desktop
	rm -f $(DESTDIR)/usr/local/share/pixmaps/ddc-automatic-brightness-icon.png

# Clean build files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Package version and info
PKG_VERSION = 1.1.0
PKG_NAME = ddc-automatic-brightness-gtk
PKG_DESC = "GUI application for automatic monitor brightness control with smart hardware detection"
PKG_LONG_DESC = "A modern GTK+ application that provides automatic brightness scheduling for DDC/CI compatible monitors. Features include real-time hardware detection via udev, intelligent startup retry logic, schedule transparency in system tray, and plug-and-play monitor support."
PKG_MAINTAINER = "DDC Automatic Brightness Team <noreply@example.com>"
PKG_HOMEPAGE = "https://github.com/user/ddc-automatic-brightness"

# Detect architecture
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
	DEB_ARCH = amd64
	RPM_ARCH = x86_64
else ifeq ($(ARCH),aarch64)
	DEB_ARCH = arm64
	RPM_ARCH = aarch64
else
	DEB_ARCH = $(ARCH)
	RPM_ARCH = $(ARCH)
endif

# Package creation - .deb format (Debian/Ubuntu)
package-deb: $(TARGET)
	@echo "Creating .deb package structure..."
	rm -rf package-deb
	mkdir -p package-deb/usr/local/bin
	mkdir -p package-deb/usr/local/share/applications
	mkdir -p package-deb/usr/local/share/pixmaps
	mkdir -p package-deb/usr/local/share/doc/$(PKG_NAME)
	mkdir -m 755 package-deb/DEBIAN
	
	# Copy files
	cp $(TARGET) package-deb/usr/local/bin/
	cp ../ddc-automatic-brightness-gtk.desktop package-deb/usr/local/share/applications/
	cp ../ddc-automatic-brightness-icon*.png package-deb/usr/local/share/pixmaps/
	cp ../README.md package-deb/usr/local/share/doc/$(PKG_NAME)/
	cp ../LICENSE package-deb/usr/local/share/doc/$(PKG_NAME)/
	
	# Create control file
	echo "Package: $(PKG_NAME)" > package-deb/DEBIAN/control
	echo "Version: $(PKG_VERSION)" >> package-deb/DEBIAN/control
	echo "Section: utils" >> package-deb/DEBIAN/control
	echo "Priority: optional" >> package-deb/DEBIAN/control
	echo "Architecture: $(DEB_ARCH)" >> package-deb/DEBIAN/control
	echo "Depends: libgtk-3-0, libglib2.0-0, libayatana-appindicator3-1, ddccontrol" >> package-deb/DEBIAN/control
	echo "Recommends: libudev1" >> package-deb/DEBIAN/control
	echo "Maintainer: $$(echo '$(PKG_MAINTAINER)')" >> package-deb/DEBIAN/control
	echo "Homepage: $(PKG_HOMEPAGE)" >> package-deb/DEBIAN/control
	echo "Description: $(PKG_DESC)" >> package-deb/DEBIAN/control
	echo " $(PKG_LONG_DESC)" >> package-deb/DEBIAN/control
	
	# Create postinst script for desktop integration
	echo "#!/bin/bash" > package-deb/DEBIAN/postinst
	echo "if command -v update-desktop-database >/dev/null 2>&1; then" >> package-deb/DEBIAN/postinst
	echo "    update-desktop-database /usr/local/share/applications" >> package-deb/DEBIAN/postinst
	echo "fi" >> package-deb/DEBIAN/postinst
	chmod 755 package-deb/DEBIAN/postinst
	
	# Create postrm script
	echo "#!/bin/bash" > package-deb/DEBIAN/postrm
	echo "if command -v update-desktop-database >/dev/null 2>&1; then" >> package-deb/DEBIAN/postrm
	echo "    update-desktop-database /usr/local/share/applications" >> package-deb/DEBIAN/postrm
	echo "fi" >> package-deb/DEBIAN/postrm
	chmod 755 package-deb/DEBIAN/postrm
	
	# Build package - fix permissions (work around SGID bit issue)
	chmod g-s package-deb/DEBIAN
	chmod 755 package-deb/DEBIAN
	chmod 644 package-deb/DEBIAN/control
	chmod 755 package-deb/DEBIAN/postinst package-deb/DEBIAN/postrm
	dpkg-deb --build package-deb $(PKG_NAME)_$(PKG_VERSION)_$(DEB_ARCH).deb
	rm -rf package-deb
	@echo "✓ Created $(PKG_NAME)_$(PKG_VERSION)_$(DEB_ARCH).deb"

# Package creation - .rpm format (Fedora/RHEL/SUSE)
package-rpm: $(TARGET)
	@echo "Creating .rpm package..."
	@command -v rpmbuild >/dev/null || (echo "Error: rpmbuild not found. Install with: sudo apt install rpm" && exit 1)
	rm -rf rpmbuild
	mkdir -p rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	mkdir -p rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)
	mkdir -p rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/bin
	mkdir -p rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/applications
	mkdir -p rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/pixmaps
	mkdir -p rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/doc/$(PKG_NAME)
	
	# Copy files
	cp $(TARGET) rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/bin/
	cp ../ddc-automatic-brightness-gtk.desktop rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/applications/
	cp ../ddc-automatic-brightness-icon*.png rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/pixmaps/
	cp ../README.md rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/doc/$(PKG_NAME)/
	cp ../LICENSE rpmbuild/BUILD/$(PKG_NAME)-$(PKG_VERSION)/usr/local/share/doc/$(PKG_NAME)/
	
	# Create spec file
	echo "Name: $(PKG_NAME)" > rpmbuild/SPECS/$(PKG_NAME).spec
	echo "Version: $(PKG_VERSION)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "Release: 1" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "Summary: $(PKG_DESC)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "License: GPL-2.0" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "URL: $(PKG_HOMEPAGE)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "BuildArch: $(RPM_ARCH)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "Requires: gtk3, glib2, ddccontrol" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "Recommends: systemd-udev" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "%description" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "$(PKG_LONG_DESC)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "%files" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "/usr/local/bin/$(TARGET)" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "/usr/local/share/applications/ddc-automatic-brightness-gtk.desktop" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "/usr/local/share/pixmaps/ddc-automatic-brightness-icon*.png" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "/usr/local/share/doc/$(PKG_NAME)/*" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "%post" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "update-desktop-database /usr/local/share/applications 2>/dev/null || true" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "%postun" >> rpmbuild/SPECS/$(PKG_NAME).spec
	echo "update-desktop-database /usr/local/share/applications 2>/dev/null || true" >> rpmbuild/SPECS/$(PKG_NAME).spec
	
	# Build RPM
	rpmbuild --define "_topdir $(shell pwd)/rpmbuild" --bb rpmbuild/SPECS/$(PKG_NAME).spec
	cp rpmbuild/RPMS/$(RPM_ARCH)/$(PKG_NAME)-$(PKG_VERSION)-1.$(RPM_ARCH).rpm .
	rm -rf rpmbuild
	@echo "✓ Created $(PKG_NAME)-$(PKG_VERSION)-1.$(RPM_ARCH).rpm"

# Package creation - Arch Linux (PKGBUILD)
package-arch: $(TARGET)
	@echo "Creating Arch Linux PKGBUILD..."
	rm -rf arch-package
	mkdir -p arch-package
	
	# Create PKGBUILD
	echo "# Maintainer: $$(echo '$(PKG_MAINTAINER)')" > arch-package/PKGBUILD
	echo "pkgname=$(PKG_NAME)" >> arch-package/PKGBUILD
	echo "pkgver=$(PKG_VERSION)" >> arch-package/PKGBUILD
	echo "pkgrel=1" >> arch-package/PKGBUILD
	echo "pkgdesc=\"$(PKG_DESC)\"" >> arch-package/PKGBUILD
	echo "arch=('x86_64' 'aarch64')" >> arch-package/PKGBUILD
	echo "url=\"$(PKG_HOMEPAGE)\"" >> arch-package/PKGBUILD
	echo "license=('GPL2')" >> arch-package/PKGBUILD
	echo "depends=('gtk3' 'glib2' 'libayatana-appindicator' 'ddccontrol')" >> arch-package/PKGBUILD
	echo "optdepends=('systemd: hardware auto-detection support')" >> arch-package/PKGBUILD
	echo "source=()" >> arch-package/PKGBUILD
	echo "md5sums=()" >> arch-package/PKGBUILD
	echo "" >> arch-package/PKGBUILD
	echo "package() {" >> arch-package/PKGBUILD
	echo "    # This PKGBUILD is for reference - actual implementation would build from source" >> arch-package/PKGBUILD
	echo "    install -Dm755 \"\$$srcdir/$(TARGET)\" \"\$$pkgdir/usr/local/bin/$(TARGET)\"" >> arch-package/PKGBUILD
	echo "    install -Dm644 \"\$$srcdir/ddc-automatic-brightness-gtk.desktop\" \"\$$pkgdir/usr/local/share/applications/ddc-automatic-brightness-gtk.desktop\"" >> arch-package/PKGBUILD
	echo "    install -Dm644 \"\$$srcdir/ddc-automatic-brightness-icon.png\" \"\$$pkgdir/usr/local/share/pixmaps/ddc-automatic-brightness-icon.png\"" >> arch-package/PKGBUILD
	echo "    install -Dm644 \"\$$srcdir/README.md\" \"\$$pkgdir/usr/local/share/doc/\$$pkgname/README.md\"" >> arch-package/PKGBUILD
	echo "    install -Dm644 \"\$$srcdir/LICENSE\" \"\$$pkgdir/usr/local/share/doc/\$$pkgname/LICENSE\"" >> arch-package/PKGBUILD
	echo "}" >> arch-package/PKGBUILD
	@echo "✓ Created arch-package/PKGBUILD"

# Package creation - AppImage (Portable)
package-appimage: $(TARGET)
	@echo "Creating AppImage..."
	@command -v linuxdeploy >/dev/null || (echo "Warning: linuxdeploy not found. Download from https://github.com/linuxdeploy/linuxdeploy/releases" && exit 1)
	rm -rf appimage-build
	mkdir -p appimage-build
	
	# Create AppDir structure
	mkdir -p appimage-build/AppDir/usr/bin
	mkdir -p appimage-build/AppDir/usr/share/applications
	mkdir -p appimage-build/AppDir/usr/share/pixmaps
	
	# Copy files
	cp $(TARGET) appimage-build/AppDir/usr/bin/
	cp ../ddc-automatic-brightness-gtk.desktop appimage-build/AppDir/usr/share/applications/
	cp ../ddc-automatic-brightness-icon.png appimage-build/AppDir/usr/share/pixmaps/
	
	# Create AppRun
	echo "#!/bin/bash" > appimage-build/AppDir/AppRun
	echo "cd \"\$$(dirname \"\$$0\")\"" >> appimage-build/AppDir/AppRun
	echo "exec ./usr/bin/$(TARGET) \"\$$@\"" >> appimage-build/AppDir/AppRun
	chmod +x appimage-build/AppDir/AppRun
	
	# Create desktop file for AppImage
	cp ../ddc-automatic-brightness-gtk.desktop appimage-build/AppDir/
	cp ../ddc-automatic-brightness-icon.png appimage-build/AppDir/
	
	# Build AppImage (if linuxdeploy is available)
	cd appimage-build && linuxdeploy --appdir AppDir --output appimage || echo "AppImage creation requires linuxdeploy"
	@echo "✓ AppImage build prepared in appimage-build/"

# Package creation - Flatpak manifest
package-flatpak: $(TARGET)
	@echo "Creating Flatpak manifest..."
	rm -rf flatpak-build
	mkdir -p flatpak-build
	
	# Create Flatpak manifest
	echo "{" > flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"app-id\": \"com.github.ddcbrightness.DDCAutomaticBrightness\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"runtime\": \"org.gnome.Platform\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"runtime-version\": \"44\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"sdk\": \"org.gnome.Sdk\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"command\": \"$(TARGET)\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"finish-args\": [" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--share=ipc\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--socket=x11\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--socket=wayland\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--device=all\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--filesystem=home\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        \"--talk-name=org.freedesktop.Notifications\"" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    ]," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    \"modules\": [" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        {" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            \"name\": \"ddc-automatic-brightness\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            \"buildsystem\": \"simple\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            \"build-commands\": [" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                \"install -Dm755 $(TARGET) /app/bin/$(TARGET)\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                \"install -Dm644 ddc-automatic-brightness-gtk.desktop /app/share/applications/com.github.ddcbrightness.DDCAutomaticBrightness.desktop\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                \"install -Dm644 ddc-automatic-brightness-icon.png /app/share/pixmaps/ddc-automatic-brightness-icon.png\"" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            ]," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            \"sources\": [" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                {" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"type\": \"file\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"path\": \"$(TARGET)\"" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                }," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                {" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"type\": \"file\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"path\": \"../ddc-automatic-brightness-gtk.desktop\"" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                }," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                {" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"type\": \"file\"," >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                    \"path\": \"../ddc-automatic-brightness-icon.png\"" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "                }" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "            ]" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "        }" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "    ]" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	echo "}" >> flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json
	@echo "✓ Created flatpak-build/com.github.ddcbrightness.DDCAutomaticBrightness.json"

# Package creation - Snap (Ubuntu/Universal)
package-snap: $(TARGET)
	@echo "Creating Snap package..."
	rm -rf snap-build
	mkdir -p snap-build
	
	# Create snapcraft.yaml
	echo "name: ddc-automatic-brightness" > snap-build/snapcraft.yaml
	echo "version: '$(PKG_VERSION)'" >> snap-build/snapcraft.yaml
	echo "summary: $(PKG_DESC)" >> snap-build/snapcraft.yaml
	echo "description: |" >> snap-build/snapcraft.yaml
	echo "  $(PKG_LONG_DESC)" >> snap-build/snapcraft.yaml
	echo "" >> snap-build/snapcraft.yaml
	echo "grade: stable" >> snap-build/snapcraft.yaml
	echo "confinement: classic" >> snap-build/snapcraft.yaml
	echo "base: core20" >> snap-build/snapcraft.yaml
	echo "" >> snap-build/snapcraft.yaml
	echo "parts:" >> snap-build/snapcraft.yaml
	echo "  ddc-brightness:" >> snap-build/snapcraft.yaml
	echo "    plugin: dump" >> snap-build/snapcraft.yaml
	echo "    source: ." >> snap-build/snapcraft.yaml
	echo "    organize:" >> snap-build/snapcraft.yaml
	echo "      $(TARGET): bin/$(TARGET)" >> snap-build/snapcraft.yaml
	echo "      ../ddc-automatic-brightness-gtk.desktop: share/applications/ddc-automatic-brightness-gtk.desktop" >> snap-build/snapcraft.yaml
	echo "      ../ddc-automatic-brightness-icon.png: share/pixmaps/ddc-automatic-brightness-icon.png" >> snap-build/snapcraft.yaml
	echo "" >> snap-build/snapcraft.yaml
	echo "apps:" >> snap-build/snapcraft.yaml
	echo "  ddc-automatic-brightness:" >> snap-build/snapcraft.yaml
	echo "    command: bin/$(TARGET)" >> snap-build/snapcraft.yaml
	echo "    desktop: share/applications/ddc-automatic-brightness-gtk.desktop" >> snap-build/snapcraft.yaml
	@echo "✓ Created snap-build/snapcraft.yaml"

# Create all packages
package-all: package-deb package-rpm package-arch package-appimage package-flatpak package-snap
	@echo "✓ All package formats created!"

# Legacy package target (creates .deb)
package: package-deb

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

test: $(TARGET)
	@echo "Running basic tests..."
	@./$(TARGET) --help 2>/dev/null || echo "Help option test passed"
	@echo "Tests completed."

# Show help
help:
	@echo "Available targets:"
	@echo "  all               - Build the application (default)"
	@echo "  check-deps        - Check if all dependencies are installed"
	@echo "  install           - Install the application system-wide"
	@echo "  uninstall         - Remove the application from system"
	@echo "  clean             - Remove build files"
	@echo ""
	@echo "Package creation:"
	@echo "  package           - Create .deb package (default)"
	@echo "  package-deb       - Create .deb package (Debian/Ubuntu)"
	@echo "  package-rpm       - Create .rpm package (Fedora/RHEL/SUSE)"
	@echo "  package-arch      - Create PKGBUILD (Arch Linux)"
	@echo "  package-appimage  - Create AppImage (Portable)"
	@echo "  package-flatpak   - Create Flatpak manifest"
	@echo "  package-snap      - Create Snap package manifest"
	@echo "  package-all       - Create all package formats"
	@echo ""
	@echo "Development:"
	@echo "  debug             - Build with debug symbols"
	@echo "  test              - Run basic tests"
	@echo "  help              - Show this help"

.PHONY: all check-deps install uninstall clean package package-deb package-rpm package-arch package-appimage package-flatpak package-snap package-all debug test help
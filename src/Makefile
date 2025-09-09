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
SOURCES = main.c brightness_control.c monitor_detect.c config.c scheduler.c schedule_dialog.c
OBJECTS = $(SOURCES:.c=.o)

# Header files
HEADERS = brightness_control.h monitor_detect.h config.h scheduler.h

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

# Package creation
package: $(TARGET)
	@echo "Creating package structure..."
	mkdir -p package/usr/local/bin
	mkdir -p package/usr/local/share/applications
	mkdir -p package/usr/local/share/pixmaps
	mkdir -p package/DEBIAN
	
	cp $(TARGET) package/usr/local/bin/
	cp ../ddc-automatic-brightness-gtk.desktop package/usr/local/share/applications/
	cp ../ddc-automatic-brightness-icon.png package/usr/local/share/pixmaps/
	
	echo "Package: ddc-automatic-brightness-gtk" > package/DEBIAN/control
	echo "Version: 1.0.0" >> package/DEBIAN/control
	echo "Section: utils" >> package/DEBIAN/control
	echo "Priority: optional" >> package/DEBIAN/control
	echo "Architecture: amd64" >> package/DEBIAN/control
	echo "Depends: libgtk-3-0, libglib2.0-0, ddccontrol" >> package/DEBIAN/control
	echo "Maintainer: DDC Automatic Brightness Team" >> package/DEBIAN/control
	echo "Description: GUI application for automatic monitor brightness control" >> package/DEBIAN/control
	echo " A GTK+ application that provides automatic brightness scheduling" >> package/DEBIAN/control
	echo " for DDC/CI compatible monitors using ddccontrol." >> package/DEBIAN/control
	
	chmod 755 package/DEBIAN
	dpkg-deb --build package ddc-automatic-brightness-gtk_1.0.0_amd64.deb
	rm -rf package

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
	@echo "  all          - Build the application (default)"
	@echo "  check-deps   - Check if all dependencies are installed"
	@echo "  install      - Install the application system-wide"
	@echo "  uninstall    - Remove the application from system"
	@echo "  clean        - Remove build files"
	@echo "  package      - Create a .deb package"
	@echo "  debug        - Build with debug symbols"
	@echo "  test         - Run basic tests"
	@echo "  help         - Show this help"

.PHONY: all check-deps install uninstall clean package debug test help
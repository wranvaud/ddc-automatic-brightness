# DDC Automatic Brightness

A GTK application for controlling external monitor brightness using DDC/CI with automatic adjustment capabilities.

## Features

**Automatic Brightness Control**
- Ambient light sensor-based adjustment with configurable curves
- Follow main/internal monitor brightness
- Time-based scheduled brightness throughout the day
- Smooth gradual transitions

**Monitor Control**
- Manual brightness adjustment via slider
- Support for multiple external monitors
- Per-monitor configuration and settings
- Real-time hardware detection (with udev)

## Installation

### Prerequisites

```bash
# Ubuntu/Debian (Required)
sudo apt install build-essential libgtk-3-dev libglib2.0-dev libayatana-appindicator3-dev ddccontrol

# Ubuntu/Debian (Optional - for hardware auto-detection)
sudo apt install libudev-dev

# Fedora
sudo dnf install gcc gtk3-devel glib2-devel libayatana-appindicator-gtk3-devel ddccontrol libudev-devel

# Arch Linux
sudo pacman -S base-devel gtk3 glib2 libayatana-appindicator ddccontrol systemd-libs
```

### Build and Install

```bash
cd src/
make check-deps  # Verify all dependencies
make             # Build the application

# Install system-wide
sudo make install

# Or create package for distribution
make package
sudo dpkg -i ddc-automatic-brightness-gtk_1.1.0_amd64.deb
```

## Usage

### Command Line Options

```bash
./ddc-automatic-brightness-gtk [options]

Options:
  --tray, --minimized  Start minimized to system tray
  --no-gui             Run in background (tray only)
  --help, -h           Show help
```

### GUI Controls

**Monitor Selection**: Choose your external monitor from dropdown

**Manual Control**: Use slider for immediate brightness adjustment

**Auto Brightness Modes**:
- Ambient Light Sensor: Automatic adjustment based on ambient light
- Follow Main Monitor: Match internal/main monitor brightness
- Time Schedule: Follow daily brightness schedule

**Configuration Dialogs**:
- Configure Light Sensor Curve: Visual graph with lux-to-brightness mapping
- Configure Schedule: 24-hour brightness timeline with visual graph
- Luminosity Sensitivity: Adjust hysteresis to prevent flickering (0-100 lux)

### Configuration

Settings stored in `~/.config/ddc-automatic-brightness/config.ini`:

## Technical Details

### Architecture

```
src/
├── main.c                  # Application core and tray integration
├── brightness_control.c    # DDC/CI monitor communication
├── monitor_detect.c        # Monitor discovery and management
├── light_sensor.c          # Ambient light sensor integration
├── laptop_backlight.c      # Internal monitor brightness reading
├── scheduler.c             # Time-based brightness scheduling
├── config.c                # Configuration management
├── *_dialog.c              # Configuration UI dialogs
└── *.h                     # Header files
```

### Dependencies

- **GTK 3.0**: GUI framework
- **GLib 2.0**: Core utilities and configuration
- **libayatana-appindicator3**: System tray support
- **ddccontrol**: DDC/CI monitor communication
- **libudev** (optional): Hardware auto-detection
- **Cairo**: Graph rendering

### Ambient Light Sensor Requirements

Requires IIO (Industrial I/O) ambient light sensor via `/sys/bus/iio/devices/`. Supported on laptops with built-in ALS hardware.

## Troubleshooting

### No Monitors Detected

```bash
# Test ddccontrol directly
sudo ddccontrol -p

# Check permissions
sudo usermod -a -G i2c $USER
# Log out and back in

# Manual refresh in GUI
Click "Refresh Monitors" button
```

### Brightness Not Changing

```bash
# Verify DDC/CI support
sudo ddccontrol dev:/dev/i2c-X -r 0x10

# Check monitor is available
The app shows monitor status in the dropdown
```

### No Tray Icon

```bash
# Install tray support
sudo apt install libayatana-appindicator3-dev

# Rebuild
make clean && make
```

### Hardware Auto-Detection Not Working

```bash
# Install udev development headers
sudo apt install libudev-dev

# Rebuild with hardware detection
make clean && make check-deps && make
```

## License

GNU General Public License v2.0

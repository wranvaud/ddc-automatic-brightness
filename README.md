# DDC Automatic Brightness

A native C/GTK application for controlling monitor brightness using ddccontrol with automatic scheduling and system tray support.

## Features

✅ **System Tray Support**: Full tray integration with brightness controls and auto-brightness toggle  
✅ **Monitor Detection**: Automatic DDC/CI compatible monitor discovery  
✅ **Brightness Control**: Real-time brightness adjustment with slider  
✅ **Automatic Scheduling**: Progressive brightness adjustment throughout the day  
✅ **Flexible Schedule**: Add/remove custom time points and brightness levels  
✅ **Startup Options**: Run minimized to tray, --help, --tray modes  

## Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install build-essential libgtk-3-dev libglib2.0-dev libayatana-appindicator3-dev ddccontrol

# Fedora
sudo dnf install gcc gtk3-devel glib2-devel libayatana-appindicator-gtk3-devel ddccontrol

# Arch Linux
sudo pacman -S base-devel gtk3 glib2 libayatana-appindicator ddccontrol
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
sudo dpkg -i ddc-automatic-brightness-gtk_1.0.0_amd64.deb
```

### Quick Test

Go to the folder src/.
After making changes run "make".

```bash
# Test compilation
./ddc-automatic-brightness-gtk --help

# Run with tray support
./ddc-automatic-brightness-gtk --tray
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

1. **Monitor Selection**: Choose your monitor from dropdown
2. **Brightness Slider**: Real-time brightness control (click to jump to position)
3. **Auto Brightness**: Enable/disable scheduled brightness changes
4. **Configure Schedule**: Set custom time/brightness points
5. **Startup Options**: "Start minimized to system tray" checkbox

### Configuration

Settings stored in `~/.config/ddc_automatic_brightness.conf`:
- Monitor selection preferences
- Per-monitor auto brightness settings  
- Custom brightness schedules
- Startup options

## Technical Details

### Architecture

```
src/
├── main.c              # GTK application + tray integration
├── brightness_control.c # Monitor DDC/CI communication
├── monitor_detect.c    # Monitor discovery via ddccontrol
├── config.c           # GLib GKeyFile configuration
├── scheduler.c        # Time-based brightness scheduling
├── schedule_dialog.c  # Schedule configuration UI
└── *.h               # Header files
```

### Dependencies

- **GTK 3.0**: Modern GUI framework
- **GLib 2.0**: Core utilities and configuration
- **libayatana-appindicator3**: System tray support
- **ddccontrol**: Monitor communication

## Development

### Building

```bash
make clean && make  # Clean build
make debug          # Debug version with symbols
make test           # Basic functionality tests
```

### Package Creation

```bash
make package        # Creates .deb package
make install        # System-wide installation
make uninstall      # Complete removal
```

### Contributing

This implementation uses patterns consistent with the ddccontrol project and could potentially be contributed upstream as an enhanced GUI with automatic brightness features.

## Troubleshooting

### No Tray Icon
```bash
# Install tray support
sudo apt install libayatana-appindicator3-dev

# Check if tray is detected
make check-deps
```

### No Monitors Detected
```bash
# Test ddccontrol directly
sudo ddccontrol -p

# Check permissions
sudo usermod -a -G i2c $USER
# Log out and back in
```

### Compilation Errors
```bash
# Verify all dependencies
make check-deps

# Check Ubuntu/Debian packages
sudo apt install build-essential libgtk-3-dev libglib2.0-dev
```

## License

The project is licensed under `GNU General Public License v2.0` license.

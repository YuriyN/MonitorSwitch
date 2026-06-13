# Monitor Switch

A simple Linux CLI app for switching inputs on DDC/CI-supported monitors. It
uses `ddcutil` and includes an interactive ncurses menu.

## Requirements

- Linux
- `ddcutil`
- DDC/CI enabled in the monitor settings
- Access to `/dev/i2c-*`
- CMake 3.16+, a C++17 compiler, and ncurses development files to build

Install `ddcutil` with your package manager:

```bash
# Arch Linux
sudo pacman -S ddcutil

# Debian / Ubuntu
sudo apt install ddcutil

# Fedora
sudo dnf install ddcutil
```

Load the I2C module and grant your user device access if needed:

```bash
sudo modprobe i2c-dev
sudo usermod -aG i2c "$USER"
```

Log out and back in after changing groups.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

## Usage

```bash
monitor-switch                 # interactive menu
monitor-switch dp-1            # DisplayPort 1
monitor-switch dp-2            # DisplayPort 2 (standard VCP value 0x10)
monitor-switch hdmi-1          # HDMI 1
monitor-switch hdmi-2          # HDMI 2
monitor-switch usb-c           # USB-C
monitor-switch auto            # automatic input selection
monitor-switch --current       # show current input
monitor-switch --detect        # list detected monitors
monitor-switch --doctor        # diagnose setup problems
monitor-switch --list          # list known input values
```

When one compatible monitor is detected, it is selected automatically. With
multiple monitors, use the interactive menu or select one explicitly:

```bash
monitor-switch dp-1 --display 2
monitor-switch dp-1 --bus 4
monitor-switch dp-1 --serial ABC123
monitor-switch dp-1 --monitor "Display Model"
```

Input-source values vary between monitor models. Use `monitor-switch --list`
for the built-in mappings or pass a raw VCP value such as `0x0f`.

The interactive menu asks the selected monitor for its advertised VCP `0x60`
values and shows only those inputs when possible. Monitor capability strings
are not always accurate, so the menu falls back to the built-in mappings and
raw values remain available.

Run `ddcutil detect` if a monitor does not appear.

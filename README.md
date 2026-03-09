
# ds4linux

A native Linux clone of [DS4Windows](https://ds4-windows.com/download/) - a daemon that bridges Sony DualShock 4 / DualSense controllers to a virtual DS4 device using `libevdev`, `uinput`, and `hidraw`. It grabs exclusive access to the physical controller, creates a virtual DualShock 4 device, and forwards all input and LED control. This hides the real hardware from other applications (including Steam) so only the clean virtual device is visible.

> **⚠️ GUI Notice:** The Qt 6 GUI portion of this project has been **scratched** and is no longer maintained. The code remains in the `gui/` directory but **GUI features may not work**. Only the daemon is actively supported. **If there is high user demand, I may pick up GUI development again in the future.**


## How It Works

The daemon:
1. Detects connected DualShock 4 / DualSense controllers.
2. Grabs exclusive access via `libevdev` so no other process sees the raw device.
3. Creates a virtual DualShock 4 controller via Linux `uinput`.
4. Forwards all input events.
5. Installs udev rules to hide the physical controller and expose only the virtual one.


## Prerequisites

- Arch Linux
- `cmake`, `ninja`, `gcc`
- `libevdev`
- `nlohmann-json`


## Installation

Pre-built packages are available on the [GitHub Releases](https://github.com/PalashDalsaniya/ds4linux/releases) page. Download the appropriate package for your distribution and install as follows:

### Debian / Ubuntu / Linux Mint (.deb)

```bash
sudo apt install ./ds4linux-*-Linux.deb
```

### Fedora (.rpm)

```bash
sudo dnf install ./ds4linux-*-Linux.rpm
```

### Bazzite (Immutable OS)

```bash
sudo rpm-ostree install ./ds4linux-*-Linux.rpm
systemctl reboot
```

### Arch (Install Script)

```bash
git clone https://github.com/ds4linux/ds4linux.git
cd ds4linux
chmod +x scripts/install.sh
./scripts/install.sh
```

The install script will:
1. Install build dependencies via `pacman`.
2. Build the daemon.
3. Install the binary to `/usr/bin/`.
4. Copy the udev rules to `/etc/udev/rules.d/` and reload them.
5. Ensure the `uinput` kernel module is loaded.

### Manual Build

```bash
# Install dependencies
sudo pacman -S --needed cmake ninja gcc libevdev nlohmann-json

# Clone Repo
git clone https://github.com/PalashDalsaniya/ds4linux.git
cd ds4linux

# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install
sudo cmake --install build --prefix /usr

# Copy udev rules
sudo cp config/99-ds4linux.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# Load uinput module
sudo modprobe uinput
echo "uinput" | sudo tee /etc/modules-load.d/ds4linux.conf > /dev/null
```


## Usage

> **Important:** Connect your DualShock 4 or DualSense controller **before** starting the daemon. The daemon scans for controllers once at startup.

### 1. Connect your controller

Connect your controller via **USB or Bluetooth**. The daemon must be started *after* the controller is connected, it only scans for devices once on launch.

### 2. Start the daemon

Every time you want to use ds4linux, run:

```bash
sudo ds4linux-daemon
```

Press `Ctrl+C` to stop the daemon when you're done.

### 3. Launch Steam (or any other game launcher)

For best compatibility, **open Steam after the daemon is running**. This ensures Steam only sees the virtual DS4 device and avoids conflicts with the daemon's exclusive grab on the physical controller.


## Project Structure

```
ds4linux/
├── CMakeLists.txt              # Top-level build
├── cmake/                      # CMake find-modules
├── common/                     # Shared library (IPC protocol, profile types)
│   ├── include/ds4linux/
│   └── src/
├── daemon/                     # Backend daemon
│   ├── include/ds4linux/
│   └── src/
├── gui/                        # Qt 6 frontend (⚠️ not maintained)
│   ├── include/ds4linux/
│   └── src/
├── config/                     # udev rules, systemd unit, default profile
└── scripts/                    # Build & install script
```


## Troubleshooting

- **Daemon doesn't detect controller:** Make sure the controller is connected (USB or Bluetooth) *before* starting the daemon.
- **Steam shows duplicate controllers:** Ensure the udev rules are installed (`/etc/udev/rules.d/99-ds4linux.rules`) and that Steam was launched *after* the daemon.
- **Permission errors:** The daemon needs root. Run with `sudo`.


## Game Compatibility

### Working

| Game | Status |
| ---- | ------ |
| Hades | ✅ Works |
| The Stanley Parable: Ultra Deluxe | ✅ Works |

### Not Working

| Game | Status | Notes |
| ---- | ------ | ----- |
| Detroit: Become Human | ❌ Does not work | works with [SenseShock](https://github.com/muhammad23012009/senseshock) |

If a game doesn't work with ds4linux, you can try [SenseShock](https://github.com/muhammad23012009/senseshock) as an alternative.

If you have tested ds4linux with a game, please let me know so I can add it to this list! You can contact me on Discord: **palashd**

# Roadmap

- Fix rumble functionality (currently not working)
- Resurrect GUI development
- take input from xbox or generic controller and translate to dualshock
- Add input support for standard Xbox, Nintendo Switch Pro, Dualshock 3, and Generic controllers (auto detected)
- Add Xbox 360 controller emulation
- have option to switch and choose between virtual ds4 or xbox360
- Add Visual Controller Tester
- options to control everything without GUI using CLI
- custom lightbar colors on supported controllers
- Custom settings profiles
- button remapping
- Emulate Dualsense so it can connect it by bluetooth and have functional adaptive triggers (for games that only support it on usb)

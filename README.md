
# ds4linux

A native Linux clone of [DS4Windows](https://ds4-windows.com/download/) - a daemon that bridges Sony DualShock 4 / DualSense controllers to a virtual DS4 device using `libevdev`, `uhid`, and `hidraw`. It grabs exclusive access to the physical controller, creates a virtual DualShock 4 (or Xbox 360) controller, and forwards input, rumble and lightbar control, with per-controller profiles for deadzones, button remapping, rumble strength and lightbar colour. This hides the real hardware from other applications (including Steam) so only the clean virtual device is visible.

## How It Works

The daemon:
1. Detects connected DualShock 4 / DualSense controllers.
2. Grabs exclusive access via `libevdev` so no other process sees the raw device.
3. Creates a virtual DualShock 4 controller via Linux `uhid` (with its own hidraw node, touchpad and motion sensors), or a virtual Xbox 360 controller via `uinput`.
4. Forwards all input events through the controller's profile (deadzones, button remaps), and rumble / lightbar output back to the controller.
5. Hides the physical controller from other programs while it is in use, and exposes only the virtual one. When the daemon stops, the controller is handed back and works as a normal (raw) controller again.

You control it with the `ds4linux-ctl` command or the `ds4linux-gui` app.


## Prerequisites

- Arch Linux
- `cmake`, `ninja`, `gcc`
- `libevdev`, `libudev`
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
git clone https://github.com/PalashDalsaniya/ds4linux.git
cd ds4linux
chmod +x scripts/install.sh
./scripts/install.sh
```

The install script will:
1. Install build dependencies via `pacman`.
2. Build the daemon.
3. Install the binary to `/usr/bin/`.
4. Copy the udev rules to `/etc/udev/rules.d/` and reload them.
5. Ensure the `uhid` kernel module is loaded.

### Manual Build

```bash
# Install dependencies
sudo pacman -S --needed cmake ninja gcc libevdev systemd nlohmann-json

# Clone Repo
git clone https://github.com/PalashDalsaniya/ds4linux.git
cd ds4linux

# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install
sudo cmake --install build --prefix /usr
sudo systemctl daemon-reload

# Copy udev rules
sudo cp config/99-ds4linux.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# Load the uhid (virtual DS4) and uinput (virtual Xbox 360) modules
sudo modprobe uhid uinput
printf 'uhid\nuinput\n' | sudo tee /etc/modules-load.d/ds4linux.conf > /dev/null
```

To also build the GUI (needs Qt 6: `sudo pacman -S qt6-base`), add `-DDS4LINUX_BUILD_GUI=ON` to the first `cmake` command.


## Usage

Controllers can be connected before or after the daemon starts. The daemon monitors input hot-plug events and automatically opens supported controllers.

### 1. Connect your controller

Connect your controller via **USB or Bluetooth**. ds4linux detects supported controllers while it is running.

### 2. Start ds4linux when you want it

ds4linux runs as a systemd service that is **not** started automatically, so your controllers stay normal controllers until you turn emulation on. Start and stop it from the GUI (**Start ds4linux** / **Stop ds4linux**) or the command line:

```bash
ds4linux-ctl start       # start emulation: hides the physical controllers
ds4linux-ctl stop        # stop it: the raw controllers work again
ds4linux-ctl restart
```

You don't need `sudo`. Starting or stopping asks for your password once (polkit), and it is remembered for a few minutes. In the GUI you can also tick **Start ds4linux when this app opens**.

To have it running all the time instead:

```bash
ds4linux-ctl autostart on    # start at boot   (off: don't; no argument: show)
```

Extra daemon options go in `/etc/default/ds4linux`, for example `DS4LINUX_ARGS=--generic`. Restart ds4linux after changing it.

You can also run the daemon in a terminal for debugging: `sudo ds4linux-daemon --debug` (`Ctrl+C` stops it). If it is killed instead, the controller stays hidden until ds4linux next starts and stops, or you reboot.

### 3. Launch Steam (or any other game launcher)

**Start ds4linux before Steam.** A program that opened the physical controller before ds4linux hid it keeps seeing it, so Steam would show two controllers. If Steam was already running, restart it; the daemon log names any program that still has the controller open.

### Controlling the daemon: `ds4linux-ctl`

Everything can be done from the command line while the daemon runs. Commands that act on controllers apply to all of them unless you pick one with `-d <number>` (from `list`):

```bash
ds4linux-ctl list                          # controllers, battery, profile, emulated controller
ds4linux-ctl test                          # live view of the controller's input
ds4linux-ctl lightbar '#ff0080'            # try a lightbar colour
ds4linux-ctl rumble 200 100 500            # test rumble: heavy, light, milliseconds

ds4linux-ctl profile new Racing            # create a profile (copy of Default)
ds4linux-ctl profile set Racing output x360        # emulate an Xbox 360 controller
ds4linux-ctl profile set Racing lightbar '#ff4400'
ds4linux-ctl profile set Racing rumble 60          # rumble strength in percent
ds4linux-ctl profile set Racing left-stick 10,90,circular   # inner%, outer%, type
ds4linux-ctl profile set Racing r2 5               # trigger deadzone
ds4linux-ctl profile remap Racing cross circle     # Cross acts as Circle
ds4linux-ctl profile remap Racing ps none          # disable the PS button
ds4linux-ctl -d 1 profile use Racing               # use it on controller 1 (remembered)
```

Run `ds4linux-ctl --help` for the full list (including `profile import` / `export`).

### GUI

The GUI is not in the release packages yet; build it from source with `-DDS4LINUX_BUILD_GUI=ON` (see [Manual Build](#manual-build)). `ds4linux-gui` shows the connected controllers (battery, profile, emulated controller), edits profiles (emulated controller, lightbar, rumble strength, stick and trigger deadzones, button remapping) and has a live controller tester that shows both the raw input and what games receive after the profile. If ds4linux isn't running, the GUI offers a **Start ds4linux** button; while it runs, **Stop ds4linux** and **Start at boot** are in the status bar.

### Profiles

Profiles are stored by the daemon in `/var/lib/ds4linux/profiles/`, and each controller remembers the profile it was last set to. A profile sets:

- **Emulated controller:** DualShock 4 (default) or Xbox 360. Xbox 360 mode is for games that only support Xbox controllers; it has no touchpad, motion sensors or lightbar control from games.
- **Lightbar colour** and **rumble strength**.
- **Deadzones** for both sticks (circular or square) and both triggers.
- **Button remapping**: any button can act as another button, or be disabled.

### Generic (non-Sony) controllers

ds4linux can also present other gamepads (Xbox, Logitech, 8BitDo, ...) as a virtual DualShock 4, so games show PlayStation button prompts. This is off by default because the daemon takes exclusive control of every controller it emulates:

```bash
sudo ds4linux-daemon --generic                  # every non-Sony gamepad
sudo ds4linux-daemon --generic=046d:c21f        # only these VID:PID pairs (see `lsusb`)
```

Sticks, buttons, triggers, the D-pad and rumble are forwarded. Controllers without a touchpad or motion sensors report them as idle, and there is no lightbar.

Limitations:
- Only gamepads the kernel exposes with standard gamepad buttons are supported. Pads with a mode switch (e.g. Logitech F710) must be in XInput mode; DirectInput / joystick-class pads are not detected.
- The controller's `/dev/input/js*` node stays visible but receives no input while the daemon runs.


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
├── cli/                        # ds4linux-ctl command-line tool
├── gui/                        # Qt 6 frontend
│   ├── include/ds4linux/
│   └── src/
├── tests/                      # Unit and kernel-level tests (ctest)
├── config/                     # udev rules, systemd unit, default profile
└── scripts/                    # Build & install script
```


## Troubleshooting

- **Daemon doesn't detect controller:** Confirm it is connected over USB or Bluetooth and that ds4linux is running (`ds4linux-ctl ping`).
- **Steam shows duplicate controllers:** Steam opened the controller before ds4linux started. Restart Steam (or start ds4linux first). `journalctl -u ds4linux` names any program that still has the physical controller open.
- **The raw controller doesn't work after stopping ds4linux:** this only happens if a daemon started by hand was killed. Reconnect the controller, or start and stop ds4linux once (it removes what the killed one left behind).
- **Permission errors:** The daemon itself runs as root (as the `ds4linux` service, or with `sudo` in a terminal); `ds4linux-ctl` and the GUI don't need root.
- **`ds4linux-ctl` / the GUI can't connect:** The daemon isn't running (or was started with a different `--socket`). Start it with `ds4linux-ctl start` (or the GUI).
- **Xbox 360 mode falls back to DualShock 4:** The `uinput` module isn't loaded. Run `sudo modprobe uinput`.
- **No rumble or lightbar from a game:** Run `sudo ds4linux-daemon --debug`. Every rumble / lightbar report a game sends is logged (`app output via ...`), as is every motor change sent to the controller (`motors heavy=... light=...`). If no `app output` lines appear, the game isn't sending rumble to the virtual controller.


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

- [x] Fix rumble functionality
- [x] Resurrect GUI development
- [x] take input from xbox or generic controller and translate to dualshock (`--generic`)
- [ ] Add input support for standard Xbox, Nintendo Switch Pro, Dualshock 3, and Generic controllers (auto detected)
- [x] Add Xbox 360 controller emulation
- [x] have option to switch and choose between virtual ds4 or xbox360
- [x] Add Visual Controller Tester
- [x] options to control everything without GUI using CLI
- [x] custom lightbar colors on supported controllers
- [x] Custom settings profiles
- [x] button remapping
- [ ] Emulate Dualsense so it can connect it by bluetooth and have functional adaptive triggers (for games that only support it on usb)

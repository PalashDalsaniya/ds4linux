# ds4linux

A native Linux clone of [DS4Windows](https://github.com/Ryochan7/DS4Windows) — bridges
Sony DualShock 4 / DualSense controllers to virtual Xbox 360 or DS4 devices,
with a full-featured Qt 6 GUI for profile management.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                    GUI (Qt 6)                        │
│  Profile editor · LED color picker · Battery view    │
└────────────────────────┬─────────────────────────────┘
                         │  Unix domain socket (IPC)
┌────────────────────────┴─────────────────────────────┐
│                Backend Daemon (C++)                   │
│  libevdev input · uinput output · hidraw LED/rumble  │
│  Runs as systemd service with elevated privileges    │
└──────────────────────────────────────────────────────┘
```

### Key Design Goals

| Concern          | Decision                                      |
| ---------------- | --------------------------------------------- |
| Language          | C++20 (daemon + common), C++20/Qt6 (GUI)     |
| Input layer       | `libevdev` + `hidraw`                         |
| Output layer      | Linux `uinput` kernel module                  |
| IPC               | Unix domain socket (`/run/ds4linux.sock`)     |
| Profiles          | JSON in `~/.config/ds4linux/profiles/`        |
| Build system      | CMake ≥ 3.22                                  |
| Wayland           | Qt 6 Wayland backend (native)                 |
| Service manager   | systemd                                       |

## Building

```bash
# Dependencies (Arch)
sudo pacman -S cmake ninja libevdev qt6-base qt6-wayland nlohmann-json

# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install (daemon + service + GUI)
sudo cmake --install build
```

## Running

```bash
# Enable & start daemon
sudo systemctl enable --now ds4linux

# Launch GUI (no root needed)
ds4linux-gui
```

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
├── gui/                        # Qt 6 frontend
│   ├── include/ds4linux/
│   ├── src/
│   └── resources/
├── config/                     # systemd unit, default profile
└── scripts/                    # Helper scripts
```

## License

GPL-3.0-or-later

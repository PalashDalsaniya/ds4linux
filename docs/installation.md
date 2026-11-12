# Installation

## Packages

Pre-built packages are on the [GitHub Releases](https://github.com/PalashDalsaniya/ds4linux/releases) page. They contain the daemon and `ds4linux-ctl`. The GUI is not packaged yet ([build it from source](#building-from-source)).

| Distribution | Command |
| ------------ | ------- |
| Debian / Ubuntu / Linux Mint | `sudo apt install ./ds4linux-*-Linux.deb` |
| Fedora | `sudo dnf install ./ds4linux-*-Linux.rpm` |
| Bazzite, Silverblue (rpm-ostree) | `sudo rpm-ostree install ./ds4linux-*-Linux.rpm`, then `systemctl reboot` |

The packages do not start or enable anything. Start ds4linux when you want it, see [Using ds4linux](usage.md).

## Arch Linux (install script)

```bash
git clone https://github.com/PalashDalsaniya/ds4linux.git
cd ds4linux
./scripts/install.sh
```

The script installs the build dependencies with `pacman`, builds and installs ds4linux to `/usr`, installs the udev rules, and loads the `uhid` and `uinput` kernel modules (now and at boot).

## Building from source

Dependencies: CMake ≥ 3.22, Ninja, a C++20 compiler, `libevdev`, `libudev` (systemd), `nlohmann-json`; Qt 6 (`qt6-base`) for the GUI.

```bash
# Arch
sudo pacman -S --needed cmake ninja gcc libevdev systemd nlohmann-json qt6-base
# Debian / Ubuntu
sudo apt install cmake ninja-build build-essential libevdev-dev libudev-dev nlohmann-json3-dev qt6-base-dev

git clone https://github.com/PalashDalsaniya/ds4linux.git
cd ds4linux
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDS4LINUX_BUILD_GUI=ON
cmake --build build
ctest --test-dir build            # optional

sudo cmake --install build --prefix /usr
sudo systemctl daemon-reload
sudo cp config/99-ds4linux.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo modprobe uhid uinput
printf 'uhid\nuinput\n' | sudo tee /etc/modules-load.d/ds4linux.conf > /dev/null
```

CMake options:

| Option | Default | |
| ------ | ------- | - |
| `DS4LINUX_BUILD_DAEMON` | `ON` | `ds4linux-daemon` |
| `DS4LINUX_BUILD_CLI` | `ON` | `ds4linux-ctl` |
| `DS4LINUX_BUILD_GUI` | `OFF` | `ds4linux-gui` (Qt 6) |
| `DS4LINUX_BUILD_TESTS` | `ON` | tests, run with `ctest` |
| `DS4LINUX_ENABLE_PACKAGING` | `OFF` | CPack `.deb` / `.rpm` (also installs the udev rules and modules-load file) |

## What gets installed

| Path | Purpose |
| ---- | ------- |
| `/usr/bin/ds4linux-daemon` | The daemon (runs as root) |
| `/usr/bin/ds4linux-ctl` | Command-line control |
| `/usr/bin/ds4linux-gui`, `/usr/share/applications/ds4linux.desktop` | GUI (if built) |
| `/usr/lib/systemd/system/ds4linux.service` | The service; not enabled by default |
| `/usr/lib/udev/rules.d/99-ds4linux.rules` (packages) or `/etc/udev/rules.d/` | Lets the logged-in user's games read the virtual DS4 |
| `/usr/lib/modules-load.d/ds4linux.conf` | Loads `uhid` and `uinput` at boot |
| `/usr/share/ds4linux/default_profile.json` | Reference copy of the Default profile |
| `/var/lib/ds4linux/` | Profiles and per-controller assignments (created by the service) |
| `/etc/default/ds4linux` | Optional: extra daemon options, `DS4LINUX_ARGS=...` |

## Uninstalling

Stop ds4linux (`ds4linux-ctl stop`), remove the package (or the files above), and run `sudo udevadm control --reload-rules`. Delete `/var/lib/ds4linux` to remove your profiles.

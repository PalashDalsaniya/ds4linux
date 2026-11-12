# Development

## Components

| Directory | Program | |
| --------- | ------- | - |
| `daemon/` | `ds4linux-daemon` | Root daemon: finds controllers, creates the virtual controllers, translates input and output, serves the control socket |
| `cli/` | `ds4linux-ctl` | Command-line client |
| `gui/` | `ds4linux-gui` | Qt 6 client |
| `common/` | `libds4linux-common` | Shared code: constants, profile type and JSON format, IPC framing |
| `tests/` | `mapping_test`, `x360_test` | Run with `ctest` (`x360_test` skips itself without access to `/dev/uinput`) |
| `config/` | | systemd unit, udev rules, Default profile |
| `scripts/` | | Arch install script, package post-install script |

## How the daemon works

```
DualSense / DS4 ──hid-playstation──► evdev nodes (gamepad, touchpad, motion) ─┐
      ▲                                                                          │ grabbed, read with libevdev
      │ hidraw output report                                                     ▼
      │ (rumble, lightbar)                                         DeviceManager (per controller)
      │                                                            profile: deadzones, remaps
      └──────────── rumble / LED from games ◄───────────────────────────────┤
                                                                             ▼
                                                    virtual DS4 (/dev/uhid) or Xbox 360 (/dev/uinput)
                                                                             ▼
                                                                        games, Steam
```

- **Discovery.** Supported controllers are found by scanning `/dev/input/event*` at startup and on every udev `add` event. The kernel driver splits each controller into a gamepad, a touchpad and a motion-sensor node; they are grouped by their parent HID device. ds4linux's own virtual devices are recognised and skipped. A Bluetooth controller handled by bluetoothd also sits under uhid, but with the Bluetooth bus type.
- **Hiding.** The evdev nodes are grabbed (`EVIOCGRAB`), but hidraw nodes can't be, so `NodeHider` hides the whole controller through udev. While a controller is in use, a runtime rule in `/run/udev/rules.d/72-ds4linux-hidden.rules` matches everything below its HID device, sets `MODE:="0600"` and removes the `uaccess` tag, before `73-seat-late` turns that tag into an ACL. The existing nodes are re-processed with a `change` uevent, and their ACL is removed. Without the tag, systemd-logind doesn't re-grant access on login or seat changes, and nodes created later are hidden as they appear. On release the rule is removed and the nodes are re-processed, which restores their normal mode and ACL. The service's `ExecStopPost` removes the rule after a crash, and a new daemon clears stale rules at startup.
- **Input.** Events are accumulated into a `DS4InputState`. The motion node reports on every controller frame, so it clocks the output reports (with a 20 ms fallback to the gamepad node's sync). Each frame goes through `apply_profile()` (`daemon/src/mapping.cpp`) and then to the virtual device.
- **Virtual DualShock 4.** A UHID device with a real DS4 v1 report descriptor, its own MAC address, and calibration / firmware / MAC feature reports. The kernel's DS4 driver binds to it, so games get the usual evdev nodes *and* a hidraw node. An I/O thread answers feature-report requests and collects output reports.
- **Virtual Xbox 360.** A uinput device with the xpad identity and `FF_RUMBLE` (upload / erase / play handled in `virtual_x360.cpp`).
- **Output.** Rumble and LED from games are written to the physical controller's hidraw node:
  - DS4: report 0x05 over USB, 0x11 + CRC32 over Bluetooth.
  - DualSense: report 0x02 over USB, 0x31 + CRC32 over Bluetooth, with vibration v2 on firmware ≥ 2.21.
  - Rumble is scaled by the profile and written only when it changes.
- **Main loop.** One `epoll` set (rebuilt when the set of device fds changes) holds the device nodes, the virtual devices' output fds, the udev monitor and the control socket.

## Control socket (IPC)

`/run/ds4linux.sock` (mode 0666; `--socket=PATH` to change). Each message is a 4-byte big-endian length followed by a JSON object with a `"type"`. An optional `"id"` in a request is echoed in its response. Requests that act on controllers take an optional `"device"`, either an evdev path or a 1-based index from `ListDevices`. Without it, they apply to every controller (`GetInputState`: the first).

| Request | Fields | Response |
| ------- | ------ | -------- |
| `Ping` | | `Pong` `{version}` |
| `ListDevices` | | `DeviceList` `{devices: [{path, name, model, connection, mac, profile, output_mode, battery: {percent, status}, touchpad, motion}]}` |
| `RescanDevices` | | `Ok` |
| `SetLightbarColor` | `r, g, b` (0–255), `device?` | `Ok` |
| `SetRumble` | `heavy, light` (0–255), `duration_ms?` (0–5000, default 500), `device?` | `Ok` |
| `ListProfiles` | | `ProfileList` `{profiles: [names]}` |
| `LoadProfile` | `name` | `Profile` `{profile}` |
| `SaveProfile` | `profile` (object, see [Profiles](profiles.md#files)) | `Ok` |
| `DeleteProfile` | `name` | `Ok` |
| `SetActiveProfile` | `name`, `device?` | `Ok` |
| `GetInputState` | `device?` | `InputState` `{path, input, output}`: sticks, triggers, D-pad, buttons, gyro, accel, touches; before and after the profile |

Errors come back as `{"type": "Error", "message": "..."}`.

## Building and testing

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDS4LINUX_BUILD_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a development daemon with its own socket and state directory:

```bash
sudo ./build/daemon/ds4linux-daemon --debug --socket=/tmp/ds4.sock --state-dir=/tmp/ds4-state
./build/cli/ds4linux-ctl --socket /tmp/ds4.sock list
./build/gui/ds4linux-gui --socket /tmp/ds4.sock
```

Only one daemon can run at a time (it holds `/run/ds4linux.lock`), so stop the service first (`ds4linux-ctl stop`).

Packages: configure with `-DDS4LINUX_ENABLE_PACKAGING=ON`, then `cpack --config build/CPackConfig.cmake`. The release workflow (`.github/workflows/release.yaml`) builds the `.deb` and `.rpm`.

# ds4linux

A native Linux clone of [DS4Windows](https://ds4-windows.com/download/). ds4linux takes over your DualShock 4 or DualSense and presents it to games as a clean virtual **DualShock 4** or **Xbox 360** controller, with per-controller profiles for deadzones, button remapping, rumble strength and lightbar colour. The real controller is hidden while ds4linux runs, so Steam and games see exactly one controller, and it is handed back untouched when you stop it.

## Features

- **Virtual DualShock 4** with buttons, sticks, triggers, two-finger touchpad, gyro and accelerometer, battery level, rumble and lightbar. Or a **virtual Xbox 360** controller with rumble, for XInput-only games. Switch at any time.
- **DualShock 4, DualSense and DualSense Edge**, over USB or Bluetooth. Several controllers at once, each with its own virtual controller.
- **Profiles**, remembered per controller: stick deadzones (circular / square), trigger deadzones, button remapping, rumble strength, lightbar colour.
- **Hotplug**: controllers can be connected and disconnected at any time.
- **On demand**: start it when you want emulation; stop it and your controllers are normal controllers again. Optionally start at boot.
- **`ds4linux-ctl`** command-line tool and a **Qt 6 GUI** with a live controller tester.
- **Generic gamepads** (Xbox, Logitech, 8BitDo, ...) can be presented as a DualShock 4 too (opt-in).

## Quick start

1. **Install** a package from [Releases](https://github.com/PalashDalsaniya/ds4linux/releases) (`.deb` / `.rpm`), or on Arch run `./scripts/install.sh`. See [Installation](docs/installation.md) for building from source and the GUI.
2. **Connect** your controller over USB or Bluetooth.
3. **Start ds4linux**, then Steam or your game. No `sudo` needed: it asks for your password once.

   ```bash
   ds4linux-ctl start
   ds4linux-ctl list        # your controller, battery and profile
   ```

   Or use the GUI's **Start ds4linux** button.
4. **Stop** it when you're done, and the raw controller works again: `ds4linux-ctl stop`.

To change settings:

```bash
ds4linux-ctl profile new Racing
ds4linux-ctl profile set Racing output x360       # emulate an Xbox 360 controller
ds4linux-ctl profile remap Racing cross circle
ds4linux-ctl -d 1 profile use Racing
```

## Documentation

| | |
| - | - |
| [Installation](docs/installation.md) | Packages, install script, building from source, what gets installed |
| [Using ds4linux](docs/usage.md) | Starting and stopping, start at boot, Steam, hotplug |
| [Profiles](docs/profiles.md) | Every setting and the profile file format |
| [Emulated controllers](docs/emulated-controllers.md) | DualShock 4 vs. Xbox 360 mode |
| [ds4linux-ctl](docs/cli.md) | Command-line reference |
| [GUI](docs/gui.md) | The `ds4linux-gui` app |
| [Generic controllers](docs/generic-controllers.md) | Non-Sony gamepads (`--generic`) |
| [Troubleshooting](docs/troubleshooting.md) | Duplicate controllers, rumble, `--debug` logging |
| [Development](docs/development.md) | Architecture, IPC protocol, building and testing |

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

# Using ds4linux

## Starting and stopping

ds4linux runs as a system service, `ds4linux.service`, that is **not** started automatically. While it runs, it takes over every supported controller and games see the virtual controller instead. When it stops, the controllers are handed back and work as normal (raw) controllers again.

From the GUI: **Start ds4linux** (shown when it isn't running), **Stop ds4linux** (in the status bar while it runs).

From a terminal (no `sudo` needed):

```bash
ds4linux-ctl start      # start emulation
ds4linux-ctl stop       # stop it; the raw controllers work again
ds4linux-ctl restart    # e.g. after changing /etc/default/ds4linux
```

Starting or stopping a system service is an administrator action, so polkit asks for your password. It is remembered for a few minutes.

### Start automatically

- **When the GUI opens:** tick *Start ds4linux when this app opens* on the GUI's start screen.
- **At boot:** `ds4linux-ctl autostart on` (or *Start at boot* in the GUI). `ds4linux-ctl autostart off` undoes it, and `ds4linux-ctl autostart` shows the current setting.

### Running it in a terminal

For debugging you can run the daemon directly instead of the service: `sudo ds4linux-daemon` (`Ctrl+C` stops it). Don't run both at once. See `ds4linux-daemon --help` for its options (`--debug`, `--generic`, `--socket`, `--state-dir`).

Options for the service go in `/etc/default/ds4linux`:

```bash
DS4LINUX_ARGS="--generic --debug"
```

## Steam and games

**Start ds4linux before Steam.** While ds4linux runs, the physical controller is hidden: its input nodes are grabbed and made root-only, and only the virtual controller is visible. A program that opened the physical controller *before* that keeps its access, so a Steam that was already running would show two controllers. Restart Steam in that case. The daemon log names any program that still has the physical controller open (`journalctl -u ds4linux`).

Steam's per-controller **Game rumble** setting must be on for games to rumble the controller through Steam Input.

## Connecting controllers

Controllers can be connected before or after ds4linux starts, over USB or Bluetooth. Each controller gets its own virtual controller and remembers its own profile. Reconnecting the same controller, by USB or Bluetooth, brings back its profile. Unplugging a controller removes its virtual controller.

Supported: DualShock 4 (v1 and v2), DualSense, DualSense Edge. Other gamepads: see [Generic controllers](generic-controllers.md).

If a controller isn't picked up, `ds4linux-ctl rescan` looks again.

## Status

```bash
ds4linux-ctl list
```

```
1. DualSense (usb)  /dev/input/event24
   battery: 80% charging   profile: Default   output: ds4   touchpad: yes   motion: yes
```

The number is what `-d` takes in other commands (`ds4linux-ctl -d 1 ...`).

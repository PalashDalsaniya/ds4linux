# Troubleshooting

First check what the daemon says:

```bash
ds4linux-ctl ping                  # is it running?
ds4linux-ctl list                  # which controllers does it have?
journalctl -u ds4linux -b          # its log (when started as the service)
```

## Steam shows two controllers

Steam opened the physical controller before ds4linux started, and keeps that access. Restart Steam, or start ds4linux before Steam. The daemon log names every program that still has the physical controller open (`... already has /dev/hidrawN open ...`).

## A controller isn't detected

- Check it's connected: over USB with a data cable; over Bluetooth, paired and connected in your Bluetooth settings.
- `ds4linux-ctl rescan`, then `ds4linux-ctl list`.
- Non-Sony gamepads need `--generic`, see [Generic controllers](generic-controllers.md).

## No rumble, or the wrong lightbar colour

- In Steam, open the controller's settings and turn on **Game rumble**.
- Check the profile's rumble strength isn't 0 (`ds4linux-ctl profile show NAME`).
- `ds4linux-ctl rumble 200 200 1000` tests the motors directly. If that works, the game isn't sending rumble.
- To see exactly what games send, run the daemon with `--debug`, either in a terminal (`ds4linux-ctl stop`, then `sudo ds4linux-daemon --debug`) or as `DS4LINUX_ARGS=--debug` in `/etc/default/ds4linux`. It logs:
  - `app output via ...`: every rumble / lightbar report a game sends to the virtual DualShock 4;
  - `x360 FF upload/play ...`: rumble effects sent to the virtual Xbox 360 controller;
  - `motors heavy=... light=...`: every change sent to the physical controller's motors.

  No `app output` / `x360 FF` lines means the game isn't sending rumble to ds4linux.

## Xbox 360 mode falls back to DualShock 4

The `uinput` kernel module isn't loaded: `sudo modprobe uinput`. The packages and the install script load it at boot via `/usr/lib/modules-load.d/ds4linux.conf` (or `/etc/modules-load.d/`).

## "Failed to open /dev/uhid"

The `uhid` module isn't loaded: `sudo modprobe uhid`.

## `ds4linux-ctl` / the GUI can't connect

ds4linux isn't running: `ds4linux-ctl start`, or the GUI's **Start ds4linux**. If you started the daemon by hand with `--socket=PATH`, pass the same `--socket` to `ds4linux-ctl` / `ds4linux-gui`.

## `ds4linux-ctl start` says the unit doesn't exist

systemd hasn't seen the installed service yet: `sudo systemctl daemon-reload`.

## The raw controller doesn't work after stopping ds4linux

ds4linux gives the controller back when it stops, also after a crash of the service. If a daemon started by hand was killed, reconnect the controller, start and stop ds4linux once (a new daemon removes what the killed one left behind), or reboot.

## A game doesn't work

See the game compatibility list in the [README](../README.md#game-compatibility). Try the other emulated controller (`output x360` / `ds4`). [SenseShock](https://github.com/muhammad23012009/senseshock) is an alternative for games that need a real DualSense.

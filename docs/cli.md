# ds4linux-ctl

`ds4linux-ctl` controls ds4linux from a terminal. It doesn't need `sudo`. `start`, `stop`, `restart` and `autostart` go through systemd and ask for your password. Every other command talks to the running daemon over its control socket (`/run/ds4linux.sock`).

```
ds4linux-ctl [--socket PATH] [-d DEVICE] <command> [args]
```

| Option | |
| ------ | - |
| `-d`, `--device DEVICE` | Which controller: its number from `list`, or its evdev path. Without it, controller commands apply to every controller (`test` uses the first). |
| `--socket PATH` | Talk to a daemon started with `--socket=PATH`. |
| `-h`, `--help` | Show the command summary. |

## Daemon

| Command | |
| ------- | - |
| `start` | Start ds4linux (the `ds4linux` service) and list the controllers it picked up. The physical controllers are hidden from other programs. |
| `stop` | Stop it. The raw controllers work normally again. |
| `restart` | Restart it, e.g. after editing `/etc/default/ds4linux`. |
| `autostart` | Show whether it starts at boot (`enabled` / `disabled`). |
| `autostart on` / `off` | Start it at boot, or don't. |
| `ping` | Check that the daemon is running, and print its version. |

## Controllers

| Command | |
| ------- | - |
| `list` (or `status`) | Connected controllers: model, USB/Bluetooth, battery, profile, emulated controller, touchpad and motion sensors. |
| `rescan` | Look for controllers again (normally not needed: new controllers are picked up automatically). |
| `lightbar '#RRGGBB'` or `lightbar R G B` | Set the lightbar colour until the next profile change. |
| `rumble HEAVY LIGHT [MS]` | Rumble the heavy (left) and light (right) motors, 0–255 each, for `MS` milliseconds (default 500, max 5000). |
| `test` | Live view of a controller's input: the raw input and the output after the profile (deadzones, remaps). `Ctrl+C` stops it. |

## Profiles

| Command | |
| ------- | - |
| `profile list` | All profiles. |
| `profile show NAME` | Print a profile as JSON. |
| `profile use NAME` | Use the profile on the selected controller(s). Remembered per controller. |
| `profile new NAME [--from BASE]` | Create a profile as a copy of Default (or of `BASE`). |
| `profile delete NAME` | Delete a profile. Controllers using it go back to Default. |
| `profile set NAME KEY VALUE` | Change a setting, see below. |
| `profile remap NAME FROM TO` | Make button `FROM` act as `TO`; `TO` = `none` disables it. |
| `profile unmap NAME FROM` | Remove a remap. |
| `profile import FILE` | Save a profile from a JSON file (its `name` field names it). |
| `profile export NAME [FILE]` | Write a profile as JSON to `FILE`, or to the terminal. |

`profile set` keys:

| KEY | VALUE | Example |
| --- | ----- | ------- |
| `output` | `ds4` or `x360` | `profile set Racing output x360` |
| `lightbar` | `#RRGGBB` | `profile set Racing lightbar '#ff4400'` |
| `rumble` | 0–100 (percent) | `profile set Racing rumble 60` |
| `left-stick`, `right-stick` | `INNER[,OUTER[,circular\|square\|none]]` in percent | `profile set Racing left-stick 10,90,circular` |
| `l2`, `r2` | `INNER[,OUTER]` in percent; `0,100` turns it off | `profile set Racing r2 5` |

Buttons: `cross circle square triangle l1 r1 l2 r2 share options l3 r3 ps touchpad dpad_up dpad_down dpad_left dpad_right`.

## Exit status

`0` on success, `1` on an error (daemon not running, daemon refused the request), `2` on a usage error. `start`, `stop`, `restart` and `autostart` return systemctl's status.

## Examples

```bash
ds4linux-ctl start
ds4linux-ctl list
ds4linux-ctl profile new Racing
ds4linux-ctl profile set Racing output x360
ds4linux-ctl profile remap Racing cross circle
ds4linux-ctl -d 1 profile use Racing
ds4linux-ctl -d 1 test
ds4linux-ctl stop
```

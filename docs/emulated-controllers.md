# Emulated controllers

Each profile picks what games see: a **DualShock 4** (default) or an **Xbox 360** controller. You can switch at any time from the GUI or with `ds4linux-ctl profile set NAME output ds4|x360`; Steam and games pick up the change immediately.

| | DualShock 4 | Xbox 360 |
| - | ----------- | -------- |
| Created through | `/dev/uhid` (kernel DS4 driver) | `/dev/uinput` |
| Seen as | Sony DualShock 4 (054C:05C4), USB | Microsoft X-Box 360 pad (045E:028E) |
| Buttons, sticks, triggers, D-pad | ✓ | ✓ (Cross = A, Circle = B, Square = X, Triangle = Y, Share = Back, Options = Start, PS = Guide) |
| Touchpad (two fingers, click) | ✓ | — (the click can be remapped to another button) |
| Gyro and accelerometer | ✓ | — |
| Battery level | ✓ | — |
| Rumble from games | ✓ | ✓ |
| Lightbar from games | ✓ | — (the profile's colour is used) |
| PlayStation button prompts | ✓ | Xbox prompts |

Use **DualShock 4** for Steam Input, games with PlayStation support, gyro aiming and touchpad use. Use **Xbox 360** for games that only support XInput controllers.

## Rumble and the lightbar

- Games' rumble is scaled by the profile's rumble strength and sent to the physical controller. On a DualSense, ds4linux picks the rumble mode that matches the controller's firmware.
- The profile's lightbar colour is set when the controller connects or the profile changes. In DualShock 4 mode, games (and Steam) can change it while they run.
- `ds4linux-ctl rumble HEAVY LIGHT [MS]` and `ds4linux-ctl lightbar '#RRGGBB'` test both directly.

## Several controllers

Each physical controller gets its own virtual controller, with its own MAC address, so games and Steam keep them apart and remember their settings.

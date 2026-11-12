# Profiles

A profile is a set of controller settings. Every controller uses one profile (the **Default** profile until you pick another). ds4linux remembers which profile each controller uses, keyed by the controller's MAC address, so it survives reconnects and restarts.

Edit profiles in the GUI's **Profiles** tab, or with `ds4linux-ctl profile ...` (see [CLI](cli.md#profiles)). Changes apply immediately to every controller using the profile.

## Settings

| Setting | Values | CLI |
| ------- | ------ | --- |
| Emulated controller | DualShock 4 (default) or Xbox 360. See [Emulated controllers](emulated-controllers.md). | `profile set NAME output ds4\|x360` |
| Lightbar colour | Any RGB colour. Applied when the profile is selected; games can change it while they run. | `profile set NAME lightbar '#RRGGBB'` |
| Rumble strength | 0–100 %. Scales every rumble games send; 0 turns rumble off. | `profile set NAME rumble 60` |
| Stick deadzones | Inner and outer deadzone in percent, and the type: **circular** (by distance from centre, the usual choice), **square** (per axis) or **none**. | `profile set NAME left-stick 10,90,circular` |
| Trigger deadzones | Inner and outer deadzone for L2 and R2, in percent. | `profile set NAME r2 5,95` |
| Button remapping | Any button can act as another button, or be disabled. | `profile remap NAME cross circle`, `profile remap NAME ps none` |

**Deadzones.** Input inside the inner deadzone reads as zero; input beyond the outer deadzone reads as full. Values in between are rescaled to the full range, so there is no jump at the edge of the deadzone. The Default profile has a 5 % circular deadzone on both sticks and none on the triggers.

**Remapping** applies to the button state games receive. Several buttons can be mapped onto the same target; any of them presses it. L2 and R2 keep their analog value when they stay L2/R2, and a button mapped onto L2 or R2 pulls the trigger fully. The D-pad directions and the touchpad click can be remapped like any other button.

Button names: `cross circle square triangle l1 r1 l2 r2 share options l3 r3 ps touchpad dpad_up dpad_down dpad_left dpad_right`. On a DualSense, `share` is the Create button.

## Managing profiles

```bash
ds4linux-ctl profile list
ds4linux-ctl profile new Racing                 # copy of Default
ds4linux-ctl profile new Racing2 --from Racing  # copy of another profile
ds4linux-ctl -d 1 profile use Racing            # controller 1 uses Racing from now on
ds4linux-ctl profile delete Racing              # its controllers go back to Default
ds4linux-ctl profile export Racing racing.json
ds4linux-ctl profile import racing.json
```

Names may contain letters, digits, spaces, `_`, `.` and `-` (up to 64 characters). The Default profile can be edited but not deleted. At most 256 profiles can exist.

## Files

Profiles are stored by the daemon in `/var/lib/ds4linux/profiles/<name>.json`, and the controller → profile assignments in `/var/lib/ds4linux/assignments.json`. Use `ds4linux-ctl` or the GUI rather than editing them by hand: the daemon validates every change and applies it immediately.

```json
{
    "name": "Default",
    "output_mode": 0,
    "lightbar_color": { "r": 0, "g": 0, "b": 255 },
    "rumble_strength": 1.0,
    "left_stick_dz":    { "type": 1, "inner": 0.05, "outer": 1.0 },
    "right_stick_dz":   { "type": 1, "inner": 0.05, "outer": 1.0 },
    "left_trigger_dz":  { "type": 0, "inner": 0.0,  "outer": 1.0 },
    "right_trigger_dz": { "type": 0, "inner": 0.0,  "outer": 1.0 },
    "button_map": { "cross": "circle", "ps": "none" }
}
```

| Field | Meaning |
| ----- | ------- |
| `output_mode` | `0` DualShock 4, `1` Xbox 360 |
| `rumble_strength` | `0.0`–`1.0` |
| `*_dz.type` | Sticks: `0` none, `1` circular, `2` square. Triggers: `0` off, anything else on |
| `*_dz.inner`, `*_dz.outer` | `0.0`–`1.0` |
| `button_map` | source button → target button, or `"none"` to disable |

Missing fields take their default values, and out-of-range numbers are clamped into range. A profile without a name, or with an invalid name, is rejected.

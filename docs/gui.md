# GUI

`ds4linux-gui` (DS4Linux in your application menu) runs as your normal user and talks to the daemon over its control socket. It is not in the release packages yet; build it with `-DDS4LINUX_BUILD_GUI=ON` (see [Installation](installation.md#building-from-source)).

## Starting and stopping

When ds4linux isn't running, the window shows a **Start ds4linux** button. Your password is asked once (polkit). Tick **Start ds4linux when this app opens** to have the app start it for you.

While it runs, the status bar has:

- **Stop ds4linux**: stops emulation and gives the raw controllers back.
- **Start at boot**: start ds4linux automatically when the computer starts.

If the daemon stops for any reason, the window goes back to the start screen and reconnects by itself once the daemon is back.

## Controllers

Every connected controller with its model, connection (USB / Bluetooth), battery, the emulated controller, and its profile. Pick a different profile from the drop-down to switch it right away; the choice is remembered for that controller.

## Profiles

Create, duplicate, delete and edit profiles: emulated controller (DualShock 4 / Xbox 360), lightbar colour, rumble strength, stick deadzones (inner, outer, circular / square / none), trigger deadzones and button remapping. **Save** applies the changes to every controller using the profile. **Try lightbar colour** and **Test rumble** try the settings on the controller before saving. The Default profile can be edited but not deleted. See [Profiles](profiles.md) for what each setting does.

## Controller test

A live test of the selected controller, like Steam's controller test:

- **Controller drawing** (DualShock 4 or DualSense): pressed buttons light up, the sticks move, L2/R2 fill as you pull them, touches show on the touchpad, and the lightbar shows the profile's colour. Choose whether it shows the **raw input** or **what games see** (after the profile's deadzones and remapping).
- **Left / right stick:** raw position (ring) and what games see (dot, with a trail), the profile's deadzones as dashed lines, and the values.
- **Triggers:** raw pull and output, with deadzone marks.
- **Touchpad:** both fingers with their paths and coordinates, and the click.
- **Motion:** a tilt indicator from the accelerometer, and live gyro (°/s) and accelerometer (g) graphs.

The header shows the connection, battery, profile and emulated controller.

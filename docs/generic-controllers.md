# Generic (non-Sony) controllers

ds4linux can also present other gamepads (Xbox, Logitech, 8BitDo, ...) as a virtual DualShock 4 (or Xbox 360) controller, so games show PlayStation button prompts and profiles work for them too. This is off by default, because ds4linux takes exclusive control of every controller it handles.

Enable it in `/etc/default/ds4linux` and restart ds4linux:

```bash
DS4LINUX_ARGS="--generic"                     # every non-Sony gamepad
DS4LINUX_ARGS="--generic=046d:c21f"           # only these VID:PID pairs (hex, see `lsusb`)
DS4LINUX_ARGS="--generic=046d:c21f,2dc8:6101" # several
```

```bash
ds4linux-ctl restart
```

(Or in a terminal: `sudo ds4linux-daemon --generic`.)

Sticks, buttons, triggers, the D-pad and rumble are forwarded, and profiles (deadzones, remapping, rumble strength, output mode) apply as for Sony controllers. Controllers without a touchpad or motion sensors report them as idle, and there is no lightbar.

Limitations:

- Only gamepads the kernel exposes with standard gamepad buttons are supported. Pads with a mode switch (e.g. Logitech F710) must be in XInput mode; DirectInput / joystick-class pads are not detected.
- Virtual gamepads created by other software (Steam Input, other remappers) are never picked up.
- The controller's `/dev/input/js*` node stays visible but receives no input while ds4linux runs.

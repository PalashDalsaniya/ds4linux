#pragma once
// ds4linux::daemon — Profile mapping: deadzones and button remaps applied to
// the normalised controller state before it reaches the virtual output.

#include "ds4linux/virtual_device.h"

#include <ds4linux/profile.h>

#include <array>
#include <cstdint>

namespace ds4linux::daemon {

/// Pressed state of every DS4 button, indexed by DS4Button (D-pad included).
using ButtonSet = std::array<bool, kDS4ButtonCount>;
[[nodiscard]] ButtonSet pressed_buttons(const DS4InputState& s);

/// Apply a profile's deadzones and button remaps to a raw controller state.
[[nodiscard]] DS4InputState apply_profile(const DS4InputState& raw, const Profile& p);

/// Scale a rumble motor value (0–255) by the profile's rumble strength.
[[nodiscard]] std::uint8_t scale_rumble(int value, double strength);

} // namespace ds4linux::daemon

#pragma once
// ds4linux — Profile data model
//
// Profiles are serialised to / from JSON.  The daemon owns them and stores
// them under /var/lib/ds4linux/profiles/<name>.json; clients edit them over IPC.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ds4linux {

// ── Lightbar color ───────────────────────────────────────────────────────────
struct LightbarColor {
    std::uint8_t r = 0x00;
    std::uint8_t g = 0x00;
    std::uint8_t b = 0xFF;
};

// ── Virtual output mode ──────────────────────────────────────────────────────
enum class OutputMode : std::uint8_t {
    DS4  = 0,   // virtual DualShock 4 (UHID: evdev + hidraw)
    X360 = 1,   // virtual Xbox 360 controller (uinput, xpad-compatible)
};

// ── Deadzone type ────────────────────────────────────────────────────────────
enum class DeadzoneType : std::uint8_t {
    None     = 0,
    Circular = 1,   // radial: applied to the stick's distance from centre
    Square   = 2,   // per axis
};

// ── Stick / Trigger deadzone parameters ──────────────────────────────────────
// Input below `inner` reads as 0, input above `outer` as full, and the range
// in between is rescaled linearly (normalised 0..1, inner < outer).
struct DeadzoneParams {
    DeadzoneType type  = DeadzoneType::Circular;
    double inner       = 0.05;
    double outer       = 1.00;
};

// ── DS4 buttons (remapping sources and targets) ──────────────────────────────
enum class DS4Button : std::uint8_t {
    Cross, Circle, Square, Triangle,
    L1, R1, L2, R2,
    Share, Options, L3, R3, PS, Touchpad,
    DpadUp, DpadDown, DpadLeft, DpadRight,
    None,   // as a target: disable the source button
};
inline constexpr std::size_t kDS4ButtonCount = static_cast<std::size_t>(DS4Button::None);

/// Lower-case name used in JSON and on the command line ("cross", "dpad_up").
[[nodiscard]] std::string_view to_string(DS4Button b) noexcept;
[[nodiscard]] std::optional<DS4Button> ds4_button_from_string(std::string_view s) noexcept;

// ── Button mapping: physical DS4 button → virtual DS4 button ─────────────────
//    Buttons not in the map pass through unchanged.
using ButtonMap = std::map<DS4Button, DS4Button>;

// ── Profile ──────────────────────────────────────────────────────────────────
struct Profile {
    std::string   name             = "Default";
    OutputMode    output_mode      = OutputMode::DS4;

    // Lightbar
    LightbarColor lightbar_color   = {};

    // Rumble scaling (0.0 = muted … 1.0 = full)
    double        rumble_strength  = 1.0;

    // Deadzones
    DeadzoneParams left_stick_dz   = {};
    DeadzoneParams right_stick_dz  = {};
    DeadzoneParams left_trigger_dz = { DeadzoneType::None, 0.0, 1.0 };
    DeadzoneParams right_trigger_dz= { DeadzoneType::None, 0.0, 1.0 };

    // Button remaps (empty = identity mapping)
    ButtonMap     button_map       = {};
};

// ── Serialisation ────────────────────────────────────────────────────────────

/// Serialise a Profile to a JSON string (pretty-printed).
[[nodiscard]] std::string profile_to_json(const Profile& p);

/// Deserialise a JSON string into a Profile.  Out-of-range values are clamped
/// and unknown keys ignored.  Throws std::runtime_error on malformed JSON.
[[nodiscard]] Profile profile_from_json(const std::string& json);

/// Profile names become file names: 1–64 characters from [A-Za-z0-9 _.-],
/// not starting with '.' or ' '.
[[nodiscard]] bool is_valid_profile_name(std::string_view name) noexcept;

} // namespace ds4linux

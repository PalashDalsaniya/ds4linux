#pragma once
// ds4linux — Profile data model
//
// Profiles are serialised to / from JSON and stored under
// ~/.config/ds4linux/profiles/<name>.json

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ds4linux {

// ── Lightbar color ───────────────────────────────────────────────────────────
struct LightbarColor {
    std::uint8_t r = 0x00;
    std::uint8_t g = 0x00;
    std::uint8_t b = 0xFF;
};

// ── Virtual output mode (DS4 only — Xbox emulation removed) ──────────────────
enum class OutputMode : std::uint8_t {
    DS4 = 0,
};

// ── Deadzone type ────────────────────────────────────────────────────────────
enum class DeadzoneType : std::uint8_t {
    None     = 0,
    Circular = 1,
    Square   = 2,
};

// ── Stick / Trigger deadzone parameters ──────────────────────────────────────
struct DeadzoneParams {
    DeadzoneType type  = DeadzoneType::Circular;
    double inner       = 0.05;   // normalised 0..1
    double outer       = 1.00;
};

// ── Button mapping: physical button → virtual button code ────────────────────
//    Key = Linux input event code of the physical button
//    Value = Linux input event code of the virtual button
using ButtonMap = std::unordered_map<std::uint16_t, std::uint16_t>;

// ── Axis mapping: physical axis → virtual axis code ──────────────────────────
using AxisMap = std::unordered_map<std::uint16_t, std::uint16_t>;

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

    // Touchpad as mouse
    bool          touchpad_as_mouse = false;
    double        touchpad_sensitivity = 1.0;

    // Button & axis remaps (empty = identity mapping)
    ButtonMap     button_map       = {};
    AxisMap       axis_map         = {};
};

// ── Serialisation ────────────────────────────────────────────────────────────

/// Serialise a Profile to a JSON string (pretty-printed).
[[nodiscard]] std::string profile_to_json(const Profile& p);

/// Deserialise a JSON string into a Profile.  Throws std::runtime_error on failure.
[[nodiscard]] Profile profile_from_json(const std::string& json);

/// Return the default config directory (~/.config/ds4linux).
[[nodiscard]] std::string config_dir();

/// Return the profiles directory (~/.config/ds4linux/profiles).
[[nodiscard]] std::string profiles_dir();

} // namespace ds4linux

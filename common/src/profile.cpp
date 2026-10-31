// ds4linux — Profile serialisation (JSON via nlohmann/json)

#include "ds4linux/profile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using json = nlohmann::json;

namespace ds4linux {

// ── Button names ─────────────────────────────────────────────────────────────

static constexpr std::array<std::string_view, kDS4ButtonCount + 1> kButtonNames = {
    "cross", "circle", "square", "triangle",
    "l1", "r1", "l2", "r2",
    "share", "options", "l3", "r3", "ps", "touchpad",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "none",
};

std::string_view to_string(DS4Button b) noexcept {
    auto i = static_cast<std::size_t>(b);
    return i < kButtonNames.size() ? kButtonNames[i] : "none";
}

std::optional<DS4Button> ds4_button_from_string(std::string_view s) noexcept {
    for (std::size_t i = 0; i < kButtonNames.size(); ++i) {
        if (kButtonNames[i] == s) return static_cast<DS4Button>(i);
    }
    return std::nullopt;
}

bool is_valid_profile_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64) return false;
    if (name.front() == '.' || name.front() == ' ') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '.' || c == '-';
    });
}

// ── JSON helpers ─────────────────────────────────────────────────────────────

/// Read a number from `j[key]`, falling back to `def` for missing, non-numeric
/// or non-finite values, and clamping into [lo, hi].
static double get_number(const json& j, const char* key, double def, double lo, double hi) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return def;
    double v = j[key].get<double>();
    if (!std::isfinite(v)) return def;
    return std::clamp(v, lo, hi);
}

static json lightbar_to_json(const LightbarColor& c) {
    return { {"r", c.r}, {"g", c.g}, {"b", c.b} };
}

static LightbarColor lightbar_from_json(const json& j) {
    auto channel = [&](const char* k, double def) {
        return static_cast<std::uint8_t>(std::lround(get_number(j, k, def, 0, 255)));
    };
    return { channel("r", 0), channel("g", 0), channel("b", 255) };
}

static json deadzone_to_json(const DeadzoneParams& dz) {
    return {
        {"type",  static_cast<int>(dz.type)},
        {"inner", dz.inner},
        {"outer", dz.outer},
    };
}

static DeadzoneParams deadzone_from_json(const json& j, const DeadzoneParams& def) {
    DeadzoneParams dz;
    const double type = get_number(j, "type", static_cast<double>(def.type), 0, 2);
    dz.type  = static_cast<DeadzoneType>(static_cast<int>(type));
    dz.inner = get_number(j, "inner", def.inner, 0.0, 0.95);
    dz.outer = get_number(j, "outer", def.outer, 0.05, 1.0);
    if (dz.outer <= dz.inner) dz.outer = std::min(1.0, dz.inner + 0.05);
    return dz;
}

static json button_map_to_json(const ButtonMap& m) {
    json j = json::object();
    for (const auto& [from, to] : m) {
        j[std::string(to_string(from))] = std::string(to_string(to));
    }
    return j;
}

static ButtonMap button_map_from_json(const json& j) {
    // Unknown names (including numeric keys from older profile versions)
    // are ignored rather than rejected.
    ButtonMap m;
    if (!j.is_object()) return m;
    for (const auto& [key, val] : j.items()) {
        if (!val.is_string()) continue;
        auto from = ds4_button_from_string(key);
        auto to   = ds4_button_from_string(val.get<std::string>());
        if (from && to && *from != DS4Button::None && *from != *to) m[*from] = *to;
    }
    return m;
}

// ── Public API ───────────────────────────────────────────────────────────────

std::string profile_to_json(const Profile& p) {
    json j;
    j["name"]               = p.name;
    j["output_mode"]        = static_cast<int>(p.output_mode);
    j["lightbar_color"]     = lightbar_to_json(p.lightbar_color);
    j["rumble_strength"]    = p.rumble_strength;
    j["left_stick_dz"]      = deadzone_to_json(p.left_stick_dz);
    j["right_stick_dz"]     = deadzone_to_json(p.right_stick_dz);
    j["left_trigger_dz"]    = deadzone_to_json(p.left_trigger_dz);
    j["right_trigger_dz"]   = deadzone_to_json(p.right_trigger_dz);
    j["button_map"]         = button_map_to_json(p.button_map);
    return j.dump(4);
}

Profile profile_from_json(const std::string& raw) {
    json j;
    try {
        j = json::parse(raw);
    } catch (const json::exception& ex) {
        throw std::runtime_error(std::string("Invalid profile JSON: ") + ex.what());
    }
    if (!j.is_object()) throw std::runtime_error("Invalid profile JSON: not an object");

    const Profile def;
    Profile p;
    if (j.contains("name") && j["name"].is_string()) p.name = j["name"].get<std::string>();
    p.output_mode = static_cast<OutputMode>(
        static_cast<int>(get_number(j, "output_mode", 0, 0, 1)));
    if (j.contains("lightbar_color"))
        p.lightbar_color  = lightbar_from_json(j["lightbar_color"]);
    p.rumble_strength     = get_number(j, "rumble_strength", 1.0, 0.0, 1.0);
    if (j.contains("left_stick_dz"))
        p.left_stick_dz   = deadzone_from_json(j["left_stick_dz"], def.left_stick_dz);
    if (j.contains("right_stick_dz"))
        p.right_stick_dz  = deadzone_from_json(j["right_stick_dz"], def.right_stick_dz);
    if (j.contains("left_trigger_dz"))
        p.left_trigger_dz = deadzone_from_json(j["left_trigger_dz"], def.left_trigger_dz);
    if (j.contains("right_trigger_dz"))
        p.right_trigger_dz= deadzone_from_json(j["right_trigger_dz"], def.right_trigger_dz);
    if (j.contains("button_map"))
        p.button_map      = button_map_from_json(j["button_map"]);
    return p;
}

} // namespace ds4linux

// ds4linux — Profile serialisation (JSON via nlohmann/json)

#include "ds4linux/profile.h"
#include "ds4linux/constants.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ds4linux {

// ── JSON helpers ─────────────────────────────────────────────────────────────

static json lightbar_to_json(const LightbarColor& c) {
    return { {"r", c.r}, {"g", c.g}, {"b", c.b} };
}

static LightbarColor lightbar_from_json(const json& j) {
    return {
        j.value("r", std::uint8_t{0}),
        j.value("g", std::uint8_t{0}),
        j.value("b", std::uint8_t{0xFF}),
    };
}

static json deadzone_to_json(const DeadzoneParams& dz) {
    return {
        {"type",  static_cast<int>(dz.type)},
        {"inner", dz.inner},
        {"outer", dz.outer},
    };
}

static DeadzoneParams deadzone_from_json(const json& j) {
    DeadzoneParams dz;
    dz.type  = static_cast<DeadzoneType>(j.value("type", 1));
    dz.inner = j.value("inner", 0.05);
    dz.outer = j.value("outer", 1.0);
    return dz;
}

static json button_map_to_json(const ButtonMap& m) {
    json j = json::object();
    for (auto& [k, v] : m) {
        j[std::to_string(k)] = v;
    }
    return j;
}

static ButtonMap button_map_from_json(const json& j) {
    ButtonMap m;
    for (auto& [key, val] : j.items()) {
        m[static_cast<std::uint16_t>(std::stoul(key))] = val.get<std::uint16_t>();
    }
    return m;
}

static json axis_map_to_json(const AxisMap& m) {
    json j = json::object();
    for (auto& [k, v] : m) {
        j[std::to_string(k)] = v;
    }
    return j;
}

static AxisMap axis_map_from_json(const json& j) {
    AxisMap m;
    for (auto& [key, val] : j.items()) {
        m[static_cast<std::uint16_t>(std::stoul(key))] = val.get<std::uint16_t>();
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
    j["touchpad_as_mouse"]  = p.touchpad_as_mouse;
    j["touchpad_sensitivity"] = p.touchpad_sensitivity;
    j["button_map"]         = button_map_to_json(p.button_map);
    j["axis_map"]           = axis_map_to_json(p.axis_map);
    return j.dump(4);
}

Profile profile_from_json(const std::string& raw) {
    auto j = json::parse(raw);
    Profile p;
    p.name                = j.value("name", std::string{"Default"});
    p.output_mode         = static_cast<OutputMode>(j.value("output_mode", 0));
    if (j.contains("lightbar_color"))
        p.lightbar_color  = lightbar_from_json(j["lightbar_color"]);
    p.rumble_strength     = j.value("rumble_strength", 1.0);
    if (j.contains("left_stick_dz"))
        p.left_stick_dz   = deadzone_from_json(j["left_stick_dz"]);
    if (j.contains("right_stick_dz"))
        p.right_stick_dz  = deadzone_from_json(j["right_stick_dz"]);
    if (j.contains("left_trigger_dz"))
        p.left_trigger_dz = deadzone_from_json(j["left_trigger_dz"]);
    if (j.contains("right_trigger_dz"))
        p.right_trigger_dz= deadzone_from_json(j["right_trigger_dz"]);
    p.touchpad_as_mouse   = j.value("touchpad_as_mouse", false);
    p.touchpad_sensitivity= j.value("touchpad_sensitivity", 1.0);
    if (j.contains("button_map"))
        p.button_map      = button_map_from_json(j["button_map"]);
    if (j.contains("axis_map"))
        p.axis_map        = axis_map_from_json(j["axis_map"]);
    return p;
}

std::string config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = xdg ? fs::path(xdg) : fs::path(std::getenv("HOME")) / ".config";
    return (base / kConfigDirName).string();
}

std::string profiles_dir() {
    return (fs::path(config_dir()) / kProfilesDirName).string();
}

} // namespace ds4linux

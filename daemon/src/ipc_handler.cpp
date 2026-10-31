// ds4linux::daemon — IPC request handler implementation
//
// Requests are JSON objects {"type": "<Request>", ...}.  An optional "id" is
// echoed back in the response.  Requests that target controllers take an
// optional "device": an evdev path or a 1-based index from ListDevices;
// without it they apply to every controller (GetInputState: the first one).

#include "ds4linux/ipc_handler.h"
#include "ds4linux/constants.h"
#include "ds4linux/device_manager.h"
#include "ds4linux/profile_manager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>

using json = nlohmann::json;

namespace ds4linux::daemon {

namespace {

struct RequestError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const char* output_mode_name(OutputMode m) {
    return m == OutputMode::X360 ? "x360" : "ds4";
}

const char* battery_status_name(BatteryStatus s) {
    switch (s) {
    case BatteryStatus::Charging:    return "charging";
    case BatteryStatus::Discharging: return "discharging";
    case BatteryStatus::NotCharging: return "not charging";
    case BatteryStatus::Full:        return "full";
    case BatteryStatus::Unknown:     break;
    }
    return "unknown";
}

int int_field(const json& j, const char* key, int lo, int hi, std::optional<int> def = {}) {
    if (!j.contains(key)) {
        if (def) return *def;
        throw RequestError(std::string("Missing field: ") + key);
    }
    if (!j[key].is_number_integer()) throw RequestError(std::string("Not an integer: ") + key);
    const auto v = j[key].get<long long>();
    if (v < lo || v > hi) {
        throw RequestError(std::string(key) + " must be between " + std::to_string(lo) +
                           " and " + std::to_string(hi));
    }
    return static_cast<int>(v);
}

std::string string_field(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) {
        throw RequestError(std::string("Missing string field: ") + key);
    }
    return j[key].get<std::string>();
}

json state_to_json(const DS4InputState& s) {
    json touches = json::array();
    for (const auto& t : s.touches) {
        touches.push_back({{"active", t.active}, {"id", t.id}, {"x", t.x}, {"y", t.y}});
    }
    return {
        {"lx", s.lx}, {"ly", s.ly}, {"rx", s.rx}, {"ry", s.ry},
        {"l2", s.l2}, {"r2", s.r2},
        {"dpad", {{"x", s.hat_x}, {"y", s.hat_y}}},
        {"buttons", {
            {"cross", s.cross}, {"circle", s.circle}, {"square", s.square},
            {"triangle", s.triangle}, {"l1", s.l1}, {"r1", s.r1},
            {"l2", s.l2_btn}, {"r2", s.r2_btn}, {"share", s.share},
            {"options", s.options}, {"l3", s.l3}, {"r3", s.r3},
            {"ps", s.ps}, {"touchpad", s.touchpad},
        }},
        {"gyro",  {s.gyro_x, s.gyro_y, s.gyro_z}},
        {"accel", {s.accel_x, s.accel_y, s.accel_z}},
        {"touches", touches},
    };
}

} // namespace

IpcHandler::IpcHandler(DeviceManager& devices, ProfileManager& profiles)
    : devices_(devices), profiles_(profiles) {}

std::string IpcHandler::handle(const std::string& request) {
    json req;
    json resp;
    try {
        req = json::parse(request);
        if (!req.is_object()) throw RequestError("Request must be a JSON object");
        const std::string type = string_field(req, "type");

        // Resolve the "device" selector to evdev paths
        auto select = [&](bool first_only) {
            const auto infos = devices_.device_info();
            std::vector<std::string> paths;
            if (!req.contains("device")) {
                for (const auto& d : infos) paths.push_back(d.path);
                if (first_only && !paths.empty()) paths.resize(1);
            } else if (req["device"].is_number_integer()) {
                const int idx = int_field(req, "device", 1, 64);
                if (idx > static_cast<int>(infos.size())) {
                    throw RequestError("No controller #" + std::to_string(idx));
                }
                paths.push_back(infos[static_cast<std::size_t>(idx - 1)].path);
            } else {
                const auto path = string_field(req, "device");
                if (std::none_of(infos.begin(), infos.end(),
                                 [&](const auto& d) { return d.path == path; })) {
                    throw RequestError("No controller at " + path);
                }
                paths.push_back(path);
            }
            if (paths.empty()) throw RequestError("No controllers connected");
            return paths;
        };

        if (type == "Ping") {
            resp = {{"type", "Pong"}, {"version", std::string(kVersion)}};

        } else if (type == "ListDevices") {
            json devs = json::array();
            for (const auto& d : devices_.device_info()) {
                devs.push_back({
                    {"path", d.path}, {"name", d.name}, {"model", d.model},
                    {"connection", d.connection}, {"mac", d.mac},
                    {"profile", d.profile}, {"output_mode", output_mode_name(d.output_mode)},
                    {"battery", {{"percent", d.battery.percent},
                                 {"status", battery_status_name(d.battery.status)}}},
                    {"touchpad", d.has_touchpad}, {"motion", d.has_motion},
                });
            }
            resp = {{"type", "DeviceList"}, {"devices", devs}};

        } else if (type == "RescanDevices") {
            devices_.scan();
            resp = {{"type", "Ok"}};

        } else if (type == "SetLightbarColor") {
            const auto r = static_cast<std::uint8_t>(int_field(req, "r", 0, 255));
            const auto g = static_cast<std::uint8_t>(int_field(req, "g", 0, 255));
            const auto b = static_cast<std::uint8_t>(int_field(req, "b", 0, 255));
            for (const auto& path : select(false)) devices_.set_lightbar(path, r, g, b);
            resp = {{"type", "Ok"}};

        } else if (type == "SetRumble") {
            const auto heavy = static_cast<std::uint8_t>(int_field(req, "heavy", 0, 255));
            const auto light = static_cast<std::uint8_t>(int_field(req, "light", 0, 255));
            const int  ms    = int_field(req, "duration_ms", 0, 5000, 500);
            for (const auto& path : select(false)) {
                devices_.test_rumble(path, heavy, light, std::chrono::milliseconds(ms));
            }
            resp = {{"type", "Ok"}};

        } else if (type == "ListProfiles") {
            resp = {{"type", "ProfileList"}, {"profiles", profiles_.list()}};

        } else if (type == "LoadProfile") {
            const auto p = profiles_.load(string_field(req, "name"));
            resp = {{"type", "Profile"}, {"profile", json::parse(profile_to_json(p))}};

        } else if (type == "SaveProfile") {
            if (!req.contains("profile") || !req["profile"].is_object()) {
                throw RequestError("Missing object field: profile");
            }
            const Profile p = profile_from_json(req["profile"].dump());
            if (!is_valid_profile_name(p.name)) {
                throw RequestError("Invalid profile name (use letters, digits, space, _ . -)");
            }
            profiles_.save(p);
            devices_.update_profile(p);
            resp = {{"type", "Ok"}};

        } else if (type == "DeleteProfile") {
            const auto name = string_field(req, "name");
            profiles_.remove(name);
            const Profile def = profiles_.load(std::string(kDefaultProfile));
            for (const auto& d : devices_.device_info()) {
                if (d.profile == name) devices_.set_profile(d.path, def);
            }
            resp = {{"type", "Ok"}};

        } else if (type == "SetActiveProfile") {
            const auto p = profiles_.load(string_field(req, "name"));
            const auto infos = devices_.device_info();
            for (const auto& path : select(false)) {
                for (const auto& d : infos) {
                    if (d.path == path) profiles_.assign(d.controller_key, p.name);
                }
                devices_.set_profile(path, p);
            }
            resp = {{"type", "Ok"}};

        } else if (type == "GetInputState") {
            const auto path  = select(true).front();
            const auto state = devices_.input_state(path);
            if (!state) throw RequestError("No controller at " + path);
            resp = {{"type", "InputState"}, {"path", path},
                    {"input", state_to_json(state->first)},
                    {"output", state_to_json(state->second)}};

        } else {
            throw RequestError("Unknown request type: " + type);
        }
    } catch (const std::exception& ex) {
        resp = {{"type", "Error"}, {"message", ex.what()}};
    }

    if (req.is_object() && req.contains("id")) resp["id"] = req["id"];
    return resp.dump();
}

} // namespace ds4linux::daemon

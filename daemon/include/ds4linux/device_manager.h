#pragma once
// ds4linux::daemon — Device manager: orchestrates physical → virtual mapping

#include "ds4linux/input_device.h"
#include "ds4linux/virtual_device.h"

#include <ds4linux/profile.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ds4linux::daemon {

/// Daemon-wide device selection options (from the command line).
struct DeviceManagerOptions {
    /// Also emulate a DS4 for non-Sony evdev gamepads (opt-in).
    bool generic_enabled = false;
    /// If non-empty, only these VID:PID pairs are treated as generic pads.
    std::vector<std::pair<std::uint16_t, std::uint16_t>> generic_ids;
};

/// How a physical gamepad node's axes map onto the DS4 report.  Built once
/// per controller from its advertised capabilities and axis ranges.
struct GamepadMap {
    std::uint16_t rx_code = 0, ry_code = 0;   // right stick axes
    int  l2_code = -1, r2_code = -1;          // analog trigger axes, -1 = none
    AbsRange lx, ly, rx, ry, l2, r2;
    bool synth_l2_btn = false, synth_r2_btn = false;   // digital from analog
    bool synth_l2_axis = false, synth_r2_axis = false; // analog from digital
};

/// Holds the state for a single controller slot.
struct ControllerSlot {
    // Physical input devices (one per event node)
    std::unique_ptr<InputDevice>   physical;          // gamepad (sticks/buttons)
    std::unique_ptr<InputDevice>   physical_touchpad; // touchpad
    std::unique_ptr<InputDevice>   physical_motion;   // motion sensors

    // Virtual output device (UHID DS4 — carries gamepad, touchpad and IMU data)
    std::unique_ptr<VirtualDevice> virtual_dev;
    std::array<std::uint8_t, 6>    virtual_mac{};

    // Input normalisation for this controller
    GamepadMap gamepad_map;
    AbsRange   touch_x, touch_y;      // physical touchpad ranges
    int        gyro_res  = 0;         // units per deg/s (0 = driver default)
    int        accel_res = 0;         // units per g     (0 = driver default)
    struct { bool up = false, down = false, left = false, right = false; } dpad_btn;

    // Accumulated DS4 state (packed into one HID report per physical frame)
    DS4InputState ds4_state;
    std::chrono::steady_clock::time_point next_battery_poll{};

    Profile     profile;
    std::string evdev_path;          // gamepad event node path
    std::string physical_id;         // sysfs path of the parent HID/USB device
    std::string touchpad_path;       // touchpad event node path (if found)
    std::string motion_path;         // motion event node path (if found)
};

/// Manages discovery, hotplug, and the translate loop for all controllers.
class DeviceManager {
public:
    explicit DeviceManager(DeviceManagerOptions options = {});
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    /// Scan /dev/input/ for supported controllers, open new ones and attach
    /// touchpad / motion nodes that appeared after their gamepad node.
    void scan();

    /// Handle a udev hotplug add event for an evdev path.
    void on_device_added(const std::string& evdev_path);

    /// Handle a udev hotplug remove event.
    void on_device_removed(const std::string& evdev_path);

    /// Run one iteration of the translate loop for all slots.
    /// Best called from an epoll wait.
    void translate_events();

    /// Set profile on a slot (by evdev path).
    void set_profile(const std::string& evdev_path, const Profile& p);

    /// Access all active slots (for status queries).
    [[nodiscard]] const auto& slots() const noexcept { return slots_; }

private:
    bool open_slot(const std::string& evdev_path, const std::string& physical_id);
    void close_slot(const std::string& evdev_path);
    [[nodiscard]] bool generic_allowed(std::uint16_t vid, std::uint16_t pid) const;
    [[nodiscard]] std::array<std::uint8_t, 6>
    allocate_virtual_mac(const std::string& seed, const std::string& physical_uniq) const;

    DeviceManagerOptions options_;
    std::unordered_map<std::string, ControllerSlot> slots_;
};

} // namespace ds4linux::daemon

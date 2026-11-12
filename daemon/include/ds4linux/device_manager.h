#pragma once
// ds4linux::daemon — Device manager: orchestrates physical → virtual mapping

#include "ds4linux/input_device.h"
#include "ds4linux/node_hider.h"
#include "ds4linux/virtual_device.h"

#include <ds4linux/profile.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
    AbsRange lx, ly, rx, ry, l2, r2, hat_x, hat_y;
    bool synth_l2_btn = false, synth_r2_btn = false;   // digital from analog
    bool synth_l2_axis = false, synth_r2_axis = false; // analog from digital
};

/// Holds the state for a single controller slot.
struct ControllerSlot {
    // Hides the physical nodes from other programs while the slot exists.
    // Declared first so it is destroyed last: access is given back only
    // after the nodes below are closed (and ungrabbed).
    NodeHider hidden;

    // Physical input devices (one per event node)
    std::unique_ptr<InputDevice>   physical;          // gamepad (sticks/buttons)
    std::unique_ptr<InputDevice>   physical_touchpad; // touchpad
    std::unique_ptr<InputDevice>   physical_motion;   // motion sensors

    // Virtual output device (UHID DS4 carrying gamepad, touchpad and IMU
    // data, or a uinput Xbox 360 pad), selected by the profile's output mode
    std::unique_ptr<VirtualOutput> virtual_dev;
    OutputMode                     output_mode = OutputMode::DS4;
    std::array<std::uint8_t, 6>    virtual_mac{};

    // Input normalisation for this controller
    GamepadMap gamepad_map;
    AbsRange   touch_x, touch_y;      // physical touchpad ranges
    int        gyro_res  = 0;         // units per deg/s (0 = driver default)
    int        accel_res = 0;         // units per g     (0 = driver default)
    struct { bool up = false, down = false, left = false, right = false; } dpad_btn;

    // Accumulated DS4 state from the physical controller (before the profile
    // is applied), and the last state sent to the virtual output (after)
    DS4InputState ds4_state;
    DS4InputState output_state;

    // Rumble: the applications' level, a test override that wins until
    // test_rumble_until, and the level last sent to the motors (-1 = none)
    int game_heavy = 0, game_light = 0;
    int test_heavy = 0, test_light = 0;
    std::chrono::steady_clock::time_point test_rumble_until{};
    int sent_heavy = -1, sent_light = -1;

    std::chrono::steady_clock::time_point next_battery_poll{};
    std::chrono::steady_clock::time_point last_motion_sync{};  // epoch = never

    Profile     profile;
    std::string controller_key;      // stable ID for profile assignment (MAC / sysfs path)
    std::string evdev_path;          // gamepad event node path
    std::string touchpad_path;       // touchpad event node path (if found)
    std::string motion_path;         // motion event node path (if found)
};

/// Summary of a controller for status queries (IPC).
struct DeviceInfo {
    std::string path, name, model, connection, mac, controller_key, profile;
    OutputMode  output_mode = OutputMode::DS4;
    BatteryInfo battery;
    bool has_touchpad = false, has_motion = false;
};

/// Manages discovery, hotplug, and the translate loop for all controllers.
class DeviceManager {
public:
    /// The part of a controller an evdev node belongs to.
    enum class NodeType { Gamepad, Touchpad, Motion, Other };

    /// Looks up the profile for a newly opened controller (by controller key).
    using ProfileResolver = std::function<Profile(const std::string& controller_key)>;

    explicit DeviceManager(DeviceManagerOptions options = {});
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    /// Scan /dev/input/ for supported controllers, open new ones and attach
    /// touchpad / motion nodes that appeared after their gamepad node.
    void scan();

    /// Handle a udev hotplug add event for an evdev path.
    void on_device_added(const std::string& evdev_path);

    /// Handle a udev hotplug remove event: closes the controller if it is a
    /// gamepad node, or detaches it if it is a touchpad / motion node.
    void on_device_removed(const std::string& evdev_path);

    /// Run one iteration of the translate loop for all slots.  Controllers
    /// whose gamepad node fails (disconnected) are closed afterwards.
    void translate_events();

    /// Set profile on a slot (by evdev path): applies the lightbar colour and
    /// switches the virtual output if the output mode changed.
    /// Returns false if there is no such slot.
    bool set_profile(const std::string& evdev_path, const Profile& p);

    /// Reload (via the profile resolver) every slot currently using the
    /// profile `name` — after it was saved or deleted.
    void reapply_profile(const std::string& name);

    void set_profile_resolver(ProfileResolver resolver) { resolver_ = std::move(resolver); }

    /// Temporarily set the physical lightbar (until the next profile change).
    bool set_lightbar(const std::string& evdev_path, std::uint8_t r, std::uint8_t g, std::uint8_t b);

    /// Play a rumble on the physical controller for `duration`.
    bool test_rumble(const std::string& evdev_path, std::uint8_t heavy, std::uint8_t light,
                     std::chrono::milliseconds duration);

    /// Raw input and mapped output state of a controller (for the tester).
    [[nodiscard]] std::optional<std::pair<DS4InputState, DS4InputState>>
    input_state(const std::string& evdev_path) const;

    [[nodiscard]] std::vector<DeviceInfo> device_info() const;

    /// File descriptors to wait on (physical nodes and virtual outputs).
    [[nodiscard]] std::vector<int> poll_fds() const;

    /// True once after the set of poll_fds() changed (slot or output added /
    /// removed), so the caller can rebuild its epoll set.
    [[nodiscard]] bool take_fds_changed() noexcept;

    /// Access all active slots (for status queries).
    [[nodiscard]] const auto& slots() const noexcept { return slots_; }

private:
    void emit(ControllerSlot& slot);
    void apply_rumble(ControllerSlot& slot, std::chrono::steady_clock::time_point now);
    void create_output(ControllerSlot& slot, OutputMode mode);
    void detach_touchpad(ControllerSlot& slot);
    void detach_motion(ControllerSlot& slot);
    bool open_slot(const std::string& evdev_path, const std::string& physical_id);
    void close_slot(const std::string& evdev_path);
    /// A supported controller node found while scanning.
    struct Node {
        std::string path;
        std::string physical_id;  // sysfs path of the parent HID / USB device
        std::string name;
        NodeType    type = NodeType::Other;
        bool        generic = false;
    };
    /// Open and inspect one evdev node; nullopt unless it is a supported
    /// controller's gamepad / touchpad / motion node (never our own).
    [[nodiscard]] std::optional<Node> probe_node(const std::string& path) const;
    [[nodiscard]] bool is_open_node(const std::string& evdev_path) const;
    [[nodiscard]] bool generic_allowed(std::uint16_t vid, std::uint16_t pid) const;
    [[nodiscard]] std::array<std::uint8_t, 6>
    allocate_virtual_mac(const std::string& seed, const std::string& physical_uniq) const;

    DeviceManagerOptions options_;
    ProfileResolver resolver_;
    bool fds_changed_ = true;
    std::unordered_map<std::string, ControllerSlot> slots_;
};

} // namespace ds4linux::daemon

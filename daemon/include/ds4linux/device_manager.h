#pragma once
// ds4linux::daemon — Device manager: orchestrates physical → virtual mapping

#include "ds4linux/input_device.h"
#include "ds4linux/virtual_device.h"

#include <ds4linux/profile.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace ds4linux::daemon {

/// Holds the state for a single controller slot.
struct ControllerSlot {
    // Physical input devices (one per event node)
    std::unique_ptr<InputDevice>   physical;          // gamepad (sticks/buttons)
    std::unique_ptr<InputDevice>   physical_touchpad; // touchpad
    std::unique_ptr<InputDevice>   physical_motion;   // motion sensors

    // Virtual output devices
    std::unique_ptr<VirtualDevice>   virtual_dev;
    std::unique_ptr<VirtualTouchpad> virtual_touchpad;
    std::unique_ptr<VirtualMotion>   virtual_motion;

    // Accumulated DS4 gamepad state (packed into HID report on SYN_REPORT)
    DS4InputState ds4_state;

    Profile     profile;
    std::string evdev_path;          // gamepad event node path
    std::string touchpad_path;       // touchpad event node path (if found)
    std::string motion_path;         // motion event node path (if found)
};

/// Manages discovery, hotplug, and the translate loop for all controllers.
class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    /// Scan /dev/input/ for matching Sony controllers and open them.
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
    void open_slot(const std::string& evdev_path);
    void close_slot(const std::string& evdev_path);

    std::unordered_map<std::string, ControllerSlot> slots_;
};

} // namespace ds4linux::daemon

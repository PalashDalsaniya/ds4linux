#pragma once
// ds4linux::daemon — Physical input device abstraction (libevdev + hidraw)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct libevdev;

namespace ds4linux::daemon {

/// Hardware connection type.
enum class ConnectionType : std::uint8_t { USB, Bluetooth };

/// Controller model (determined from VID/PID).
enum class ControllerModel : std::uint8_t {
    DS4v1,
    DS4v2,
    DualSense,
    DualSenseEdge,
    Unknown,
};

/// Battery state reported by the kernel power_supply for a controller.
struct BatteryInfo {
    int  percent = -1;            // 0–100, -1 = unknown
    bool cable_connected = false;
    bool full = false;
};

/// Represents one physical Sony controller attached to the system.
class InputDevice {
public:
    /// Open a /dev/input/eventX device and optionally grab exclusive access.
    /// Throws std::system_error on failure.
    explicit InputDevice(const std::string& evdev_path, bool grab_exclusive = true);
    ~InputDevice();

    InputDevice(const InputDevice&) = delete;
    InputDevice& operator=(const InputDevice&) = delete;
    InputDevice(InputDevice&&) noexcept;
    InputDevice& operator=(InputDevice&&) noexcept;

    /// The file descriptor for epoll registration.
    [[nodiscard]] int fd() const noexcept;

    /// Human-readable device name.
    [[nodiscard]] std::string name() const;

    /// Deduced controller model.
    [[nodiscard]] ControllerModel model() const noexcept;

    /// Connection type (USB / BT).
    [[nodiscard]] ConnectionType connection() const noexcept;

    /// Read one event from the device.  Returns false if no event is pending.
    /// Calls `handler` for every input_event consumed.
    using EventHandler = std::function<void(std::uint16_t type,
                                            std::uint16_t code,
                                            std::int32_t  value)>;
    bool read_events(const EventHandler& handler);

    /// Open the corresponding hidraw device for LED / rumble control.
    /// Returns the fd, or -1 if not found.
    [[nodiscard]] int open_hidraw() const;

    /// Set lightbar color via hidraw.
    bool set_lightbar(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    /// Set rumble motors via hidraw.
    bool set_rumble(std::uint8_t heavy, std::uint8_t light);

    /// Read battery percentage (0–100, or -1 on failure).
    [[nodiscard]] int battery_percent() const;

    /// Read this controller's battery level and charging state from sysfs.
    [[nodiscard]] BatteryInfo battery() const;

    /// Maximum value of an absolute axis, or 0 if the axis is absent.
    [[nodiscard]] int abs_maximum(std::uint16_t code) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

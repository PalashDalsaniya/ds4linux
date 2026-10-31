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
    Generic,        // any other evdev gamepad (opt-in, see --generic)
};

/// Charging state as reported by the kernel power_supply "status" attribute.
enum class BatteryStatus : std::uint8_t {
    Unknown,
    Charging,
    Discharging,
    NotCharging,   // cable connected but not charging (e.g. temperature fault)
    Full,
};

/// Battery state reported by the kernel power_supply for a controller.
struct BatteryInfo {
    int           percent = -1;   // 0–100, -1 = no battery information
    BatteryStatus status  = BatteryStatus::Unknown;
};

/// Range of an absolute axis.  `present` is false if the device lacks it.
struct AbsRange {
    bool present = false;
    int  minimum = 0;
    int  maximum = 0;
    int  resolution = 0;
};

/// One multitouch contact as currently tracked by libevdev.
struct MtContact {
    int tracking_id = -1;         // -1 = no contact in this slot
    int x = 0;
    int y = 0;
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

    /// Range of an absolute axis.
    [[nodiscard]] AbsRange abs_range(std::uint16_t code) const;

    /// Whether the device advertises an event code (e.g. EV_KEY / BTN_TL2).
    [[nodiscard]] bool has_event_code(std::uint16_t type, std::uint16_t code) const;

    /// Current state of a multitouch slot, as tracked by libevdev from the
    /// events read so far (including SYN_DROPPED resyncs).
    [[nodiscard]] MtContact mt_contact(int slot) const;

    /// The device's unique ID (MAC for Sony controllers), may be empty.
    [[nodiscard]] std::string uniq() const;

private:
    bool send_ds4_output();
    bool send_ff_rumble(std::uint8_t heavy, std::uint8_t light);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

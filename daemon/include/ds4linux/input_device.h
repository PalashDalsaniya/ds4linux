#pragma once
// ds4linux::daemon — Physical input device abstraction (libevdev + hidraw)

#include <cstdint>
#include <memory>
#include <type_traits>
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
/// The sysfs directory of the device an evdev node belongs to (the
/// controller's HID device, or the USB interface of a generic pad):
/// /sys/class/input/eventN/device/device.  Its hidraw/ and power_supply/
/// children belong to the same controller.
[[nodiscard]] std::string hid_sysfs_dir(const std::string& evdev_path);

class InputDevice {
public:
    /// Open a /dev/input/eventX device and grab exclusive access to it.
    /// Throws std::system_error on failure.
    explicit InputDevice(const std::string& evdev_path);
    ~InputDevice();

    InputDevice(const InputDevice&) = delete;
    InputDevice& operator=(const InputDevice&) = delete;

    /// The file descriptor for epoll registration.
    [[nodiscard]] int fd() const noexcept;

    /// Human-readable device name.
    [[nodiscard]] std::string name() const;

    /// Deduced controller model.
    [[nodiscard]] ControllerModel model() const noexcept;

    /// Connection type (USB / BT).
    [[nodiscard]] ConnectionType connection() const noexcept;

    /// Non-owning reference to an event callback `(type, code, value)`:
    /// read on every frame, so it must not allocate like std::function.
    /// Only valid while the referenced callable lives (pass lambdas directly).
    class EventHandler {
    public:
        template <typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, EventHandler>)
        EventHandler(F&& f) noexcept  // NOLINT(google-explicit-constructor)
            : obj_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
              call_([](void* o, std::uint16_t t, std::uint16_t c, std::int32_t v) {
                  (*static_cast<std::remove_reference_t<F>*>(o))(t, c, v);
              }) {}
        void operator()(std::uint16_t type, std::uint16_t code, std::int32_t value) const {
            call_(obj_, type, code, value);
        }
    private:
        void* obj_;
        void (*call_)(void*, std::uint16_t, std::uint16_t, std::int32_t);
    };

    /// Read every pending event, calling `handler` for each.  Returns false
    /// on a read error (the device was removed), true otherwise.
    bool read_events(EventHandler handler);

    /// Call `handler` with the current value of every key and absolute axis
    /// the device has — evdev only reports changes, so this seeds state that
    /// was already held when the device was opened.
    void replay_current_state(EventHandler handler) const;

    /// Set lightbar color via hidraw.
    bool set_lightbar(std::uint8_t r, std::uint8_t g, std::uint8_t b);

    /// Set rumble motors via hidraw.
    bool set_rumble(std::uint8_t heavy, std::uint8_t light);

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
    /// Open the controller's hidraw node (LED / rumble).  -1 if not found.
    [[nodiscard]] int open_hidraw() const;
    [[nodiscard]] bool is_dualsense() const noexcept;
    bool send_output();   // the DS4 or DualSense report, from the stored state
    bool ensure_hidraw();
    bool send_ds4_output();
    bool send_dualsense_output();
    void detect_dualsense_vibration_mode();
    bool send_ff_rumble(std::uint8_t heavy, std::uint8_t light);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

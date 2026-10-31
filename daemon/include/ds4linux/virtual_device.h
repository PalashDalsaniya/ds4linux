#pragma once
// ds4linux::daemon — Virtual output device (UHID DualShock 4)
// Creates a virtual DS4 via /dev/uhid, producing both event and hidraw nodes.
// The kernel DS4 driver creates the touchpad and motion sensor nodes itself
// from the touch / IMU data carried in the input report.

#include "ds4linux/input_device.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace ds4linux::daemon {

struct TouchPoint {
    bool active = false;
    std::uint8_t id = 0;      // 7-bit tracking ID
    std::uint16_t x = 0;      // DS4 touchpad units (0–kDS4TouchpadMaxX)
    std::uint16_t y = 0;      // DS4 touchpad units (0–kDS4TouchpadMaxY)
};

// ── DS4 input state — accumulated from physical DualSense events ─────────────

struct DS4InputState {
    std::uint8_t lx = 0x80;   // Left stick X  (0=left, 128=center, 255=right)
    std::uint8_t ly = 0x80;   // Left stick Y  (0=up,   128=center, 255=down)
    std::uint8_t rx = 0x80;   // Right stick X
    std::uint8_t ry = 0x80;   // Right stick Y

    int hat_x = 0;            // D-pad X: -1=left, 0=center, 1=right
    int hat_y = 0;            // D-pad Y: -1=up,   0=center, 1=down

    bool square   = false;
    bool cross    = false;
    bool circle   = false;
    bool triangle = false;
    bool l1       = false;
    bool r1       = false;
    bool l2_btn   = false;    // L2 digital button
    bool r2_btn   = false;    // R2 digital button
    bool share    = false;
    bool options  = false;
    bool l3       = false;
    bool r3       = false;
    bool ps       = false;    // PS/Home button
    bool touchpad = false;    // Touchpad click

    std::uint8_t l2 = 0;     // L2 trigger analog (0–255)
    std::uint8_t r2 = 0;     // R2 trigger analog (0–255)

    // Raw DS4 IMU units (see kDS4GyroPerDegS in constants.h)
    std::int16_t gyro_x = 0;  // pitch
    std::int16_t gyro_y = 0;  // yaw
    std::int16_t gyro_z = 0;  // roll
    std::int16_t accel_x = 0;
    std::int16_t accel_y = 0;
    std::int16_t accel_z = 0;

    BatteryInfo battery;

    // Controller sensor clock (µs, wraps at 32 bits) from the motion node's
    // MSC_TIMESTAMP; without it the report timestamp uses the system clock.
    bool          has_sensor_time = false;
    std::uint32_t sensor_time_us  = 0;

    TouchPoint touches[2];
};

// ── Rumble/LED output from applications → forwarded to physical device ───────

struct RumbleOutput {
    int heavy = -1;           // Left/strong motor (0–255), -1 = no update
    int light = -1;           // Right/weak motor  (0–255), -1 = no update
    std::uint8_t led_r = 0, led_g = 0, led_b = 0;
    bool led_changed = false;
};

/// Creates and manages a virtual DS4 gamepad via /dev/uhid.
/// This produces both /dev/input/eventX and /dev/hidrawX nodes.
class VirtualDevice {
public:
    /// Create a virtual DS4 gamepad via UHID with the given MAC address
    /// (display order, e.g. {0x30, 0x0E, 0xD5, ...}).  The MAC must be unique:
    /// hid-playstation refuses to bind two devices with the same MAC.
    /// Throws std::system_error on failure.
    explicit VirtualDevice(const std::array<std::uint8_t, 6>& mac);
    ~VirtualDevice();

    VirtualDevice(const VirtualDevice&) = delete;
    VirtualDevice& operator=(const VirtualDevice&) = delete;
    VirtualDevice(VirtualDevice&&) noexcept;
    VirtualDevice& operator=(VirtualDevice&&) noexcept;

    /// Pack the DS4 input state into an HID report and dispatch via UHID.
    void send_report(const DS4InputState& state);

    /// Take the rumble/LED output that applications sent since the last call
    /// (fields are -1 / led_changed=false when nothing new arrived).
    RumbleOutput take_output();

    /// Becomes readable when output is pending (for epoll registration).
    [[nodiscard]] int output_fd() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

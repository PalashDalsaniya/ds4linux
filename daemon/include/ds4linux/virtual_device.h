#pragma once
// ds4linux::daemon — Virtual output device (UHID DualShock 4)
// Creates a virtual DS4 via /dev/uhid, producing both event and hidraw nodes.
// The kernel DS4 driver creates the touchpad and motion sensor nodes itself
// from the touch / IMU data carried in the input report.

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

    int  battery_percent = -1;      // 0–100, -1 = unknown
    bool cable_connected = true;
    bool battery_full    = false;

    int current_touch_slot = 0;     // evdev MT slot being updated, -1 = ignored
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
    /// Create a virtual DS4 gamepad via UHID.
    /// Throws std::system_error on failure.
    VirtualDevice();
    ~VirtualDevice();

    VirtualDevice(const VirtualDevice&) = delete;
    VirtualDevice& operator=(const VirtualDevice&) = delete;
    VirtualDevice(VirtualDevice&&) noexcept;
    VirtualDevice& operator=(VirtualDevice&&) noexcept;

    /// Pack the DS4 input state into an HID report and dispatch via UHID.
    void send_report(const DS4InputState& state);

    /// Read and process pending UHID output/feature events.
    /// Call this when the UHID fd is readable.
    RumbleOutput process_output();

    /// The UHID file descriptor (for epoll registration).
    [[nodiscard]] int fd() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

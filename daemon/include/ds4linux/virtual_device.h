#pragma once
// ds4linux::daemon — Virtual output device (UHID for gamepad, uinput for touchpad/motion)
// Creates virtual DS4 gamepad via /dev/uhid, producing both event and hidraw nodes.
// Touchpad and motion sensor devices still use uinput.

#include <cstdint>
#include <memory>
#include <string>

namespace ds4linux::daemon {

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

/// Creates a virtual touchpad device (multitouch, 2 fingers).
class VirtualTouchpad {
public:
    VirtualTouchpad();
    ~VirtualTouchpad();

    VirtualTouchpad(const VirtualTouchpad&) = delete;
    VirtualTouchpad& operator=(const VirtualTouchpad&) = delete;
    VirtualTouchpad(VirtualTouchpad&&) noexcept;
    VirtualTouchpad& operator=(VirtualTouchpad&&) noexcept;

    void emit(std::uint16_t type, std::uint16_t code, std::int32_t value);
    void syn();
    [[nodiscard]] int fd() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Creates a virtual motion sensor device (gyro + accelerometer).
class VirtualMotion {
public:
    VirtualMotion();
    ~VirtualMotion();

    VirtualMotion(const VirtualMotion&) = delete;
    VirtualMotion& operator=(const VirtualMotion&) = delete;
    VirtualMotion(VirtualMotion&&) noexcept;
    VirtualMotion& operator=(VirtualMotion&&) noexcept;

    void emit(std::uint16_t type, std::uint16_t code, std::int32_t value);
    void syn();
    [[nodiscard]] int fd() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

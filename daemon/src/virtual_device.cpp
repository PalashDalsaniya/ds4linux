// ds4linux::daemon — VirtualDevice (UHID)
//
// VirtualDevice: Creates a DS4 v1 HID device via /dev/uhid, forcing the kernel
// to instantiate both /dev/input/eventX *and* /dev/hidrawX nodes.  This allows
// Wine/Proton to detect the device as a native DualShock 4 via hidraw.  The
// kernel DS4 driver also creates the touchpad and motion sensor event nodes
// from the touch / IMU fields of the input report.

#include "ds4linux/virtual_device.h"
#include "ds4linux/constants.h"
#include "ds4linux/debug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <linux/uhid.h>
#include <mutex>
#include <sys/eventfd.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace ds4linux::daemon {

// ═══════════════════════════════════════════════════════════════════════════════
// DS4 HID Report Descriptor — DualShock 4 v1 (USB, VID 054C PID 05C4)
//
// This is the real descriptor layout from the DS4 v1 hardware.  The input
// report (ID 0x01, 64 bytes) contains:
//   • 4 stick axes (LX, LY, RX, RY) — 8-bit unsigned each
//   • 4-bit hat switch (D-pad: 0–7 directions, 8 = released)
//   • 14 buttons (Square, Cross, Circle, Triangle, L1, R1, L2btn, R2btn,
//                 Share, Options, L3, R3, PS, Touchpad)
//   • 6-bit frame counter
//   • 2 trigger axes (L2, R2) — 8-bit unsigned each
//   • 54 bytes of vendor-specific data (timestamps, IMU, touchpad, battery)
//
// Output report (ID 0x05, 32 bytes) — rumble motors + lightbar control.
// Feature reports — calibration, MAC address, firmware info, etc.
// ═══════════════════════════════════════════════════════════════════════════════

// clang-format off
static const std::uint8_t kDS4HidReportDescriptor[] = {
    // Exact 1:1 hex dump from physical DualShock 4 v1 (CUH-ZCT1x).
    // Byte 78 (Logical Maximum for 6-bit counter) kept as 0x3F to avoid
    // Windows hidparse.sys rejecting Sony's native firmware bug.
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02, 0x09, 0x39, 0x15, 0x00, 0x25,
    0x07, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x75, 0x06, 0x95, 0x01, 0x15, 0x00, 0x25, 0x3F, 0x81, 0x02, 0x05,
    0x01, 0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x21, 0x95, 0x36, 0x81, 0x02, 0x85, 0x05, 0x09, 0x22, 0x95, 0x1F, 0x91,
    0x02, 0x85, 0x04, 0x09, 0x23, 0x95, 0x24, 0xB1, 0x02, 0x85, 0x02, 0x09, 0x24, 0x95, 0x24, 0xB1,
    0x02, 0x85, 0x08, 0x09, 0x25, 0x95, 0x03, 0xB1, 0x02, 0x85, 0x10, 0x09, 0x26, 0x95, 0x04, 0xB1,
    0x02, 0x85, 0x11, 0x09, 0x27, 0x95, 0x02, 0xB1, 0x02, 0x85, 0x12, 0x06, 0x02, 0xFF, 0x09, 0x21,
    0x95, 0x0F, 0xB1, 0x02, 0x85, 0x13, 0x09, 0x22, 0x95, 0x16, 0xB1, 0x02, 0x85, 0x14, 0x06, 0x05,
    0xFF, 0x09, 0x20, 0x95, 0x10, 0xB1, 0x02, 0x85, 0x15, 0x09, 0x21, 0x95, 0x2C, 0xB1, 0x02, 0x06,
    0x80, 0xFF, 0x85, 0x80, 0x09, 0x20, 0x95, 0x06, 0xB1, 0x02, 0x85, 0x81, 0x09, 0x21, 0x95, 0x06,
    0xB1, 0x02, 0x85, 0x82, 0x09, 0x22, 0x95, 0x05, 0xB1, 0x02, 0x85, 0x83, 0x09, 0x23, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0x84, 0x09, 0x24, 0x95, 0x04, 0xB1, 0x02, 0x85, 0x85, 0x09, 0x25, 0x95, 0x06,
    0xB1, 0x02, 0x85, 0x86, 0x09, 0x26, 0x95, 0x06, 0xB1, 0x02, 0x85, 0x87, 0x09, 0x27, 0x95, 0x23,
    0xB1, 0x02, 0x85, 0x88, 0x09, 0x28, 0x95, 0x22, 0xB1, 0x02, 0x85, 0x89, 0x09, 0x29, 0x95, 0x02,
    0xB1, 0x02, 0x85, 0x90, 0x09, 0x30, 0x95, 0x05, 0xB1, 0x02, 0x85, 0x91, 0x09, 0x31, 0x95, 0x03,
    0xB1, 0x02, 0x85, 0x92, 0x09, 0x32, 0x95, 0x03, 0xB1, 0x02, 0x85, 0x93, 0x09, 0x33, 0x95, 0x0C,
    0xB1, 0x02, 0x85, 0xA0, 0x09, 0x40, 0x95, 0x06, 0xB1, 0x02, 0x85, 0xA1, 0x09, 0x41, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xA2, 0x09, 0x42, 0x95, 0x01, 0xB1, 0x02, 0x85, 0xA3, 0x09, 0x43, 0x95, 0x30,
    0xB1, 0x02, 0x85, 0xA4, 0x09, 0x44, 0x95, 0x0D, 0xB1, 0x02, 0x85, 0xA5, 0x09, 0x45, 0x95, 0x15,
    0xB1, 0x02, 0x85, 0xA6, 0x09, 0x46, 0x95, 0x15, 0xB1, 0x02, 0x85, 0xF0, 0x09, 0x47, 0x95, 0x3F,
    0xB1, 0x02, 0x85, 0xF1, 0x09, 0x48, 0x95, 0x3F, 0xB1, 0x02, 0x85, 0xF2, 0x09, 0x49, 0x95, 0x0F,
    0xB1, 0x02, 0x85, 0xA7, 0x09, 0x4A, 0x95, 0x01, 0xB1, 0x02, 0x85, 0xA8, 0x09, 0x4B, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xA9, 0x09, 0x4C, 0x95, 0x08, 0xB1, 0x02, 0x85, 0xAA, 0x09, 0x4E, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xAB, 0x09, 0x4F, 0x95, 0x39, 0xB1, 0x02, 0x85, 0xAC, 0x09, 0x50, 0x95, 0x39,
    0xB1, 0x02, 0x85, 0xAD, 0x09, 0x51, 0x95, 0x0B, 0xB1, 0x02, 0x85, 0xAE, 0x09, 0x52, 0x95, 0x01,
    0xB1, 0x02, 0x85, 0xAF, 0x09, 0x53, 0x95, 0x02, 0xB1, 0x02, 0x85, 0xB0, 0x09, 0x54, 0x95, 0x3F,
    0xB1, 0x02, 0x85, 0xB1, 0x09, 0x55, 0x95, 0x02, 0xB1, 0x02, 0x85, 0xB2, 0x09, 0x56, 0x95, 0x02,
    0xB1, 0x02, 0xC0
};
// clang-format on

static constexpr std::size_t kDS4ReportSize = 64; // 1 byte ID + 63 data

// ── D-pad hat switch encoding ────────────────────────────────────────────────
// Converts (hat_x, hat_y) from evdev ABS_HAT0X / ABS_HAT0Y to the DS4
// 4-bit hat value.  0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=none.

static std::uint8_t encode_dpad(int hat_x, int hat_y) {
    // Index table:  hat_x = {-1, 0, 1}  → column 0,1,2
    //               hat_y = {-1, 0, 1}  → row    0,1,2
    static constexpr std::uint8_t table[3][3] = {
        // hat_x:  -1    0    +1
        /* hat_y=-1 */ { 7, 0, 1 },     // NW, N, NE
        /* hat_y= 0 */ { 6, 8, 2 },     // W, none, E
        /* hat_y=+1 */ { 5, 4, 3 },     // SW, S, SE
    };
    return table[hat_y + 1][hat_x + 1];
}

static void put_le16(std::uint8_t* buf, std::size_t off, std::int16_t v) {
    buf[off]     = static_cast<std::uint8_t>(v & 0xFF);
    buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

// ── Feature report helpers ───────────────────────────────────────────────────
// Build calibration data (report 0x02, 37 bytes).
//
// The kernel DS4 driver scales raw gyro by
//   (speed+ + speed-) * 1024 / (axis+ - axis-)
// and raw accel by 2 * 8192 / (axis+ - axis-) around the midpoint.  The values
// below mimic real hardware: gyro at kDS4GyroPerDegS raw units per deg/s and
// accel at 8192 raw units per g, so each 16-bit raw value maps cleanly back
// to the kernel's normalised units.

static void build_calibration_report(std::uint8_t* buf, std::size_t size) {
    std::memset(buf, 0, size);
    buf[0] = 0x02; // report ID

    // Offsets within the 37-byte report (after report ID byte):
    //  1-6:   gyro pitch / yaw / roll bias = 0
    //  7-18:  gyro pitch / yaw / roll +/-  = ±gyro_range
    // 19-22:  gyro speed +/-               =  gyro_speed
    // 23-34:  accel X / Y / Z +/-          = ±8192
    constexpr std::int16_t gyro_speed = 540;  // typical DS4 value
    constexpr std::int16_t gyro_range = gyro_speed * kDS4GyroPerDegS;
    constexpr std::int16_t accel_1g   = kDS4AccelPerG;
    for (std::size_t off : {7, 11, 15}) {
        put_le16(buf, off,      gyro_range);
        put_le16(buf, off + 2, -gyro_range);
    }
    put_le16(buf, 19, gyro_speed);
    put_le16(buf, 21, gyro_speed);
    for (std::size_t off : {23, 27, 31}) {
        put_le16(buf, off,      accel_1g);
        put_le16(buf, off + 2, -accel_1g);
    }
}

// DS4 report timestamps tick in units of 16/3 µs (5.33 µs) and wrap at 16
// bits.  Because 2^32 * 3 / 16 is a multiple of 2^16, converting a wrapping
// 32-bit µs counter this way stays continuous across its wrap too.
static std::uint16_t ds4_timestamp(std::uint64_t us) {
    return static_cast<std::uint16_t>((us * 3) / 16);
}

static std::uint64_t steady_now_us() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count());
}

// Kernel DS4 battery byte: low nibble 0–10 = level in 10% steps.  With the
// cable bit (0x10) set, 0–10 = charging, 11 = full, 14/15 = not charging /
// unknown (the kernel then shows 0%).  Without battery information, report
// cabled + full so nothing raises low-battery warnings.
static std::uint8_t encode_battery(const BatteryInfo& b) {
    constexpr std::uint8_t kCable = 0x10;
    if (b.percent < 0) return kCable | 11;
    auto level = static_cast<std::uint8_t>(std::clamp(b.percent / 10, 0, 10));
    switch (b.status) {
    case BatteryStatus::Discharging: return level;
    case BatteryStatus::Charging:    return kCable | level;
    case BatteryStatus::Full:        return kCable | 11;
    case BatteryStatus::NotCharging: return kCable | 14;
    case BatteryStatus::Unknown:
        // hid-playstation reports Unknown at 100% until its first input
        // report, and at 0% for real error states — keep a known level.
        if (b.percent > 0) return kCable | level;
        break;
    }
    return kCable | 15;
}

// ═══════════════════════════════════════════════════════════════════════════════
// VirtualDevice — DS4 gamepad via /dev/uhid
// ═══════════════════════════════════════════════════════════════════════════════

struct VirtualDevice::Impl {
    int fd = -1;
    int output_event_fd = -1;       // signalled when `pending` has output
    std::uint8_t counter = 0;       // 6-bit frame counter
    std::uint8_t touch_counter = 0; // touch packet timestamp
    std::array<std::uint8_t, 6> mac{};
    std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();

    // Permanent background thread variables.  The I/O thread is the only
    // reader of the UHID fd; it hands rumble/LED output to the main loop
    // through `pending` + `output_event_fd`.
    std::atomic<bool> running{false};
    std::thread io_thread;
    std::mutex pending_mutex;
    RumbleOutput pending;

    void handle_events();

    ~Impl() {
        if (fd >= 0) {
            // Safely shut down the background thread
            running.store(false, std::memory_order_relaxed);
            if (io_thread.joinable()) {
                io_thread.join();
            }

            // Destroy the UHID device
            struct uhid_event ev{};
            ev.type = UHID_DESTROY;
            [[maybe_unused]] auto _ = ::write(fd, &ev, sizeof(ev));
            ::close(fd);
        }
        if (output_event_fd >= 0) ::close(output_event_fd);
    }
};

VirtualDevice::VirtualDevice(const std::array<std::uint8_t, 6>& mac)
    : impl_(std::make_unique<Impl>())
{
    impl_->mac = mac;

    impl_->output_event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (impl_->output_event_fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "eventfd failed");
    }

    // ── Open /dev/uhid ───────────────────────────────────────────────────────
    impl_->fd = ::open("/dev/uhid", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (impl_->fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to open /dev/uhid");
    }

    // ── UHID_CREATE2 ────────────────────────────────────────────────────────
    struct uhid_event ev{};
    ev.type = UHID_CREATE2;

    auto& cr = ev.u.create2;
    std::strncpy(reinterpret_cast<char*>(cr.name),
                 "Wireless Controller",  // as a real DS4 (and ViGEm's) reports it
                 sizeof(cr.name) - 1);
    std::strncpy(reinterpret_cast<char*>(cr.phys),
                 std::string(kVirtualDS4Phys).c_str(),
                 sizeof(cr.phys) - 1);
    std::snprintf(reinterpret_cast<char*>(cr.uniq), sizeof(cr.uniq),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cr.bus     = BUS_USB;
    cr.vendor  = kVidSony;
    cr.product = kPidDS4v1;     // DS4 v1 (CUH-ZCT1x)
    cr.version = 0x0100;        // bcdDevice of a real DS4 v1
    cr.country = 0;

    // Embed the HID report descriptor
    static_assert(sizeof(kDS4HidReportDescriptor) <= sizeof(cr.rd_data),
                  "DS4 HID descriptor exceeds UHID_DATA_MAX");
    std::memcpy(cr.rd_data, kDS4HidReportDescriptor,
                sizeof(kDS4HidReportDescriptor));
    cr.rd_size = sizeof(kDS4HidReportDescriptor);

    // ── Permanent Background I/O Thread ──────────────────────────────────────
    // The kernel probe runs asynchronously, meaning the driver may request the
    // MAC address (0x81 / 0x12) after this constructor returns, and feature
    // requests block the requester until answered.  A dedicated thread
    // answers them and collects rumble/LED output for the main loop.  It
    // captures the heap-allocated Impl (not `this`) so moving the
    // VirtualDevice cannot leave it with a dangling pointer.
    impl_->running.store(true, std::memory_order_relaxed);
    impl_->io_thread = std::thread([impl = impl_.get()]() {
        struct pollfd pfd{};
        pfd.fd = impl->fd;
        pfd.events = POLLIN;
        while (impl->running.load(std::memory_order_relaxed)) {
            // Poll with a 100ms timeout to periodically check the 'running' flag
            if (::poll(&pfd, 1, 100) > 0) {
                impl->handle_events();
            }
        }
    });

    if (::write(impl_->fd, &ev, sizeof(ev)) < 0) {
        int err = errno;
        impl_->running.store(false, std::memory_order_relaxed);
        impl_->io_thread.join();
        ::close(impl_->fd);
        impl_->fd = -1;
        throw std::system_error(err, std::system_category(),
                                "UHID_CREATE2 failed");
    }

    std::cout << "[ds4linux] Virtual DS4 gamepad created via UHID "
              << "(VID=054C PID=05C4, MAC=" << cr.uniq << ")\n";
}

VirtualDevice::~VirtualDevice() = default;
VirtualDevice::VirtualDevice(VirtualDevice&&) noexcept = default;
VirtualDevice& VirtualDevice::operator=(VirtualDevice&&) noexcept = default;

int VirtualDevice::output_fd() const noexcept { return impl_->output_event_fd; }

// hid-playstation sets a default lightbar colour while probing the virtual
// DS4; drop LED output this soon after creation so it can't override the
// profile colour already applied to the physical controller.
static constexpr auto kProbeLedGrace = std::chrono::seconds(2);

RumbleOutput VirtualDevice::take_output() {
    std::uint64_t count = 0;
    [[maybe_unused]] auto _ = ::read(impl_->output_event_fd, &count, sizeof(count));

    std::lock_guard lock(impl_->pending_mutex);
    RumbleOutput out = impl_->pending;
    impl_->pending = {};
    if (std::chrono::steady_clock::now() - impl_->created_at < kProbeLedGrace) {
        out.led_changed = false;
    }
    return out;
}

// ── send_report: pack DS4InputState → 64-byte HID report → UHID_INPUT2 ──────

void VirtualDevice::send_report(const DS4InputState& s) {
    // DS4 USB Input Report layout (64 bytes, report ID 0x01):
    //
    //  Byte  0: Report ID = 0x01
    //  Byte  1: Left stick X   (0–255, 0x80 center)
    //  Byte  2: Left stick Y   (0–255, 0x80 center)
    //  Byte  3: Right stick X  (0–255, 0x80 center)
    //  Byte  4: Right stick Y  (0–255, 0x80 center)
    //  Byte  5: [Hat:4] [Square:1] [Cross:1] [Circle:1] [Triangle:1]
    //  Byte  6: [L1:1] [R1:1] [L2:1] [R2:1] [Share:1] [Options:1] [L3:1] [R3:1]
    //  Byte  7: [PS:1] [Touchpad:1] [Counter:6]
    //  Byte  8: L2 trigger analog (0–255)
    //  Byte  9: R2 trigger analog (0–255)
    //  Bytes 10–11: Timestamp (uint16_t LE, 5.33 µs units)
    //  Byte  12: Sensor temperature (unused)
    //  Bytes 13–18: Gyro pitch / yaw / roll (int16 LE)
    //  Bytes 19–24: Accel X / Y / Z (int16 LE)
    //  Byte  30: [Cable:1 @ bit 4] [Battery level:4]
    //  Byte  33: Number of touch packets
    //  Byte  34: Touch packet timestamp
    //  Bytes 35–38: Touch point 0, Bytes 39–42: Touch point 1

    std::uint8_t report[kDS4ReportSize]{};

    report[0] = 0x01;                       // Report ID

    // Sticks
    report[1] = s.lx;
    report[2] = s.ly;
    report[3] = s.rx;
    report[4] = s.ry;

    // Byte 5: hat switch (low nibble) + buttons (high nibble)
    std::uint8_t hat = encode_dpad(s.hat_x, s.hat_y);
    report[5] = (hat & 0x0F)
              | (s.square   ? (1u << 4) : 0)
              | (s.cross    ? (1u << 5) : 0)
              | (s.circle   ? (1u << 6) : 0)
              | (s.triangle ? (1u << 7) : 0);

    // Byte 6: more buttons
    report[6] = (s.l1      ? (1u << 0) : 0)
              | (s.r1      ? (1u << 1) : 0)
              | (s.l2_btn  ? (1u << 2) : 0)
              | (s.r2_btn  ? (1u << 3) : 0)
              | (s.share   ? (1u << 4) : 0)
              | (s.options ? (1u << 5) : 0)
              | (s.l3      ? (1u << 6) : 0)
              | (s.r3      ? (1u << 7) : 0);

    // Byte 7: PS + Touchpad + 6-bit counter
    report[7] = (s.ps       ? (1u << 0) : 0)
              | (s.touchpad ? (1u << 1) : 0)
              | ((impl_->counter & 0x3F) << 2);
    impl_->counter = (impl_->counter + 1) & 0x3F;

    // Triggers
    report[8] = s.l2;
    report[9] = s.r2;

    const std::uint64_t ts_us = s.has_sensor_time ? s.sensor_time_us : steady_now_us();
    put_le16(report, 10, static_cast<std::int16_t>(ds4_timestamp(ts_us)));

    // IMU
    put_le16(report, 13, s.gyro_x);
    put_le16(report, 15, s.gyro_y);
    put_le16(report, 17, s.gyro_z);
    put_le16(report, 19, s.accel_x);
    put_le16(report, 21, s.accel_y);
    put_le16(report, 23, s.accel_z);

    // Battery level & cable state
    report[30] = encode_battery(s.battery);

    // ── Bytes 33–42: Touchpad (one packet, two touch points) ─────────────────
    report[33] = 1;
    report[34] = impl_->touch_counter++;

    for (int i = 0; i < 2; ++i) {
        const std::size_t offset = 35 + (i * 4);
        const auto& t = s.touches[i];

        // Byte 0: bit 7 set = not touching, low 7 bits = tracking ID.
        // Bytes 1–3: two 12-bit coordinates (X then Y), little-endian packed.
        report[offset]     = static_cast<std::uint8_t>((t.active ? 0x00 : 0x80) | (t.id & 0x7F));
        report[offset + 1] = static_cast<std::uint8_t>(t.x & 0xFF);
        report[offset + 2] = static_cast<std::uint8_t>(((t.x >> 8) & 0x0F) | ((t.y & 0x0F) << 4));
        report[offset + 3] = static_cast<std::uint8_t>((t.y >> 4) & 0xFF);
    }

    // ── Dispatch via UHID_INPUT2 ─────────────────────────────────────────────
    struct uhid_event ev{};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = kDS4ReportSize;
    std::memcpy(ev.u.input2.data, report, kDS4ReportSize);

    [[maybe_unused]] auto rc = ::write(impl_->fd, &ev, sizeof(ev));
}

// ── DS4 output report (rumble + lightbar) from an application ────────────────
//   USB: id 0x05, common block at byte 1
//   BT:  id 0x11, [1] hw_control, [2] audio_control, common block at byte 3
//   common: [0] valid_flag0 (0x01 motors, 0x02 LED) [1] valid_flag1
//           [2] reserved [3] right/weak motor [4] left/strong motor [5..7] RGB

static void parse_ds4_output(const std::uint8_t* d, std::size_t sz, const char* via,
                             RumbleOutput& result) {
    std::size_t c = 0;  // offset of the common block
    if (sz >= 1 && d[0] == 0x05) c = 1;
    else if (sz >= 1 && d[0] == 0x11) c = 3;

    if (c == 0 || sz < c + 8) {
        if (g_debug) {
            std::fprintf(stderr, "[ds4linux] debug: ignored output report via %s: "
                         "id=0x%02x len=%zu\n", via, sz ? d[0] : 0u, sz);
        }
        return;
    }
    const std::uint8_t* p = d + c;
    // Only the fields the valid flags select are applied, like a real DS4.
    // (Steam sends reports with no flags at all while rumble is off for the
    // controller in its settings; they must not overwrite the lightbar.)
    const std::uint8_t flags = p[0];
    if (flags & 0x01) {
        result.light = p[3];
        result.heavy = p[4];
    }
    if (flags & 0x02) {
        result.led_r = p[5];
        result.led_g = p[6];
        result.led_b = p[7];
        result.led_changed = true;
    }
    if (g_debug) {
        std::fprintf(stderr, "[ds4linux] debug: app output via %s: id=0x%02x len=%zu "
                     "flags=0x%02x%s motors(heavy=%u light=%u)%s rgb=(%u,%u,%u)%s\n",
                     via, d[0], sz, p[0], p[0] ? "" : " (no fields valid)", p[4], p[3],
                     (flags & 0x01) ? "" : " [ignored]", p[5], p[6], p[7],
                     (flags & 0x02) ? "" : " [ignored]");
    }
}

/// " 05 ff 04 ..." (first 16 bytes) for debug logs.
static std::string hex_bytes(const std::uint8_t* d, std::size_t n) {
    std::string out;
    char b[4];
    for (std::size_t i = 0; i < std::min<std::size_t>(n, 16); ++i) {
        std::snprintf(b, sizeof(b), " %02x", d[i]);
        out += b;
    }
    if (n > 16) out += " ...";
    return out;
}

/// Answer a GET_REPORT / SET_REPORT request.  The requester in the kernel
/// blocks until the reply arrives, so retry briefly if the fd is full.
static void write_reply(int fd, const uhid_event& reply, const char* what) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (::write(fd, &reply, sizeof(reply)) >= 0) return;
        if (errno != EAGAIN) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cerr << "[ds4linux] Failed to answer UHID " << what << ": "
              << std::strerror(errno) << "\n";
}

// ── handle_events: UHID_OUTPUT (rumble/LED) and feature report requests ─────
// Runs on the I/O thread only.

void VirtualDevice::Impl::handle_events() {
    RumbleOutput result;

    while (true) {
        struct uhid_event ev{};
        auto n = ::read(fd, &ev, sizeof(ev));
        if (n < static_cast<ssize_t>(sizeof(ev.type))) break;

        switch (ev.type) {

        case UHID_OUTPUT:
            // Output report written to the hidraw node (or by hid-playstation)
            parse_ds4_output(ev.u.output.data, ev.u.output.size, "write", result);
            break;

        case UHID_GET_REPORT: {
            // Kernel/application requests a feature report.
            // We must reply with UHID_GET_REPORT_REPLY or the caller blocks.
            struct uhid_event reply{};
            reply.type = UHID_GET_REPORT_REPLY;
            reply.u.get_report_reply.id  = ev.u.get_report.id;
            reply.u.get_report_reply.err = 0;

            std::uint8_t rnum = ev.u.get_report.rnum;
            bool known = true;

            // MAC octets in report order (little-endian: last octet first)
            auto put_mac_le = [&](std::uint8_t* dst) {
                for (std::size_t k = 0; k < 6; ++k) dst[k] = mac[5 - k];
            };

            // Provide meaningful data for critical reports; zero-fill others.
            switch (rnum) {
            case 0x02: {
                // Calibration data (37 bytes)
                std::uint8_t cal[37]{};
                build_calibration_report(cal, sizeof(cal));
                reply.u.get_report_reply.size = sizeof(cal);
                std::memcpy(reply.u.get_report_reply.data, cal, sizeof(cal));
                break;
            }
            case 0x81: {
                // MAC address (7 bytes: ID + 6 MAC octets, little-endian)
                std::uint8_t addr[7]{};
                addr[0] = 0x81;
                put_mac_le(addr + 1);
                reply.u.get_report_reply.size = sizeof(addr);
                std::memcpy(reply.u.get_report_reply.data, addr, sizeof(addr));
                break;
            }
            case 0xA3: {
                // Firmware info (49 bytes), as a real DS4 answers it: the
                // reference blob ViGEmBus uses for its virtual DS4 (build
                // date "Aug  3 2013 07:01:12", LE16 hw version 0x3100 at byte
                // 35, fw version 0x0049 at byte 41).  Programs that see
                // zeroes here treat the pad as an unknown clone, and some
                // games check hw version > 0x30FF (from PR #3).
                static constexpr std::uint8_t info[49] = {
                    0xA3, 0x41, 0x75, 0x67, 0x20, 0x20, 0x33, 0x20,
                    0x32, 0x30, 0x31, 0x33, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x30, 0x37, 0x3A, 0x30, 0x31, 0x3A, 0x31,
                    0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x01, 0x00, 0x31, 0x03, 0x00, 0x00,
                    0x00, 0x49, 0x00, 0x05, 0x00, 0x00, 0x80, 0x03,
                    0x00};
                reply.u.get_report_reply.size = sizeof(info);
                std::memcpy(reply.u.get_report_reply.data, info, sizeof(info));
                break;
            }
            case 0x12: {
                // Paired device / link key / MAC (16 bytes)
                std::uint8_t lk[16]{};
                lk[0] = 0x12;
                put_mac_le(lk + 1);

                reply.u.get_report_reply.size = sizeof(lk);
                std::memcpy(reply.u.get_report_reply.data, lk, sizeof(lk));
                break;
            }
            default: {
                known = false;
                // Unknown report — return a minimal zeroed response with the ID.
                // Determine expected size from the descriptor's report count.
                // Fallback: 64 bytes (max common report size).
                std::uint8_t buf[64]{};
                buf[0] = rnum;
                reply.u.get_report_reply.size = sizeof(buf);
                std::memcpy(reply.u.get_report_reply.data, buf, sizeof(buf));
                break;
            }
            } // switch rnum

            if (g_debug) {
                std::fprintf(stderr, "[ds4linux] debug: app read feature report 0x%02x "
                             "(type %u) -> %u bytes%s\n", rnum,
                             unsigned(ev.u.get_report.rtype), unsigned(reply.u.get_report_reply.size),
                             known ? "" : " (unknown report, answered with zeroes)");
            }
            write_reply(fd, reply, "GET_REPORT");
            break;
        }

        case UHID_SET_REPORT: {
            // Feature or output report sent as a SET_REPORT request (e.g.
            // hidapi's hid_send_output_report / HIDIOCSOUTPUT).  Output
            // reports carry rumble / LED like UHID_OUTPUT; acknowledge all.
            const auto& sr = ev.u.set_report;
            if (g_debug) {
                std::fprintf(stderr, "[ds4linux] debug: app sent SET_REPORT 0x%02x (%s) %u bytes:%s\n",
                             sr.rnum, sr.rtype == UHID_OUTPUT_REPORT ? "output"
                                    : sr.rtype == UHID_FEATURE_REPORT ? "feature" : "input",
                             unsigned(sr.size), hex_bytes(sr.data, sr.size).c_str());
            }
            // Effects are output reports, but accept the DS4 effects report
            // IDs whatever type the program used
            if (sr.rtype == UHID_OUTPUT_REPORT ||
                (sr.size > 0 && (sr.data[0] == 0x05 || sr.data[0] == 0x11))) {
                parse_ds4_output(sr.data, sr.size, "set_report", result);
            }
            struct uhid_event reply{};
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id  = ev.u.set_report.id;
            reply.u.set_report_reply.err = 0;
            write_reply(fd, reply, "SET_REPORT");
            break;
        }

        case UHID_OPEN:
        case UHID_CLOSE:
            // First reader opened / last reader closed the device
            if (g_debug) {
                std::fprintf(stderr, "[ds4linux] debug: virtual DS4 %s\n",
                             ev.type == UHID_OPEN ? "opened by a program" : "closed by all programs");
            }
            break;

        default:
            if (g_debug) {
                std::fprintf(stderr, "[ds4linux] debug: unhandled UHID event %u\n", unsigned(ev.type));
            }
            break;
        } // switch ev.type
    }

    if (result.heavy >= 0 || result.led_changed) {
        {
            std::lock_guard lock(pending_mutex);
            if (result.heavy >= 0) {
                pending.heavy = result.heavy;
                pending.light = result.light;
            }
            if (result.led_changed) {
                pending.led_r = result.led_r;
                pending.led_g = result.led_g;
                pending.led_b = result.led_b;
                pending.led_changed = true;
            }
        }
        std::uint64_t one = 1;
        [[maybe_unused]] auto _ = ::write(output_event_fd, &one, sizeof(one));
    }
}

} // namespace ds4linux::daemon

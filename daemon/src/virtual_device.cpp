// ds4linux::daemon — VirtualDevice (UHID) / VirtualTouchpad / VirtualMotion (uinput)
//
// VirtualDevice: Creates a DS4 v1 HID device via /dev/uhid, forcing the kernel
// to instantiate both /dev/input/eventX *and* /dev/hidrawX nodes.  This allows
// Wine/Proton to detect the device as a native DualShock 4 via hidraw.
//
// VirtualTouchpad and VirtualMotion remain uinput-based.

#include "ds4linux/virtual_device.h"
#include "ds4linux/constants.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <linux/uhid.h>
#include <linux/uinput.h>
#include <stdexcept>
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

// ── Feature report helpers ───────────────────────────────────────────────────
// Build default calibration data (report 0x02, 37 bytes).
// Provides neutral biases and unity-gain scale so hid-sony initialises cleanly.

static void build_calibration_report(std::uint8_t* buf, std::size_t size) {
    std::memset(buf, 0, size);
    buf[0] = 0x02; // report ID

    // Gyro bias: all zero (no offset)
    // Gyro plus/minus speed: ±8192 (default scale)
    auto put_le16 = [&](std::size_t off, std::int16_t v) {
        buf[off]     = static_cast<std::uint8_t>(v & 0xFF);
        buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    };
    // Offsets within the 37-byte report (after report ID byte):
    //  1-2: gyro pitch bias  = 0
    //  3-4: gyro yaw bias    = 0
    //  5-6: gyro roll bias   = 0
    //  7-8:  gyro pitch +    =  8192
    //  9-10: gyro pitch -    = -8192
    // 11-12: gyro yaw +      =  8192
    // 13-14: gyro yaw -      = -8192
    // 15-16: gyro roll +     =  8192
    // 17-18: gyro roll -     = -8192
    // 19-20: gyro speed +    =  8192
    // 21-22: gyro speed -    =  8192
    // 23-24: accel X +       =  8192
    // 25-26: accel X -       = -8192
    // 27-28: accel Y +       =  8192
    // 29-30: accel Y -       = -8192
    // 31-32: accel Z +       =  8192
    // 33-34: accel Z -       = -8192
    constexpr std::int16_t pos =  8192;
    constexpr std::int16_t neg = -8192;
    put_le16( 7, pos);  put_le16( 9, neg);
    put_le16(11, pos);  put_le16(13, neg);
    put_le16(15, pos);  put_le16(17, neg);
    put_le16(19, pos);  put_le16(21, pos);
    put_le16(23, pos);  put_le16(25, neg);
    put_le16(27, pos);  put_le16(29, neg);
    put_le16(31, pos);  put_le16(33, neg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// VirtualDevice — DS4 gamepad via /dev/uhid
// ═══════════════════════════════════════════════════════════════════════════════

struct VirtualDevice::Impl {
    int fd = -1;
    std::uint8_t counter = 0; // 6-bit frame counter

    // Permanent background thread variables
    std::atomic<bool> running{false};
    std::thread io_thread;

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
    }
};

VirtualDevice::VirtualDevice()
    : impl_(std::make_unique<Impl>())
{
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
                 "Sony Computer Entertainment Wireless Controller",
                 sizeof(cr.name) - 1);
    std::strncpy(reinterpret_cast<char*>(cr.phys),
                 "ds4linux/virtual",
                 sizeof(cr.phys) - 1);
    std::strncpy(reinterpret_cast<char*>(cr.uniq),
                 "30:0e:d5:89:8c:29",
                 sizeof(cr.uniq) - 1);

    cr.bus     = BUS_USB;
    cr.vendor  = kVidSony;
    cr.product = kPidDS4v1;     // DS4 v1 (CUH-ZCT1x)
    cr.version = 0x0111;        // v1.11 Gamepad
    cr.country = 0;

    // Embed the HID report descriptor
    static_assert(sizeof(kDS4HidReportDescriptor) <= sizeof(cr.rd_data),
                  "DS4 HID descriptor exceeds UHID_DATA_MAX");
    std::memcpy(cr.rd_data, kDS4HidReportDescriptor,
                sizeof(kDS4HidReportDescriptor));
    cr.rd_size = sizeof(kDS4HidReportDescriptor);

    // ── Permanent Background I/O Thread ──────────────────────────────────────
    // The kernel probe runs asynchronously, meaning the driver may request the
    // MAC address (0x81) after this constructor returns. Furthermore, the main
    // loop does not poll the UHID fd for rumble/LED output events. We must keep
    // a thread permanently alive to answer requests and process outputs.
    impl_->running.store(true, std::memory_order_relaxed);
    impl_->io_thread = std::thread([this]() {
        struct pollfd pfd{};
        pfd.fd = impl_->fd;
        pfd.events = POLLIN;
        while (impl_->running.load(std::memory_order_relaxed)) {
            // Poll with a 100ms timeout to periodically check the 'running' flag
            if (::poll(&pfd, 1, 100) > 0) {
                this->process_output();
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
              << "(VID=054C PID=05C4)\n";
}

VirtualDevice::~VirtualDevice() = default;
VirtualDevice::VirtualDevice(VirtualDevice&&) noexcept = default;
VirtualDevice& VirtualDevice::operator=(VirtualDevice&&) noexcept = default;

int VirtualDevice::fd() const noexcept { return impl_->fd; }

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
    //  Bytes 10–11: Timestamp (uint16_t LE, increments)
    //  Byte  12: Battery level
    //  Bytes 13–63: IMU, touchpad, vendor data (zeroed)

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

    // Timestamp (simple incrementing — hid-sony uses this for jitter filtering)
    static std::uint16_t ts = 0;
    ts += 188; // ~5.33 ms at USB poll rate
    report[10] = static_cast<std::uint8_t>(ts & 0xFF);
    report[11] = static_cast<std::uint8_t>((ts >> 8) & 0xFF);

    // Battery level: report as charged USB (0x0B = full, cable connected)
    report[12] = 0x0B;

    // Touchpad: no active touches — set inactive flags
    // Byte 33: number of touch reports = 0
    // Byte 34: touch 0 — contact bit clear, ID 0 (0x80 = inactive)
    report[33] = 0; // touch report count
    report[34] = 0x80;
    report[38] = 0x80;

    // ── Dispatch via UHID_INPUT2 ─────────────────────────────────────────────
    struct uhid_event ev{};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = kDS4ReportSize;
    std::memcpy(ev.u.input2.data, report, kDS4ReportSize);

    [[maybe_unused]] auto rc = ::write(impl_->fd, &ev, sizeof(ev));
}

// ── process_output: handle UHID_OUTPUT (rumble/LED) and UHID_GET_REPORT ──────

RumbleOutput VirtualDevice::process_output() {
    RumbleOutput result;

    while (true) {
        struct uhid_event ev{};
        auto n = ::read(impl_->fd, &ev, sizeof(ev));
        if (n < static_cast<ssize_t>(sizeof(ev.type))) break;

        switch (ev.type) {

        case UHID_OUTPUT: {
            // Application sent an output report (e.g. rumble + LED).
            // DS4 USB Output Report 0x05 layout:
            //   Byte 0: Report ID (0x05)
            //   Byte 1: valid_flag0  (0xFF = all)
            //   Byte 2: valid_flag1
            //   Byte 3: reserved
            //   Byte 4: right/weak motor (0–255)
            //   Byte 5: left/strong motor (0–255)
            //   Byte 6: LED red
            //   Byte 7: LED green
            //   Byte 8: LED blue
            auto* d = ev.u.output.data;
            auto  sz = ev.u.output.size;
            if (sz >= 9 && d[0] == 0x05) {
                std::uint8_t flags = d[1];
                if (flags & 0x01) { // motor control
                    result.light = d[4];
                    result.heavy = d[5];
                }
                if (flags & 0x02) { // LED control
                    result.led_r = d[6];
                    result.led_g = d[7];
                    result.led_b = d[8];
                    result.led_changed = true;
                }
            }
            break;
        }

        case UHID_GET_REPORT: {
            // Kernel/application requests a feature report.
            // We must reply with UHID_GET_REPORT_REPLY or the caller blocks.
            struct uhid_event reply{};
            reply.type = UHID_GET_REPORT_REPLY;
            reply.u.get_report_reply.id  = ev.u.get_report.id;
            reply.u.get_report_reply.err = 0;

            std::uint8_t rnum = ev.u.get_report.rnum;

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
                // MAC address (7 bytes: ID + 6 MAC octets)
                // Authentic DS4 v1 OUI/MAC: 30:0E:D5:89:8C:29
                std::uint8_t mac[7]{};
                mac[0] = 0x81;
                // Reversed for Little-Endian parsing -> 30:0E:D5:89:8C:29
                mac[1] = 0x29; mac[2] = 0x8C; mac[3] = 0x89;
                mac[4] = 0xD5; mac[5] = 0x0E; mac[6] = 0x30;
                reply.u.get_report_reply.size = sizeof(mac);
                std::memcpy(reply.u.get_report_reply.data, mac, sizeof(mac));
                break;
            }
            case 0xA3: {
                // DS4 firmware info (49 bytes) — zeroed with report ID
                std::uint8_t info[49]{};
                info[0] = 0xA3;
                // Byte 1-2: HW version
                info[1] = 0x00; info[2] = 0x01;
                // Reversed for Little-Endian parsing -> 30:0E:D5:89:8C:29
                info[4] = 0x29; info[5] = 0x8C; info[6] = 0x89;
                info[7] = 0xD5; info[8] = 0x0E; info[9] = 0x30;
                reply.u.get_report_reply.size = sizeof(info);
                std::memcpy(reply.u.get_report_reply.data, info, sizeof(info));
                break;
            }
            case 0x12: {
                // Paired device / link key / MAC (16 bytes)
                std::uint8_t lk[16]{};
                lk[0] = 0x12;
                
                // Reversed for Little-Endian parsing -> 30:0E:D5:89:8C:29
                lk[1] = 0x29; lk[2] = 0x8C; lk[3] = 0x89;
                lk[4] = 0xD5; lk[5] = 0x0E; lk[6] = 0x30;
                
                reply.u.get_report_reply.size = sizeof(lk);
                std::memcpy(reply.u.get_report_reply.data, lk, sizeof(lk));
                break;
            }
            default: {
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

            ssize_t rc;
            int retries = 0;
            do {
                rc = ::write(impl_->fd, &reply, sizeof(reply));
                if (rc < 0 && errno == EAGAIN) {
                    retries++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            } while (rc < 0 && errno == EAGAIN && retries < 10);

            if (rc < 0) {
                std::cerr << "[ds4linux] Failed to write UHID_GET_REPORT_REPLY (rnum: "
                          << (int)rnum << ") Error: " << errno << "\n";
            }
            break;
        }

        case UHID_SET_REPORT: {
            // Application sets a feature report.  Acknowledge immediately.
            struct uhid_event reply{};
            reply.type = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id  = ev.u.set_report.id;
            reply.u.set_report_reply.err = 0;
            ssize_t rc;
            int retries = 0;
            do {
                rc = ::write(impl_->fd, &reply, sizeof(reply));
                if (rc < 0 && errno == EAGAIN) {
                    retries++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            } while (rc < 0 && errno == EAGAIN && retries < 50);
            
            if (rc < 0) {
                std::cerr << "[ds4linux] CRITICAL: Failed to send MAC address to kernel. Errno: " << errno << "\n";
            }
            break;
        }

        case UHID_OPEN:
            // A reader opened the hidraw node — no action needed.
            break;
        case UHID_CLOSE:
            // Reader closed — no action needed.
            break;

        default:
            break;
        } // switch ev.type
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shared uinput helpers (for VirtualTouchpad / VirtualMotion only)
// ═══════════════════════════════════════════════════════════════════════════════

static int open_uinput() {
    int fd = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to open /dev/uinput");
    }
    return fd;
}

static void create_device(int fd, struct uinput_setup& setup) {
    if (::ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
        throw std::system_error(errno, std::system_category(),
                                "UI_DEV_SETUP failed");
    }
    if (::ioctl(fd, UI_DEV_CREATE) < 0) {
        throw std::system_error(errno, std::system_category(),
                                "UI_DEV_CREATE failed");
    }
}

static void emit_event(int fd, std::uint16_t type, std::uint16_t code,
                        std::int32_t value) {
    struct input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    [[maybe_unused]] auto _ = ::write(fd, &ev, sizeof(ev));
}

static std::string get_device_node(int fd) {
    char sysname[64]{};
    if (::ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) >= 0) {
        return std::string("/dev/input/") + sysname;
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// VirtualTouchpad — DS4 touchpad (multitouch, 2 finger slots)
// ═══════════════════════════════════════════════════════════════════════════════

struct VirtualTouchpad::Impl {
    int fd = -1;
    ~Impl() {
        if (fd >= 0) { ::ioctl(fd, UI_DEV_DESTROY); ::close(fd); }
    }
};

VirtualTouchpad::VirtualTouchpad()
    : impl_(std::make_unique<Impl>())
{
    impl_->fd = open_uinput();

    // ── Buttons ──────────────────────────────────────────────────────────────
    ::ioctl(impl_->fd, UI_SET_EVBIT, EV_KEY);
    ::ioctl(impl_->fd, UI_SET_KEYBIT, BTN_LEFT);           // touchpad click
    ::ioctl(impl_->fd, UI_SET_KEYBIT, BTN_TOUCH);          // finger contact
    ::ioctl(impl_->fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);    // 1 finger
    ::ioctl(impl_->fd, UI_SET_KEYBIT, BTN_TOOL_DOUBLETAP); // 2 fingers

    // ── Axes ─────────────────────────────────────────────────────────────────
    ::ioctl(impl_->fd, UI_SET_EVBIT, EV_ABS);
    struct uinput_abs_setup abs{};

    // Single-touch (Type A fallback): ABS_X, ABS_Y
    for (auto axis : {ABS_X, ABS_Y}) {
        std::memset(&abs, 0, sizeof(abs));
        abs.code            = static_cast<__u16>(axis);
        abs.absinfo.minimum = 0;
        abs.absinfo.maximum = (axis == ABS_X) ? kTouchpadMaxX : kTouchpadMaxY;
        abs.absinfo.resolution = (axis == ABS_X) ? 44 : 41;
        ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);
    }

    // Multi-touch slot
    std::memset(&abs, 0, sizeof(abs));
    abs.code            = ABS_MT_SLOT;
    abs.absinfo.minimum = 0;
    abs.absinfo.maximum = kTouchpadMaxSlots - 1;
    ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);

    // MT position
    std::memset(&abs, 0, sizeof(abs));
    abs.code            = ABS_MT_POSITION_X;
    abs.absinfo.minimum = 0;
    abs.absinfo.maximum = kTouchpadMaxX;
    abs.absinfo.resolution = 44;
    ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);

    std::memset(&abs, 0, sizeof(abs));
    abs.code            = ABS_MT_POSITION_Y;
    abs.absinfo.minimum = 0;
    abs.absinfo.maximum = kTouchpadMaxY;
    abs.absinfo.resolution = 41;
    ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);

    // MT tracking ID (-1 = released, 0+ = finger id)
    std::memset(&abs, 0, sizeof(abs));
    abs.code            = ABS_MT_TRACKING_ID;
    abs.absinfo.minimum = -1;
    abs.absinfo.maximum = 65535;
    ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);

    // ── Properties ───────────────────────────────────────────────────────────
    ::ioctl(impl_->fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
    ::ioctl(impl_->fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);

    // ── Device identity ──────────────────────────────────────────────────────
    struct uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = kVidSony;
    setup.id.product = kPidDS4v2;
    setup.id.version = 0x0100;
    std::strncpy(setup.name, std::string(kVirtualDS4TouchpadName).c_str(),
                 UINPUT_MAX_NAME_SIZE - 1);

    create_device(impl_->fd, setup);
    std::cout << "[ds4linux] Virtual DS4 touchpad created: "
              << get_device_node(impl_->fd) << "\n";
}

VirtualTouchpad::~VirtualTouchpad() = default;
VirtualTouchpad::VirtualTouchpad(VirtualTouchpad&&) noexcept = default;
VirtualTouchpad& VirtualTouchpad::operator=(VirtualTouchpad&&) noexcept = default;

void VirtualTouchpad::emit(std::uint16_t type, std::uint16_t code, std::int32_t value) {
    emit_event(impl_->fd, type, code, value);
}
void VirtualTouchpad::syn() { emit_event(impl_->fd, EV_SYN, SYN_REPORT, 0); }
int VirtualTouchpad::fd() const noexcept { return impl_->fd; }

// ═══════════════════════════════════════════════════════════════════════════════
// VirtualMotion — DS4 motion sensors (gyro + accel)
// ═══════════════════════════════════════════════════════════════════════════════

struct VirtualMotion::Impl {
    int fd = -1;
    ~Impl() {
        if (fd >= 0) { ::ioctl(fd, UI_DEV_DESTROY); ::close(fd); }
    }
};

VirtualMotion::VirtualMotion()
    : impl_(std::make_unique<Impl>())
{
    impl_->fd = open_uinput();

    // ── Axes: accel (ABS_X, ABS_Y, ABS_Z) + gyro (ABS_RX, ABS_RY, ABS_RZ)
    ::ioctl(impl_->fd, UI_SET_EVBIT, EV_ABS);
    struct uinput_abs_setup abs{};

    // Accelerometer
    for (auto axis : {ABS_X, ABS_Y, ABS_Z}) {
        std::memset(&abs, 0, sizeof(abs));
        abs.code              = static_cast<__u16>(axis);
        abs.absinfo.minimum   = kMotionAccelMin;
        abs.absinfo.maximum   = kMotionAccelMax;
        abs.absinfo.fuzz      = 0;
        abs.absinfo.flat      = 0;
        abs.absinfo.resolution = 8192;
        ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);
    }

    // Gyroscope
    for (auto axis : {ABS_RX, ABS_RY, ABS_RZ}) {
        std::memset(&abs, 0, sizeof(abs));
        abs.code              = static_cast<__u16>(axis);
        abs.absinfo.minimum   = kMotionGyroMin;
        abs.absinfo.maximum   = kMotionGyroMax;
        abs.absinfo.fuzz      = 0;
        abs.absinfo.flat      = 0;
        abs.absinfo.resolution = 1024;
        ::ioctl(impl_->fd, UI_ABS_SETUP, &abs);
    }

    // MSC for timestamp
    ::ioctl(impl_->fd, UI_SET_EVBIT, EV_MSC);
    ::ioctl(impl_->fd, UI_SET_MSCBIT, MSC_TIMESTAMP);

    // ── Device identity ──────────────────────────────────────────────────────
    struct uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = kVidSony;
    setup.id.product = kPidDS4v2;
    setup.id.version = 0x0100;
    std::strncpy(setup.name, std::string(kVirtualDS4MotionName).c_str(),
                 UINPUT_MAX_NAME_SIZE - 1);

    create_device(impl_->fd, setup);
    std::cout << "[ds4linux] Virtual DS4 motion sensors created: "
              << get_device_node(impl_->fd) << "\n";
}

VirtualMotion::~VirtualMotion() = default;
VirtualMotion::VirtualMotion(VirtualMotion&&) noexcept = default;
VirtualMotion& VirtualMotion::operator=(VirtualMotion&&) noexcept = default;

void VirtualMotion::emit(std::uint16_t type, std::uint16_t code, std::int32_t value) {
    emit_event(impl_->fd, type, code, value);
}
void VirtualMotion::syn() { emit_event(impl_->fd, EV_SYN, SYN_REPORT, 0); }
int VirtualMotion::fd() const noexcept { return impl_->fd; }

} // namespace ds4linux::daemon

// ds4linux::daemon — InputDevice implementation (libevdev + hidraw)

#include "ds4linux/input_device.h"
#include "ds4linux/constants.h"

#include <libevdev/libevdev.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

// ── Helpers ──────────────────────────────────────────────────────────────────

static ControllerModel deduce_model(std::uint16_t vid, std::uint16_t pid) {
    if (vid != kVidSony) return ControllerModel::Generic;
    switch (pid) {
        case kPidDS4v1:         return ControllerModel::DS4v1;
        case kPidDS4v2:         return ControllerModel::DS4v2;
        case kPidDualSense:     return ControllerModel::DualSense;
        case kPidDualSenseEdge: return ControllerModel::DualSenseEdge;
        default:                return ControllerModel::Generic;
    }
}

/// Kernel crc32_le semantics: reflected poly 0xEDB88320, no pre/post inversion.
static std::uint32_t crc32_le(std::uint32_t crc, const std::uint8_t* data,
                              std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

/// Sony Bluetooth output reports end with a CRC32 over a 0xA2 seed byte and
/// the report itself (all but the last 4 bytes), stored little-endian.
static void finish_bt_report(std::uint8_t* buf, std::size_t size) {
    const std::uint8_t seed = 0xA2;
    std::uint32_t crc = crc32_le(0xFFFFFFFFu, &seed, 1);
    crc = ~crc32_le(crc, buf, size - 4);
    for (std::size_t i = 0; i < 4; ++i) {
        buf[size - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
}

// ── Pimpl ────────────────────────────────────────────────────────────────────

struct InputDevice::Impl {
    int              fd       = -1;
    libevdev*        dev      = nullptr;
    bool             grabbed  = false;
    ControllerModel  model_   = ControllerModel::Generic;
    ConnectionType   conn_    = ConnectionType::USB;
    std::string      evdev_path_;
    int              hidraw_fd_ = -1;

    // Persistent DS4 output state: like the kernel driver, rumble and
    // lightbar always go out together so one never cancels the other.
    struct {
        std::uint8_t heavy = 0, light = 0;
        std::uint8_t r = 0, g = 0, b = 0;
    } ds4_out;

    // Persistent DualSense output state, sent as one combined report.
    struct {
        bool rumble_valid = false, light_valid = false;
        std::uint8_t heavy = 0, light = 0;
        std::uint8_t r = 0, g = 0, b = 0;
        std::uint8_t bt_seq = 0;
    } ds_out;
    bool ds_vibration_v2 = true;   // see detect_dualsense_vibration_mode()

    int ff_effect_id = -1;   // uploaded FF_RUMBLE effect (generic pads)

    ~Impl() {
        if (grabbed && fd >= 0) {
            ::ioctl(fd, EVIOCGRAB, 0); // release grab
        }
        if (dev) {
            libevdev_free(dev);
        }
        if (fd >= 0) {
            ::close(fd);
        }
        if (hidraw_fd_ >= 0) {
            ::close(hidraw_fd_);
        }
    }
};

// ── Construction ─────────────────────────────────────────────────────────────

InputDevice::InputDevice(const std::string& evdev_path)
    : impl_(std::make_unique<Impl>())
{
    impl_->evdev_path_ = evdev_path;

    // Read-write so generic pads can play force-feedback rumble
    impl_->fd = ::open(evdev_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (impl_->fd < 0) {
        impl_->fd = ::open(evdev_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (impl_->fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to open " + evdev_path);
    }

    int rc = libevdev_new_from_fd(impl_->fd, &impl_->dev);
    if (rc < 0) {
        throw std::system_error(-rc, std::system_category(),
                                "libevdev_new_from_fd failed for " + evdev_path);
    }

    // Deduce model from VID/PID
    auto vid = static_cast<std::uint16_t>(libevdev_get_id_vendor(impl_->dev));
    auto pid = static_cast<std::uint16_t>(libevdev_get_id_product(impl_->dev));
    impl_->model_ = deduce_model(vid, pid);

    // Connection type from the bus (hid-playstation sets uniq on USB too)
    impl_->conn_ = (libevdev_get_id_bustype(impl_->dev) == BUS_BLUETOOTH)
                        ? ConnectionType::Bluetooth
                        : ConnectionType::USB;

    // Grab exclusive access so that the raw device doesn't also produce events
    if (::ioctl(impl_->fd, EVIOCGRAB, 1) < 0) {
        throw std::system_error(errno, std::system_category(),
                                "EVIOCGRAB failed for " + evdev_path);
    }
    impl_->grabbed = true;

}

InputDevice::~InputDevice() = default;

// ── Accessors ────────────────────────────────────────────────────────────────

int InputDevice::fd() const noexcept { return impl_->fd; }

std::string InputDevice::name() const {
    const char* n = libevdev_get_name(impl_->dev);
    return n ? n : "Unknown";
}

ControllerModel InputDevice::model() const noexcept { return impl_->model_; }
ConnectionType  InputDevice::connection() const noexcept { return impl_->conn_; }

// ── Event reading (hot path — keep lean) ─────────────────────────────────────

bool InputDevice::read_events(EventHandler handler) {
    struct input_event ev{};
    int rc = LIBEVDEV_READ_STATUS_SUCCESS;

    while (rc >= 0) {
        rc = libevdev_next_event(impl_->dev,
                                 LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
                                 &ev);
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            handler(ev.type, ev.code, ev.value);
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Device was lagging — drain the sync queue
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                handler(ev.type, ev.code, ev.value);
                rc = libevdev_next_event(impl_->dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
        } else if (rc == -EAGAIN) {
            break; // no more events pending
        } else {
            return false; // error or device removed
        }
    }
    return true;
}

void InputDevice::replay_current_state(EventHandler handler) const {
    for (unsigned code = 0; code <= KEY_MAX; ++code) {
        if (libevdev_has_event_code(impl_->dev, EV_KEY, code)) {
            handler(EV_KEY, static_cast<std::uint16_t>(code),
                    libevdev_get_event_value(impl_->dev, EV_KEY, code));
        }
    }
    for (unsigned code = 0; code < ABS_MT_SLOT; ++code) {
        if (libevdev_has_event_code(impl_->dev, EV_ABS, code)) {
            handler(EV_ABS, static_cast<std::uint16_t>(code),
                    libevdev_get_event_value(impl_->dev, EV_ABS, code));
        }
    }
}

// ── hidraw helpers ───────────────────────────────────────────────────────────

/// Open the hidraw node on first use (only the gamepad node of a slot ever
/// sends output; hiding it from other programs is NodeHider's job).
bool InputDevice::ensure_hidraw() {
    if (impl_->hidraw_fd_ >= 0) return true;
    impl_->hidraw_fd_ = open_hidraw();
    if (impl_->hidraw_fd_ < 0) return false;
    if (impl_->model_ == ControllerModel::DualSense) detect_dualsense_vibration_mode();
    return true;
}

std::string hid_sysfs_dir(const std::string& evdev_path) {
    return "/sys/class/input/" + fs::path(evdev_path).filename().string() + "/device/device";
}

int InputDevice::open_hidraw() const {
    // The hidraw node is a sibling of this evdev node under the same HID device
    const fs::path dir = fs::path(hid_sysfs_dir(impl_->evdev_path_)) / "hidraw";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        auto node = fs::path("/dev") / entry.path().filename();
        int hfd = ::open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (hfd < 0) continue;

        return hfd; // caller owns this fd
    }
    return -1;
}

// ── DS4 output (report 0x05 over USB, 0x11 + CRC32 over Bluetooth) ──────────
// Layout from drivers/hid/hid-playstation.c (dualshock4_output_report_*):
//   common: [0] valid_flag0 [1] valid_flag1 [2] reserved [3] motor_right
//           [4] motor_left [5..7] lightbar RGB [8..9] blink on/off
//   USB: id 0x05 @0, common @1, 32 bytes total
//   BT:  id 0x11 @0, hw_control @1, audio_control @2, common @3,
//        CRC32 (seed 0xA2) over bytes 0..73 @74, 78 bytes total

bool InputDevice::send_ds4_output() {
    constexpr std::uint8_t kFlagMotor = 0x01;
    constexpr std::uint8_t kFlagLed   = 0x02;
    const auto& st = impl_->ds4_out;

    auto fill_common = [&](std::uint8_t* c) {
        c[0] = kFlagMotor | kFlagLed;
        c[3] = st.light;  // motor_right (weak)
        c[4] = st.heavy;  // motor_left (strong)
        c[5] = st.r;
        c[6] = st.g;
        c[7] = st.b;
    };

    if (impl_->conn_ == ConnectionType::Bluetooth) {
        std::uint8_t buf[78]{};
        buf[0] = 0x11;
        buf[1] = 0x80 | 0x40; // HWCTL_HID | HWCTL_CRC32
        fill_common(buf + 3);
        finish_bt_report(buf, sizeof(buf));
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    }

    std::uint8_t buf[32]{};
    buf[0] = 0x05;
    fill_common(buf + 1);
    return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
}

// ── DualSense output (report 0x02 over USB, 0x31 + CRC32 over Bluetooth) ────
// Layout from drivers/hid/hid-playstation.c (dualsense_output_report_*):
//   common: [0] valid_flag0 [1] valid_flag1 [2] motor_right [3] motor_left
//           [38] valid_flag2 [44..46] lightbar RGB
//   USB: id 0x02 @0, common @1, 63 bytes total
//   BT:  id 0x31 @0, seq << 4 @1, tag 0x10 @2, common @3,
//        CRC32 (seed 0xA2) over bytes 0..73 @74, 78 bytes total
// Rumble and lightbar always go out together with persistent state so one
// update never resets the other.  Based on the fix proposed in PR #3.

void InputDevice::detect_dualsense_vibration_mode() {
    // Original DualSense firmware only supports the classic rumble emulation;
    // "vibration v2" needs feature version 2.21+ (firmware info report 0x20,
    // LE16 update_version at offset 44).  The DualSense Edge always has it.
    std::uint8_t buf[64]{};
    buf[0] = 0x20;
    if (::ioctl(impl_->hidraw_fd_, HIDIOCGFEATURE(sizeof(buf)), buf) < 46) return;
    const unsigned update_version = buf[44] | (buf[45] << 8);
    impl_->ds_vibration_v2 = update_version >= 0x0215;  // DS_FEATURE_VERSION(2, 21)
}

bool InputDevice::send_dualsense_output() {
    constexpr std::uint8_t kFlag0CompatVibration  = 0x01;
    constexpr std::uint8_t kFlag0HapticsSelect    = 0x02;
    constexpr std::uint8_t kFlag1LightbarEnable   = 0x04;
    constexpr std::uint8_t kFlag2CompatVibration2 = 0x04;
    auto& st = impl_->ds_out;

    auto fill_common = [&](std::uint8_t* c) {
        if (st.rumble_valid) {
            c[0] |= kFlag0HapticsSelect;
            if (impl_->ds_vibration_v2) c[38] |= kFlag2CompatVibration2;
            else                        c[0]  |= kFlag0CompatVibration;
            c[2] = st.light;  // motor_right (weak)
            c[3] = st.heavy;  // motor_left (strong)
        }
        if (st.light_valid) {
            c[1] |= kFlag1LightbarEnable;
            c[44] = st.r;
            c[45] = st.g;
            c[46] = st.b;
        }
    };

    if (impl_->conn_ == ConnectionType::Bluetooth) {
        std::uint8_t buf[78]{};
        buf[0] = 0x31;
        buf[1] = static_cast<std::uint8_t>(st.bt_seq << 4);
        st.bt_seq = static_cast<std::uint8_t>((st.bt_seq + 1) & 0x0F);
        buf[2] = 0x10;
        fill_common(buf + 3);
        finish_bt_report(buf, sizeof(buf));
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    }

    std::uint8_t buf[63]{};
    buf[0] = 0x02;
    fill_common(buf + 1);
    return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
}

// ── Force-feedback rumble (generic evdev gamepads) ──────────────────────────

bool InputDevice::send_ff_rumble(std::uint8_t heavy, std::uint8_t light) {
    if (!libevdev_has_event_code(impl_->dev, EV_FF, FF_RUMBLE)) return false;

    struct ff_effect effect{};
    effect.type = FF_RUMBLE;
    effect.id   = static_cast<std::int16_t>(impl_->ff_effect_id);
    effect.u.rumble.strong_magnitude = static_cast<std::uint16_t>(heavy * 257);
    effect.u.rumble.weak_magnitude   = static_cast<std::uint16_t>(light * 257);
    effect.replay.length = 0;  // play until stopped / replaced
    if (::ioctl(impl_->fd, EVIOCSFF, &effect) < 0) return false;
    impl_->ff_effect_id = effect.id;

    struct input_event play{};
    play.type  = EV_FF;
    play.code  = static_cast<std::uint16_t>(effect.id);
    play.value = (heavy || light) ? 1 : 0;
    if (::write(impl_->fd, &play, sizeof(play)) != sizeof(play)) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[ds4linux] Could not play rumble on " << impl_->evdev_path_
                      << ": " << std::strerror(errno) << "\n";
            warned = true;
        }
        return false;
    }
    return true;
}

bool InputDevice::set_lightbar(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (impl_->model_ == ControllerModel::Generic) return false; // no lightbar
    if (is_dualsense()) {
        impl_->ds_out.light_valid = true;
        impl_->ds_out.r = r;
        impl_->ds_out.g = g;
        impl_->ds_out.b = b;
    } else {
        impl_->ds4_out.r = r;
        impl_->ds4_out.g = g;
        impl_->ds4_out.b = b;
    }
    return send_output();
}

bool InputDevice::set_rumble(std::uint8_t heavy, std::uint8_t light) {
    if (impl_->model_ == ControllerModel::Generic) {
        return send_ff_rumble(heavy, light);
    }
    if (is_dualsense()) {
        impl_->ds_out.rumble_valid = true;
        impl_->ds_out.heavy = heavy;
        impl_->ds_out.light = light;
    } else {
        impl_->ds4_out.heavy = heavy;
        impl_->ds4_out.light = light;
    }
    return send_output();
}

bool InputDevice::is_dualsense() const noexcept {
    return impl_->model_ == ControllerModel::DualSense ||
           impl_->model_ == ControllerModel::DualSenseEdge;
}

/// Send the stored rumble + lightbar state (they always go out together, so
/// one update never resets the other).
bool InputDevice::send_output() {
    if (!ensure_hidraw()) return false;
    return is_dualsense() ? send_dualsense_output() : send_ds4_output();
}

BatteryInfo InputDevice::battery() const {
    // The kernel registers the battery under this controller's HID device:
    // <hid device>/power_supply/<name>/{capacity,status}
    BatteryInfo info;
    const fs::path supplies = fs::path(hid_sysfs_dir(impl_->evdev_path_)) / "power_supply";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(supplies, ec)) {
        std::ifstream cap(entry.path() / "capacity");
        int pct = -1;
        if (!(cap >> pct)) continue;
        info.percent = pct;

        std::ifstream st(entry.path() / "status");
        std::string status;
        if (std::getline(st, status)) {
            if (status == "Charging")          info.status = BatteryStatus::Charging;
            else if (status == "Discharging")  info.status = BatteryStatus::Discharging;
            else if (status == "Not charging") info.status = BatteryStatus::NotCharging;
            else if (status == "Full")         info.status = BatteryStatus::Full;
        }
        break;
    }
    return info;
}

AbsRange InputDevice::abs_range(std::uint16_t code) const {
    const auto* abs = libevdev_get_abs_info(impl_->dev, code);
    if (!abs) return {};
    return {true, abs->minimum, abs->maximum, abs->resolution};
}

bool InputDevice::has_event_code(std::uint16_t type, std::uint16_t code) const {
    return libevdev_has_event_code(impl_->dev, type, code) != 0;
}

MtContact InputDevice::mt_contact(int slot) const {
    MtContact c;
    int v = 0;
    if (libevdev_fetch_slot_value(impl_->dev, static_cast<unsigned>(slot),
                                  ABS_MT_TRACKING_ID, &v)) {
        c.tracking_id = v;
    }
    if (c.tracking_id >= 0) {
        c.x = libevdev_get_slot_value(impl_->dev, static_cast<unsigned>(slot), ABS_MT_POSITION_X);
        c.y = libevdev_get_slot_value(impl_->dev, static_cast<unsigned>(slot), ABS_MT_POSITION_Y);
    }
    return c;
}

std::string InputDevice::uniq() const {
    const char* u = libevdev_get_uniq(impl_->dev);
    return u ? u : "";
}

} // namespace ds4linux::daemon

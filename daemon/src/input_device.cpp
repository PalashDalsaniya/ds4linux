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
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

// ── Helpers ──────────────────────────────────────────────────────────────────

static ControllerModel deduce_model(std::uint16_t vid, std::uint16_t pid) {
    if (vid != kVidSony) return ControllerModel::Unknown;
    switch (pid) {
        case kPidDS4v1:         return ControllerModel::DS4v1;
        case kPidDS4v2:         return ControllerModel::DS4v2;
        case kPidDualSense:     return ControllerModel::DualSense;
        case kPidDualSenseEdge: return ControllerModel::DualSenseEdge;
        default:                return ControllerModel::Unknown;
    }
}

// ── Pimpl ────────────────────────────────────────────────────────────────────

struct InputDevice::Impl {
    int              fd       = -1;
    libevdev*        dev      = nullptr;
    bool             grabbed  = false;
    ControllerModel  model_   = ControllerModel::Unknown;
    ConnectionType   conn_    = ConnectionType::USB;
    std::string      evdev_path_;
    int              hidraw_fd_ = -1;

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

InputDevice::InputDevice(const std::string& evdev_path, bool grab_exclusive)
    : impl_(std::make_unique<Impl>())
{
    impl_->evdev_path_ = evdev_path;

    impl_->fd = ::open(evdev_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (impl_->fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to open " + evdev_path);
    }

    int rc = libevdev_new_from_fd(impl_->fd, &impl_->dev);
    if (rc < 0) {
        ::close(impl_->fd);
        throw std::system_error(-rc, std::system_category(),
                                "libevdev_new_from_fd failed for " + evdev_path);
    }

    // Deduce model from VID/PID
    auto vid = static_cast<std::uint16_t>(libevdev_get_id_vendor(impl_->dev));
    auto pid = static_cast<std::uint16_t>(libevdev_get_id_product(impl_->dev));
    impl_->model_ = deduce_model(vid, pid);

    // Deduce connection type (heuristic: Bluetooth devices have the uniq field set)
    const char* uniq = libevdev_get_uniq(impl_->dev);
    impl_->conn_ = (uniq && std::strlen(uniq) > 0)
                        ? ConnectionType::Bluetooth
                        : ConnectionType::USB;

    // Grab exclusive access so that the raw device doesn't also produce events
    if (grab_exclusive) {
        if (::ioctl(impl_->fd, EVIOCGRAB, 1) < 0) {
            throw std::system_error(errno, std::system_category(),
                                    "EVIOCGRAB failed for " + evdev_path);
        }
        impl_->grabbed = true;
    }

    // Eagerly open hidraw to hold it from other apps (Steam uses hidraw)
    impl_->hidraw_fd_ = open_hidraw();
    if (impl_->hidraw_fd_ >= 0) {
        std::cout << "[ds4linux] Acquired hidraw for " << evdev_path << "\n";
    }
}

InputDevice::~InputDevice() = default;
InputDevice::InputDevice(InputDevice&&) noexcept = default;
InputDevice& InputDevice::operator=(InputDevice&&) noexcept = default;

// ── Accessors ────────────────────────────────────────────────────────────────

int InputDevice::fd() const noexcept { return impl_->fd; }

std::string InputDevice::name() const {
    const char* n = libevdev_get_name(impl_->dev);
    return n ? n : "Unknown";
}

ControllerModel InputDevice::model() const noexcept { return impl_->model_; }
ConnectionType  InputDevice::connection() const noexcept { return impl_->conn_; }

// ── Event reading (hot path — keep lean) ─────────────────────────────────────

bool InputDevice::read_events(const EventHandler& handler) {
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

// ── hidraw helpers ───────────────────────────────────────────────────────────

int InputDevice::open_hidraw() const {
    // Walk /sys to find the hidraw sibling of our evdev
    // /sys/class/input/eventX/device/ → look for hidraw* in siblings
    // Simplified: scan /dev/hidraw* and match by VID/PID
    for (const auto& entry : fs::directory_iterator("/dev")) {
        auto fname = entry.path().filename().string();
        if (fname.rfind("hidraw", 0) != 0) continue;

        int hfd = ::open(entry.path().c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (hfd < 0) continue;

        struct hidraw_devinfo info{};
        if (::ioctl(hfd, HIDIOCGRAWINFO, &info) < 0) {
            ::close(hfd);
            continue;
        }

        auto vid = static_cast<std::uint16_t>(info.vendor);
        auto pid = static_cast<std::uint16_t>(info.product);
        if (deduce_model(vid, pid) == impl_->model_) {
            ::fchmod(hfd, 0600);
            std::cout << "[ds4linux] Locked " << entry.path()
                      << " permissions to 0600 (root-only)\n";
            return hfd; // caller owns this fd
        }
        ::close(hfd);
    }
    return -1;
}

bool InputDevice::set_lightbar(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (impl_->hidraw_fd_ < 0) {
        impl_->hidraw_fd_ = open_hidraw();
    }
    if (impl_->hidraw_fd_ < 0) return false;

    if (impl_->model_ == ControllerModel::DualSense ||
        impl_->model_ == ControllerModel::DualSenseEdge) {
        // DualSense USB output report 0x02, 48 bytes
        std::uint8_t buf[48]{};
        buf[0] = 0x02;
        buf[1] = 0x04; // flags: lightbar
        buf[2] = 0x00;
        buf[45] = r;
        buf[46] = g;
        buf[47] = b;
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    } else {
        // DS4 USB output report 0x05
        std::uint8_t buf[32]{};
        buf[0] = 0x05;
        buf[1] = 0xFF;
        buf[2] = 0x04; // flags: lightbar
        buf[6] = r;
        buf[7] = g;
        buf[8] = b;
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    }
}

bool InputDevice::set_rumble(std::uint8_t heavy, std::uint8_t light) {
    if (impl_->hidraw_fd_ < 0) {
        impl_->hidraw_fd_ = open_hidraw();
    }
    if (impl_->hidraw_fd_ < 0) return false;

    if (impl_->model_ == ControllerModel::DualSense ||
        impl_->model_ == ControllerModel::DualSenseEdge) {
        // DualSense USB output report 0x02, 48 bytes
        // Byte 0: report ID (0x02)
        // Byte 1: valid_flag0 — 0x01 = haptics enable, 0x02 = haptics disable
        // Byte 2: valid_flag1
        // Byte 3: right motor (light/weak)
        // Byte 4: left motor (heavy/strong)
        std::uint8_t buf[48]{};
        buf[0] = 0x02;
        buf[1] = 0x01 | 0x02; // enable haptics + motor control
        buf[2] = 0x00;
        buf[3] = light;
        buf[4] = heavy;
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    } else {
        // DS4 USB output report 0x05
        std::uint8_t buf[32]{};
        buf[0] = 0x05;
        buf[1] = 0xFF;
        buf[2] = 0x01; // flags: rumble
        buf[4] = light;
        buf[5] = heavy;
        return ::write(impl_->hidraw_fd_, buf, sizeof(buf)) >= 0;
    }
}

int InputDevice::battery_percent() const {
    // Try reading from power_supply sysfs
    // /sys/class/power_supply/sony_controller_battery_*/capacity
    for (const auto& entry : fs::directory_iterator("/sys/class/power_supply")) {
        auto name = entry.path().filename().string();
        if (name.find("sony") == std::string::npos &&
            name.find("ps-controller") == std::string::npos) continue;

        auto cap_path = entry.path() / "capacity";
        if (!fs::exists(cap_path)) continue;

        std::ifstream ifs(cap_path);
        int pct = -1;
        if (ifs >> pct) return pct;
    }
    return -1;
}

} // namespace ds4linux::daemon

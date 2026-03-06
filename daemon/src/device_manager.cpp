// ds4linux::daemon — DeviceManager implementation

#include "ds4linux/device_manager.h"
#include "ds4linux/constants.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <libevdev/libevdev.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

DeviceManager::DeviceManager()  = default;
DeviceManager::~DeviceManager() = default;

// ── Capability helpers ───────────────────────────────────────────────────────

static auto test_bit = [](const unsigned long* bits, unsigned int bit) -> bool {
    return (bits[bit / (8 * sizeof(unsigned long))] >>
            (bit % (8 * sizeof(unsigned long)))) & 1;
};

/// Check whether the evdev node is the *main gamepad* interface.
static bool is_gamepad_node(int fd) {
    unsigned long evbits[((EV_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                          (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return false;
    if (!test_bit(evbits, EV_KEY) || !test_bit(evbits, EV_ABS))
        return false;

    unsigned long keybits[((KEY_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                           (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
        return false;
    if (!test_bit(keybits, BTN_SOUTH) && !test_bit(keybits, BTN_GAMEPAD))
        return false;

    unsigned long absbits[((ABS_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                           (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0)
        return false;
    if (!test_bit(absbits, ABS_X))
        return false;

    return true;
}

/// Check if this is a touchpad node (has ABS_MT_POSITION_X + BTN_TOUCH).
static bool is_touchpad_node(int fd) {
    unsigned long evbits[((EV_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                          (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return false;
    if (!test_bit(evbits, EV_ABS) || !test_bit(evbits, EV_KEY))
        return false;

    unsigned long absbits[((ABS_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                           (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0)
        return false;
    if (!test_bit(absbits, ABS_MT_POSITION_X))
        return false;

    unsigned long keybits[((KEY_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                           (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0)
        return false;
    if (!test_bit(keybits, BTN_TOUCH))
        return false;

    return true;
}

/// Check if this is a motion sensor node (has ABS_X/Y/Z + ABS_RX/RY/RZ but no keys).
static bool is_motion_node(int fd) {
    unsigned long evbits[((EV_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                          (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return false;
    if (!test_bit(evbits, EV_ABS))
        return false;
    // Motion sensor nodes have NO EV_KEY
    if (test_bit(evbits, EV_KEY))
        return false;

    unsigned long absbits[((ABS_MAX + 1) + 8 * sizeof(unsigned long) - 1) /
                           (8 * sizeof(unsigned long))] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0)
        return false;

    // Must have all 6 axes: X, Y, Z, RX, RY, RZ
    return test_bit(absbits, ABS_X) && test_bit(absbits, ABS_Y) &&
           test_bit(absbits, ABS_Z) && test_bit(absbits, ABS_RX) &&
           test_bit(absbits, ABS_RY) && test_bit(absbits, ABS_RZ);
}

/// Get the physical device path from sysfs for an event node.
/// e.g. /dev/input/event19 → /sys/class/input/event19/device/device
/// Returns the realpath of the parent "device" symlink so we can find siblings.
static std::string get_physical_id(const std::string& evdev_path) {
    // Extract eventN from path
    auto fname = fs::path(evdev_path).filename().string();
    fs::path sysfs = fs::path("/sys/class/input") / fname / "device" / "device";
    try {
        return fs::canonical(sysfs).string();
    } catch (...) {
        return {};
    }
}

// ── Scanning ─────────────────────────────────────────────────────────────────

void DeviceManager::scan() {
    std::cout << "[ds4linux] Scanning /dev/input/ for Sony controllers...\n";

    // First pass: find all Sony event nodes and classify them
    struct NodeInfo {
        std::string path;
        std::string physical_id;
        enum Type { Gamepad, Touchpad, Motion, Other } type;
    };
    std::vector<NodeInfo> nodes;

    for (const auto& entry : fs::directory_iterator("/dev/input")) {
        auto fname = entry.path().filename().string();
        if (fname.rfind("event", 0) != 0) continue;

        int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES) {
                static bool perm_warned = false;
                if (!perm_warned) {
                    std::cerr << "[ds4linux] Permission denied on "
                              << entry.path()
                              << " — is the daemon running as root?\n";
                    perm_warned = true;
                }
            }
            continue;
        }

        struct input_id id{};
        if (::ioctl(fd, EVIOCGID, &id) < 0) {
            ::close(fd);
            continue;
        }

        bool vid_match = (id.vendor == kVidSony);
        bool pid_match = (id.product == kPidDS4v1 || id.product == kPidDS4v2 ||
                          id.product == kPidDualSense || id.product == kPidDualSenseEdge);

        if (vid_match && pid_match) {
            char name_buf[256] = {};
            ::ioctl(fd, EVIOCGNAME(sizeof(name_buf)), name_buf);

            // Skip our own virtual devices using the custom phys string
            char phys_buf[256] = {};
            if (::ioctl(fd, EVIOCGPHYS(sizeof(phys_buf)), phys_buf) >= 0) {
                std::string dev_phys(phys_buf);
                if (dev_phys == "ds4linux/virtual") {
                    ::close(fd);
                    continue;
                }
            }

            NodeInfo::Type type = NodeInfo::Other;
            if (is_gamepad_node(fd))       type = NodeInfo::Gamepad;
            else if (is_touchpad_node(fd)) type = NodeInfo::Touchpad;
            else if (is_motion_node(fd))   type = NodeInfo::Motion;

            auto phys_id = get_physical_id(entry.path().string());

            const char* type_str = "other";
            if (type == NodeInfo::Gamepad)  type_str = "gamepad";
            if (type == NodeInfo::Touchpad) type_str = "touchpad";
            if (type == NodeInfo::Motion)   type_str = "motion";

            std::cout << "[ds4linux] Found " << type_str << ": "
                      << entry.path() << " (" << name_buf << ")"
                      << " phys=" << phys_id << "\n";

            nodes.push_back({entry.path().string(), phys_id, type});
        }

        ::close(fd);
    }

    // Second pass: for each gamepad node, find its sibling touchpad & motion
    int opened = 0;
    for (const auto& gp : nodes) {
        if (gp.type != NodeInfo::Gamepad) continue;
        if (slots_.count(gp.path)) continue; // already open

        std::string tp_path, mo_path;
        for (const auto& n : nodes) {
            if (n.physical_id == gp.physical_id && n.path != gp.path) {
                if (n.type == NodeInfo::Touchpad && tp_path.empty()) tp_path = n.path;
                if (n.type == NodeInfo::Motion   && mo_path.empty()) mo_path = n.path;
            }
        }

        std::cout << "[ds4linux] Opening controller group:\n"
                  << "  gamepad:  " << gp.path << "\n"
                  << "  touchpad: " << (tp_path.empty() ? "(not found)" : tp_path) << "\n"
                  << "  motion:   " << (mo_path.empty() ? "(not found)" : mo_path) << "\n";

        open_slot(gp.path);

        // Attach touchpad & motion to the slot
        auto it = slots_.find(gp.path);
        if (it != slots_.end()) {
            it->second.touchpad_path = tp_path;
            it->second.motion_path   = mo_path;

            if (!tp_path.empty()) {
                try {
                    it->second.physical_touchpad =
                        std::make_unique<InputDevice>(tp_path, /*grab=*/true);
                    it->second.virtual_touchpad =
                        std::make_unique<VirtualTouchpad>();
                    std::cout << "[ds4linux] Touchpad active: " << tp_path << "\n";
                } catch (const std::exception& ex) {
                    std::cerr << "[ds4linux] Could not open touchpad "
                              << tp_path << ": " << ex.what() << "\n";
                }
            }

            if (!mo_path.empty()) {
                try {
                    it->second.physical_motion =
                        std::make_unique<InputDevice>(mo_path, /*grab=*/true);
                    it->second.virtual_motion =
                        std::make_unique<VirtualMotion>();
                    std::cout << "[ds4linux] Motion sensors active: " << mo_path << "\n";
                } catch (const std::exception& ex) {
                    std::cerr << "[ds4linux] Could not open motion "
                              << mo_path << ": " << ex.what() << "\n";
                }
            }

            ++opened;
        }
    }

    std::cout << "[ds4linux] Scan complete — " << opened
              << " controller(s) found, " << slots_.size()
              << " slot(s) opened.\n";
}

// ── Hotplug ──────────────────────────────────────────────────────────────────

void DeviceManager::on_device_added(const std::string& evdev_path) {
    if (slots_.count(evdev_path)) return;
    open_slot(evdev_path);
}

void DeviceManager::on_device_removed(const std::string& evdev_path) {
    close_slot(evdev_path);
}

// ── Translate loop ───────────────────────────────────────────────────────────

void DeviceManager::translate_events() {
    // The translation strategy for the UHID path:
    //   1. Read evdev events from the physical DualSense (hid-playstation driver).
    //   2. Accumulate them into the per-slot DS4InputState.
    //   3. On SYN_REPORT, pack the state into a DS4 HID input report and
    //      dispatch it to the kernel via UHID_INPUT2.
    //
    // Physical DualSense evdev codes (hid-playstation):
    //   BTN_SOUTH=Cross, BTN_EAST=Circle, BTN_NORTH=Triangle, BTN_WEST=Square
    //   BTN_TL=L1, BTN_TR=R1, BTN_TL2=L2btn, BTN_TR2=R2btn
    //   BTN_SELECT=Share/Create, BTN_START=Options
    //   BTN_THUMBL=L3, BTN_THUMBR=R3, BTN_MODE=PS
    //   ABS_X=LX, ABS_Y=LY, ABS_RX=RX, ABS_RY=RY
    //   ABS_Z=L2 (analog), ABS_RZ=R2 (analog)
    //   ABS_HAT0X=D-pad X, ABS_HAT0Y=D-pad Y

    for (auto& [path, slot] : slots_) {
        // ── Gamepad events (physical → DS4 HID report) ──────────────────
        if (slot.physical && slot.virtual_dev) {
            bool ok = slot.physical->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    auto& s = slot.ds4_state;

                    if (type == EV_ABS) {
                        switch (code) {
                        case ABS_X:     s.lx = static_cast<std::uint8_t>(value); break;
                        case ABS_Y:     s.ly = static_cast<std::uint8_t>(value); break;
                        case ABS_RX:    s.rx = static_cast<std::uint8_t>(value); break;
                        case ABS_RY:    s.ry = static_cast<std::uint8_t>(value); break;
                        case ABS_Z:     s.l2 = static_cast<std::uint8_t>(value); break;
                        case ABS_RZ:    s.r2 = static_cast<std::uint8_t>(value); break;
                        case ABS_HAT0X: s.hat_x = value; break;
                        case ABS_HAT0Y: s.hat_y = value; break;
                        default: break;
                        }
                    } else if (type == EV_KEY) {
                        bool pressed = (value != 0);
                        switch (code) {
                        case BTN_WEST:   s.square   = pressed; break; // Square
                        case BTN_SOUTH:  s.cross    = pressed; break; // Cross
                        case BTN_EAST:   s.circle   = pressed; break; // Circle
                        case BTN_NORTH:  s.triangle = pressed; break; // Triangle
                        case BTN_TL:     s.l1       = pressed; break; // L1
                        case BTN_TR:     s.r1       = pressed; break; // R1
                        case BTN_TL2:    s.l2_btn   = pressed; break; // L2 digital
                        case BTN_TR2:    s.r2_btn   = pressed; break; // R2 digital
                        case BTN_SELECT: s.share    = pressed; break; // Share/Create
                        case BTN_START:  s.options  = pressed; break; // Options
                        case BTN_THUMBL: s.l3       = pressed; break; // L3
                        case BTN_THUMBR: s.r3       = pressed; break; // R3
                        case BTN_MODE:   s.ps       = pressed; break; // PS
                        default: break;
                        }
                    } else if (type == EV_SYN && code == SYN_REPORT) {
                        // Flush: pack accumulated state and send HID report
                        slot.virtual_dev->send_report(s);
                    }
                }
            );
            if (!ok) {
                std::cerr << "[ds4linux] Device disconnected: " << path << "\n";
            }
        }

        // ── UHID output events (rumble/LED from applications → physical) ─
        if (slot.virtual_dev && slot.physical) {
            auto out = slot.virtual_dev->process_output();
            if (out.heavy >= 0) {
                slot.physical->set_rumble(
                    static_cast<std::uint8_t>(out.heavy),
                    static_cast<std::uint8_t>(out.light));
            }
            if (out.led_changed) {
                slot.physical->set_lightbar(out.led_r, out.led_g, out.led_b);
            }
        }

        // ── Touchpad events (pass through directly via uinput) ──────────
        if (slot.physical_touchpad && slot.virtual_touchpad) {
            slot.physical_touchpad->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    if (type == EV_SYN) {
                        slot.virtual_touchpad->syn();
                    } else {
                        slot.virtual_touchpad->emit(type, code, value);
                    }
                }
            );
        }

        // ── Motion events (pass through directly via uinput) ────────────
        if (slot.physical_motion && slot.virtual_motion) {
            slot.physical_motion->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    if (type == EV_SYN) {
                        slot.virtual_motion->syn();
                    } else {
                        slot.virtual_motion->emit(type, code, value);
                    }
                }
            );
        }
    }
}

// ── Profile ──────────────────────────────────────────────────────────────────

void DeviceManager::set_profile(const std::string& evdev_path, const Profile& p) {
    auto it = slots_.find(evdev_path);
    if (it == slots_.end()) return;
    it->second.profile = p;

    // Apply lightbar immediately
    it->second.physical->set_lightbar(p.lightbar_color.r,
                                      p.lightbar_color.g,
                                      p.lightbar_color.b);
}

// ── Internal ─────────────────────────────────────────────────────────────────

void DeviceManager::open_slot(const std::string& evdev_path) {
    try {
        ControllerSlot slot;
        slot.evdev_path = evdev_path;
        slot.physical   = std::make_unique<InputDevice>(evdev_path, /*grab=*/true);
        slot.virtual_dev = std::make_unique<VirtualDevice>();

        std::cout << "[ds4linux] Opened controller: "
                  << slot.physical->name()
                  << " @ " << evdev_path << "\n";

        // Apply default lightbar color
        slot.physical->set_lightbar(slot.profile.lightbar_color.r,
                                    slot.profile.lightbar_color.g,
                                    slot.profile.lightbar_color.b);

        slots_.emplace(evdev_path, std::move(slot));
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] Could not open " << evdev_path << ": "
                  << ex.what() << "\n";
    }
}

void DeviceManager::close_slot(const std::string& evdev_path) {
    slots_.erase(evdev_path);
}

} // namespace ds4linux::daemon

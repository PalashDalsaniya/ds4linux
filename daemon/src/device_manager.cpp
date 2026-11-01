// ds4linux::daemon — DeviceManager implementation

#include "ds4linux/device_manager.h"
#include "ds4linux/constants.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>

#include <libevdev/libevdev.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

DeviceManager::DeviceManager(DeviceManagerOptions options)
    : options_(std::move(options)) {}
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
/// Empty for devices without a parent (uinput-created virtual devices).
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

/// True if the evdev node belongs to one of our own UHID virtual DS4s.
/// hid-sony copies the UHID phys string onto its input nodes, but
/// hid-playstation (kernel 6.2+) leaves phys empty, so also match on the HID
/// device living under the uhid misc device with a USB bus type.  Bluetooth
/// controllers handled by bluetoothd also sit under uhid, but with BUS_BLUETOOTH.
static bool is_own_virtual_node(int fd, const struct input_id& id,
                                const std::string& physical_id) {
    char phys_buf[256] = {};
    if (::ioctl(fd, EVIOCGPHYS(sizeof(phys_buf)), phys_buf) >= 0 &&
        std::string_view(phys_buf).starts_with(kVirtualDS4Phys)) {
        return true;
    }
    return id.bustype == BUS_USB &&
           physical_id.starts_with("/sys/devices/virtual/misc/uhid/");
}

static bool is_supported_sony(const struct input_id& id) {
    return id.vendor == kVidSony &&
           (id.product == kPidDS4v1 || id.product == kPidDS4v2 ||
            id.product == kPidDualSense || id.product == kPidDualSenseEdge);
}

bool DeviceManager::generic_allowed(std::uint16_t vid, std::uint16_t pid) const {
    if (!options_.generic_enabled) return false;
    if (options_.generic_ids.empty()) return true;
    return std::any_of(options_.generic_ids.begin(), options_.generic_ids.end(),
                       [&](const auto& p) { return p.first == vid && p.second == pid; });
}

// ── Virtual MAC allocation ───────────────────────────────────────────────────
// hid-playstation rejects a device whose MAC is already registered (including
// the physical controller's own), so each virtual DS4 needs its own address.
// It is derived from a stable seed (the physical MAC / sysfs path) so games
// and Steam see the same virtual controller across reconnects.

static std::array<std::uint8_t, 6> parse_mac(const std::string& s) {
    std::array<std::uint8_t, 6> m{};
    unsigned v[6];
    if (std::sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
                    &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        for (int i = 0; i < 6; ++i) m[i] = static_cast<std::uint8_t>(v[i]);
    }
    return m;
}

std::array<std::uint8_t, 6>
DeviceManager::allocate_virtual_mac(const std::string& seed,
                                    const std::string& physical_uniq) const {
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : seed) { h ^= c; h *= 1099511628211ull; }

    std::array<std::uint8_t, 6> mac{0x30, 0x0E, 0xD5,  // Sony OUI (real DS4s)
                                    static_cast<std::uint8_t>(h >> 16),
                                    static_cast<std::uint8_t>(h >> 8),
                                    static_cast<std::uint8_t>(h)};

    std::set<std::array<std::uint8_t, 6>> taken;
    for (const auto& [path, slot] : slots_) taken.insert(slot.virtual_mac);
    taken.insert(parse_mac(physical_uniq));

    while (taken.count(mac)) {
        if (++mac[5] == 0) ++mac[4];
    }
    return mac;
}

// ── Scanning ─────────────────────────────────────────────────────────────────

void DeviceManager::scan() {
    std::cout << "[ds4linux] Scanning /dev/input/ for controllers...\n";

    // First pass: find all supported event nodes and classify them
    struct NodeInfo {
        std::string path;
        std::string physical_id;
        enum Type { Gamepad, Touchpad, Motion, Other } type;
    };
    std::vector<NodeInfo> nodes;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev/input", ec)) {
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

        const bool sony    = is_supported_sony(id);
        const bool generic = !sony && generic_allowed(id.vendor, id.product);
        if (!sony && !generic) {
            ::close(fd);
            continue;
        }

        auto phys_id = get_physical_id(entry.path().string());

        // Skip our own virtual devices (gamepad, touchpad and motion nodes
        // the kernel DS4 driver creates for the UHID device).  For generic
        // pads also skip software devices — uinput pads from Steam Input and
        // uhid pads from other remappers — which would otherwise feed back.
        // Bluetooth pads may legitimately live under uhid (bluetoothd).
        const bool software_device =
            phys_id.empty() ||
            (phys_id.starts_with("/sys/devices/virtual/") && id.bustype != BUS_BLUETOOTH);
        if (is_own_virtual_node(fd, id, phys_id) || (generic && software_device)) {
            ::close(fd);
            continue;
        }

        NodeInfo::Type type = NodeInfo::Other;
        if (is_gamepad_node(fd))       type = NodeInfo::Gamepad;
        else if (is_touchpad_node(fd)) type = NodeInfo::Touchpad;
        else if (is_motion_node(fd))   type = NodeInfo::Motion;

        if (type != NodeInfo::Other) {
            char name_buf[256] = {};
            ::ioctl(fd, EVIOCGNAME(sizeof(name_buf)), name_buf);

            const char* type_str = "gamepad";
            if (type == NodeInfo::Touchpad) type_str = "touchpad";
            if (type == NodeInfo::Motion)   type_str = "motion";

            std::cout << "[ds4linux] Found " << (generic ? "generic " : "")
                      << type_str << ": " << entry.path() << " (" << name_buf << ")"
                      << " phys=" << phys_id << "\n";

            nodes.push_back({entry.path().string(), phys_id, type});
        }

        ::close(fd);
    }

    // Second pass: for each gamepad node, find its sibling touchpad & motion
    int opened = 0;
    for (const auto& gp : nodes) {
        if (gp.type != NodeInfo::Gamepad) continue;

        std::string tp_path, mo_path;
        for (const auto& n : nodes) {
            if (!gp.physical_id.empty() && n.physical_id == gp.physical_id &&
                n.path != gp.path) {
                if (n.type == NodeInfo::Touchpad && tp_path.empty()) tp_path = n.path;
                if (n.type == NodeInfo::Motion   && mo_path.empty()) mo_path = n.path;
            }
        }

        auto it = slots_.find(gp.path);
        if (it == slots_.end()) {
            std::cout << "[ds4linux] Opening controller group:\n"
                      << "  gamepad:  " << gp.path << "\n"
                      << "  touchpad: " << (tp_path.empty() ? "(not found)" : tp_path) << "\n"
                      << "  motion:   " << (mo_path.empty() ? "(not found)" : mo_path) << "\n";

            if (!open_slot(gp.path, gp.physical_id)) continue;
            it = slots_.find(gp.path);
            ++opened;
        }
        auto& slot = it->second;

        // Attach touchpad & motion — also for already-open slots, whose
        // sibling nodes may have appeared after the gamepad node
        if (!tp_path.empty() && !slot.physical_touchpad) {
            try {
                slot.physical_touchpad =
                    std::make_unique<InputDevice>(tp_path, /*grab=*/true);
                slot.touchpad_path = tp_path;
                slot.touch_x = slot.physical_touchpad->abs_range(ABS_MT_POSITION_X);
                slot.touch_y = slot.physical_touchpad->abs_range(ABS_MT_POSITION_Y);
                std::cout << "[ds4linux] Touchpad active: " << tp_path << "\n";
            } catch (const std::exception& ex) {
                std::cerr << "[ds4linux] Could not open touchpad "
                          << tp_path << ": " << ex.what() << "\n";
            }
        }

        if (!mo_path.empty() && !slot.physical_motion) {
            try {
                slot.physical_motion =
                    std::make_unique<InputDevice>(mo_path, /*grab=*/true);
                slot.motion_path = mo_path;
                slot.gyro_res  = slot.physical_motion->abs_range(ABS_RX).resolution;
                slot.accel_res = slot.physical_motion->abs_range(ABS_X).resolution;
                std::cout << "[ds4linux] Motion sensors active: " << mo_path << "\n";
            } catch (const std::exception& ex) {
                std::cerr << "[ds4linux] Could not open motion "
                          << mo_path << ": " << ex.what() << "\n";
            }
        }
    }

    std::cout << "[ds4linux] Scan complete — " << opened
              << " new controller(s), " << slots_.size()
              << " slot(s) open.\n";
}

// ── Hotplug ──────────────────────────────────────────────────────────────────

void DeviceManager::on_device_added(const std::string& evdev_path) {
    if (slots_.count(evdev_path)) return;
    // A full scan applies the same filtering and pairs sibling nodes
    scan();
}

void DeviceManager::on_device_removed(const std::string& evdev_path) {
    close_slot(evdev_path);
}

// ── Normalisation helpers ────────────────────────────────────────────────────

static std::int16_t clamp_i16(std::int64_t v) {
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(v, -32768, 32767));
}

/// Integer division rounding to nearest (half away from zero).
static std::int64_t round_div(std::int64_t num, std::int64_t den) {
    return (num >= 0 ? num + den / 2 : num - den / 2) / den;
}

/// Map a value from an axis range onto 0..255 (DS4 stick / trigger range).
static std::uint8_t to_u8(std::int32_t v, const AbsRange& r) {
    if (!r.present || r.maximum <= r.minimum) {
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    }
    const std::int64_t span = static_cast<std::int64_t>(r.maximum) - r.minimum;
    const std::int64_t n = (static_cast<std::int64_t>(v) - r.minimum) * 255 + span / 2;
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(n / span, 0, 255));
}

/// Rescale a touch coordinate from the physical pad's range to the DS4 pad's.
static std::uint16_t scale_touch(std::int32_t v, const AbsRange& r, int dst_max) {
    std::int64_t scaled = v;
    if (r.present && r.maximum > r.minimum) {
        scaled = (static_cast<std::int64_t>(v) - r.minimum) * dst_max /
                 (static_cast<std::int64_t>(r.maximum) - r.minimum);
    }
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(scaled, 0, dst_max));
}

/// Work out how this gamepad's axes map onto the DS4 report.
static GamepadMap build_gamepad_map(const InputDevice& dev) {
    GamepadMap m;
    m.lx = dev.abs_range(ABS_X);
    m.ly = dev.abs_range(ABS_Y);

    // Right stick is normally RX/RY.  Many HID gamepads (DirectInput mode)
    // report it on Z/RZ instead and have digital-only triggers.
    const bool rxry = dev.has_event_code(EV_ABS, ABS_RX) && dev.has_event_code(EV_ABS, ABS_RY);
    const bool zrz  = dev.has_event_code(EV_ABS, ABS_Z)  && dev.has_event_code(EV_ABS, ABS_RZ);
    const bool z_is_stick = !rxry && zrz;
    m.rx_code = z_is_stick ? ABS_Z  : ABS_RX;
    m.ry_code = z_is_stick ? ABS_RZ : ABS_RY;
    m.rx = dev.abs_range(m.rx_code);
    m.ry = dev.abs_range(m.ry_code);

    auto pick_trigger = [&](std::uint16_t primary, std::uint16_t fallback) {
        if (!z_is_stick && dev.has_event_code(EV_ABS, primary)) return int{primary};
        if (dev.has_event_code(EV_ABS, fallback)) return int{fallback};
        return -1;
    };
    m.l2_code = pick_trigger(ABS_Z,  ABS_BRAKE);
    m.r2_code = pick_trigger(ABS_RZ, ABS_GAS);
    if (m.l2_code >= 0) m.l2 = dev.abs_range(static_cast<std::uint16_t>(m.l2_code));
    if (m.r2_code >= 0) m.r2 = dev.abs_range(static_cast<std::uint16_t>(m.r2_code));

    const bool btn_tl2 = dev.has_event_code(EV_KEY, BTN_TL2);
    const bool btn_tr2 = dev.has_event_code(EV_KEY, BTN_TR2);
    m.synth_l2_btn  = m.l2_code >= 0 && !btn_tl2;
    m.synth_r2_btn  = m.r2_code >= 0 && !btn_tr2;
    m.synth_l2_axis = m.l2_code < 0 && btn_tl2;
    m.synth_r2_axis = m.r2_code < 0 && btn_tr2;
    m.hat_x = dev.abs_range(ABS_HAT0X);
    m.hat_y = dev.abs_range(ABS_HAT0Y);
    return m;
}

/// Map a hat axis value to -1 / 0 / +1 around the centre of its range.
static int hat_direction(std::int32_t v, const AbsRange& r) {
    if (!r.present || r.maximum <= r.minimum) return std::clamp(v, -1, 1);
    const std::int64_t twice = 2 * static_cast<std::int64_t>(v) - r.minimum - r.maximum;
    return twice < 0 ? -1 : twice > 0 ? 1 : 0;
}

// Analog trigger level at which a synthesised L2/R2 button counts as pressed.
static constexpr std::uint8_t kTriggerButtonThreshold = 30;

static void apply_gamepad_event(ControllerSlot& slot, std::uint16_t type,
                                std::uint16_t code, std::int32_t value) {
    auto& s = slot.ds4_state;
    const auto& m = slot.gamepad_map;

    if (type == EV_ABS) {
        if (code == ABS_X)          { s.lx = to_u8(value, m.lx); return; }
        if (code == ABS_Y)          { s.ly = to_u8(value, m.ly); return; }
        if (code == m.rx_code)      { s.rx = to_u8(value, m.rx); return; }
        if (code == m.ry_code)      { s.ry = to_u8(value, m.ry); return; }
        if (code == m.l2_code) {
            s.l2 = to_u8(value, m.l2);
            if (m.synth_l2_btn) s.l2_btn = s.l2 >= kTriggerButtonThreshold;
            return;
        }
        if (code == m.r2_code) {
            s.r2 = to_u8(value, m.r2);
            if (m.synth_r2_btn) s.r2_btn = s.r2 >= kTriggerButtonThreshold;
            return;
        }
        if (code == ABS_HAT0X) { s.hat_x = hat_direction(value, m.hat_x); return; }
        if (code == ABS_HAT0Y) { s.hat_y = hat_direction(value, m.hat_y); return; }
        return;
    }

    if (type != EV_KEY) return;
    const bool pressed = (value != 0);
    auto& d = slot.dpad_btn;
    switch (code) {
    case BTN_WEST:   s.square   = pressed; break; // Square
    case BTN_SOUTH:  s.cross    = pressed; break; // Cross
    case BTN_EAST:   s.circle   = pressed; break; // Circle
    case BTN_NORTH:  s.triangle = pressed; break; // Triangle
    case BTN_TL:     s.l1       = pressed; break; // L1
    case BTN_TR:     s.r1       = pressed; break; // R1
    case BTN_TL2:
        s.l2_btn = pressed;                       // L2 digital
        if (m.synth_l2_axis) s.l2 = pressed ? 255 : 0;
        break;
    case BTN_TR2:
        s.r2_btn = pressed;                       // R2 digital
        if (m.synth_r2_axis) s.r2 = pressed ? 255 : 0;
        break;
    case BTN_SELECT: s.share    = pressed; break; // Share/Create
    case BTN_START:  s.options  = pressed; break; // Options
    case BTN_THUMBL: s.l3       = pressed; break; // L3
    case BTN_THUMBR: s.r3       = pressed; break; // R3
    case BTN_MODE:   s.ps       = pressed; break; // PS
    // D-pad reported as buttons (some generic pads) instead of a hat
    case BTN_DPAD_UP:    d.up    = pressed; s.hat_y = d.down  - d.up;   break;
    case BTN_DPAD_DOWN:  d.down  = pressed; s.hat_y = d.down  - d.up;   break;
    case BTN_DPAD_LEFT:  d.left  = pressed; s.hat_x = d.right - d.left; break;
    case BTN_DPAD_RIGHT: d.right = pressed; s.hat_x = d.right - d.left; break;
    default: break;
    }
}

static void apply_motion_event(ControllerSlot& slot, std::uint16_t type,
                               std::uint16_t code, std::int32_t value) {
    auto& s = slot.ds4_state;
    if (type == EV_MSC && code == MSC_TIMESTAMP) {
        s.sensor_time_us  = static_cast<std::uint32_t>(value);
        s.has_sensor_time = true;
        return;
    }
    if (type != EV_ABS) return;

    // Convert the driver's normalised units (axis resolution = units per
    // deg/s or per g) back to raw DS4 units (see constants.h)
    const int gyro_res  = slot.gyro_res  > 0 ? slot.gyro_res  : kEvdevGyroPerDegS;
    const int accel_res = slot.accel_res > 0 ? slot.accel_res : kDS4AccelPerG;
    auto gyro  = [&](std::int32_t v) {
        return clamp_i16(round_div(std::int64_t{v} * kDS4GyroPerDegS, gyro_res));
    };
    auto accel = [&](std::int32_t v) {
        return clamp_i16(round_div(std::int64_t{v} * kDS4AccelPerG, accel_res));
    };
    switch (code) {
    case ABS_X:  s.accel_x = accel(value); break;
    case ABS_Y:  s.accel_y = accel(value); break;
    case ABS_Z:  s.accel_z = accel(value); break;
    case ABS_RX: s.gyro_x  = gyro(value);  break;
    case ABS_RY: s.gyro_y  = gyro(value);  break;
    case ABS_RZ: s.gyro_z  = gyro(value);  break;
    default: break;
    }
}

/// Refresh both DS4 touch points from libevdev's multitouch slot state.
/// Returns true if a finger was put down or lifted.
static bool refresh_touches(ControllerSlot& slot) {
    bool transition = false;
    for (int i = 0; i < 2; ++i) {
        auto& t = slot.ds4_state.touches[i];
        const MtContact c = slot.physical_touchpad->mt_contact(i);
        const bool active = c.tracking_id >= 0;
        if (active != t.active) transition = true;
        t.active = active;
        if (active) {
            t.id = static_cast<std::uint8_t>(c.tracking_id & 0x7F);
            t.x  = scale_touch(c.x, slot.touch_x, kDS4TouchpadMaxX);
            t.y  = scale_touch(c.y, slot.touch_y, kDS4TouchpadMaxY);
        }
    }
    return transition;
}

static constexpr auto kBatteryPollInterval = std::chrono::seconds(5);

// If the motion node has not synced for this long, gamepad / touch syncs end
// frames instead (e.g. third-party DS4s over Bluetooth that only send minimal
// reports without IMU data, so the motion node exists but stays silent).
static constexpr auto kMotionStallTimeout = std::chrono::milliseconds(20);

// hid-playstation sets a default lightbar colour while probing the virtual
// DS4; ignore LED output this soon after opening so it can't override the
// profile colour already applied to the physical controller.
static constexpr auto kProbeOutputGrace = std::chrono::seconds(2);

// ── Translate loop ───────────────────────────────────────────────────────────

std::vector<std::string> DeviceManager::translate_events() {
    std::vector<std::string> disconnected;
    // The translation strategy for the UHID path:
    //   1. Read evdev events from the physical controller's gamepad, motion
    //      and touchpad nodes (in the order hid-playstation emits them).
    //   2. Accumulate them into the per-slot DS4InputState.
    //   3. Send one DS4 HID report per physical frame.  The motion node
    //      syncs on every controller report (its timestamp always changes),
    //      so while it is live it is the frame clock; otherwise any node's
    //      SYN_REPORT ends a frame.  A finger going down or up is flushed
    //      immediately so quick taps are never merged away.
    //
    // Physical evdev codes (hid-playstation; generic pads follow the same
    // evdev gamepad conventions, with axes normalised by their ranges):
    //   BTN_SOUTH=Cross, BTN_EAST=Circle, BTN_NORTH=Triangle, BTN_WEST=Square
    //   BTN_TL=L1, BTN_TR=R1, BTN_TL2=L2btn, BTN_TR2=R2btn
    //   BTN_SELECT=Share/Create, BTN_START=Options
    //   BTN_THUMBL=L3, BTN_THUMBR=R3, BTN_MODE=PS
    //   ABS_X=LX, ABS_Y=LY, ABS_RX=RX, ABS_RY=RY
    //   ABS_Z=L2 (analog), ABS_RZ=R2 (analog)
    //   ABS_HAT0X=D-pad X, ABS_HAT0Y=D-pad Y

    for (auto& [path, slot] : slots_) {
        if (!slot.physical || !slot.virtual_dev) continue;

        const auto now = std::chrono::steady_clock::now();
        bool frame_end  = false;   // motion node finished a frame
        bool input_sync = false;   // gamepad / touchpad node finished a frame

        // ── Gamepad events (physical → DS4 HID report) ──────────────────
        bool ok = slot.physical->read_events(
            [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                if (type == EV_SYN) {
                    if (code == SYN_REPORT) input_sync = true;
                    return;
                }
                apply_gamepad_event(slot, type, code, value);
            }
        );
        if (!ok) {
            std::cerr << "[ds4linux] Device disconnected: " << path << "\n";
            disconnected.push_back(path);
            continue;
        }

        // ── UHID output (rumble/LED from applications → physical) ───────
        auto out = slot.virtual_dev->take_output();
        if (out.heavy >= 0) {
            slot.physical->set_rumble(
                static_cast<std::uint8_t>(out.heavy),
                static_cast<std::uint8_t>(out.light));
        }
        if (out.led_changed && now - slot.opened_at >= kProbeOutputGrace) {
            slot.physical->set_lightbar(out.led_r, out.led_g, out.led_b);
        }

        // ── Motion events (physical → DS4 report IMU fields) ────────────
        if (slot.physical_motion) {
            slot.physical_motion->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    if (type == EV_SYN) {
                        if (code == SYN_REPORT) {
                            frame_end = true;
                            slot.last_motion_sync = now;
                        }
                        return;
                    }
                    apply_motion_event(slot, type, code, value);
                }
            );
        }

        // ── Touchpad events (physical → DS4 report touch points) ────────
        if (slot.physical_touchpad) {
            slot.physical_touchpad->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    if (type == EV_KEY && code == BTN_LEFT) {
                        // BTN_LEFT on the touchpad node is the touchpad click
                        slot.ds4_state.touchpad = (value != 0);
                    } else if (type == EV_SYN && code == SYN_REPORT) {
                        if (refresh_touches(slot)) {
                            slot.virtual_dev->send_report(slot.ds4_state);
                            frame_end = input_sync = false;
                        } else {
                            input_sync = true;
                        }
                    }
                }
            );
        }

        // ── Battery (polled from sysfs) ─────────────────────────────────
        if (now >= slot.next_battery_poll) {
            slot.next_battery_poll = now + kBatteryPollInterval;
            slot.ds4_state.battery = slot.physical->battery();
        }

        const bool motion_live = slot.physical_motion &&
                                 now - slot.last_motion_sync < kMotionStallTimeout;
        // A stalled sensor clock would freeze the report timestamp
        if (!motion_live) slot.ds4_state.has_sensor_time = false;
        if (frame_end || (input_sync && !motion_live)) {
            slot.virtual_dev->send_report(slot.ds4_state);
        }
    }
    return disconnected;
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

bool DeviceManager::open_slot(const std::string& evdev_path,
                              const std::string& physical_id) {
    try {
        ControllerSlot slot;
        slot.evdev_path  = evdev_path;
        slot.physical_id = physical_id;
        slot.opened_at   = std::chrono::steady_clock::now();
        slot.physical    = std::make_unique<InputDevice>(evdev_path, /*grab=*/true);
        slot.gamepad_map = build_gamepad_map(*slot.physical);

        const std::string uniq = slot.physical->uniq();
        const std::string seed = !uniq.empty()        ? uniq
                               : !physical_id.empty() ? physical_id
                                                      : evdev_path;
        slot.virtual_mac = allocate_virtual_mac(seed, uniq);
        slot.virtual_dev = std::make_unique<VirtualDevice>(slot.virtual_mac);

        std::cout << "[ds4linux] Opened controller: "
                  << slot.physical->name()
                  << " @ " << evdev_path << "\n";

        // Apply default lightbar color
        slot.physical->set_lightbar(slot.profile.lightbar_color.r,
                                    slot.profile.lightbar_color.g,
                                    slot.profile.lightbar_color.b);

        slots_.emplace(evdev_path, std::move(slot));
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] Could not open " << evdev_path << ": "
                  << ex.what() << "\n";
        return false;
    }
}

void DeviceManager::close_slot(const std::string& evdev_path) {
    slots_.erase(evdev_path);
}

} // namespace ds4linux::daemon

// ds4linux::daemon — DeviceManager implementation

#include "ds4linux/device_manager.h"
#include "ds4linux/constants.h"
#include "ds4linux/debug.h"
#include "ds4linux/mapping.h"
#include "ds4linux/virtual_x360.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <utility>

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

// ── Node classification ──────────────────────────────────────────────────────

/// Which part of a controller an evdev node is, from its event capabilities:
/// the gamepad (buttons + sticks), the touchpad (multitouch + BTN_TOUCH) or
/// the motion sensors (accel + gyro axes, no keys).
static DeviceManager::NodeType classify_node(int fd) {
    using T = DeviceManager::NodeType;
    constexpr std::size_t kBits = 8 * sizeof(unsigned long);
    unsigned long ev[(EV_MAX + kBits) / kBits]{};
    unsigned long key[(KEY_MAX + kBits) / kBits]{};
    unsigned long abs[(ABS_MAX + kBits) / kBits]{};
    auto has = [](const unsigned long* bits, unsigned n) {
        return ((bits[n / kBits] >> (n % kBits)) & 1) != 0;
    };

    if (::ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !has(ev, EV_ABS) ||
        ::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0) {
        return T::Other;
    }
    if (!has(ev, EV_KEY)) {
        for (unsigned axis : {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ}) {
            if (!has(abs, axis)) return T::Other;
        }
        return T::Motion;
    }
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0) return T::Other;
    if (has(key, BTN_SOUTH) && has(abs, ABS_X)) return T::Gamepad;
    if (has(key, BTN_TOUCH) && has(abs, ABS_MT_POSITION_X)) return T::Touchpad;
    return T::Other;
}

/// Get the physical device path from sysfs for an event node.
/// e.g. /dev/input/event19 → /sys/class/input/event19/device/device
/// Returns the realpath of the parent "device" symlink so we can find siblings.
/// Empty for devices without a parent (uinput-created virtual devices).
static std::string get_physical_id(const std::string& evdev_path) {
    std::error_code ec;
    auto path = fs::canonical(hid_sysfs_dir(evdev_path), ec);
    return ec ? std::string() : path.string();
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

std::optional<DeviceManager::Node> DeviceManager::probe_node(const std::string& path) const {
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        static bool perm_warned = false;
        if (errno == EACCES && !std::exchange(perm_warned, true)) {
            std::cerr << "[ds4linux] Permission denied on " << path
                      << " — is the daemon running as root?\n";
        }
        return std::nullopt;
    }
    struct Close { int fd; ~Close() { ::close(fd); } } closer{fd};

    struct input_id id{};
    if (::ioctl(fd, EVIOCGID, &id) < 0) return std::nullopt;
    const bool sony    = is_supported_sony(id);
    const bool generic = !sony && generic_allowed(id.vendor, id.product);
    if (!sony && !generic) return std::nullopt;

    Node node{path, get_physical_id(path), {}, NodeType::Other, generic};

    // Skip our own virtual devices (gamepad, touchpad and motion nodes the
    // kernel DS4 driver creates for the UHID device).  For generic pads also
    // skip software devices — uinput pads from Steam Input and uhid pads
    // from other remappers — which would otherwise feed back.  Bluetooth
    // pads may legitimately live under uhid (bluetoothd).
    const bool software_device =
        node.physical_id.empty() ||
        (node.physical_id.starts_with("/sys/devices/virtual/") && id.bustype != BUS_BLUETOOTH);
    if (is_own_virtual_node(fd, id, node.physical_id) || (generic && software_device)) {
        return std::nullopt;
    }

    node.type = classify_node(fd);
    if (node.type == NodeType::Other) return std::nullopt;
    char name[256] = {};
    ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    node.name = name;
    return node;
}

void DeviceManager::scan() {
    std::cout << "[ds4linux] Scanning /dev/input/ for controllers...\n";

    // First pass: find all supported event nodes and classify them
    std::vector<Node> nodes;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/dev/input", ec)) {
        if (!entry.path().filename().string().starts_with("event")) continue;
        auto node = probe_node(entry.path().string());
        if (!node) continue;
        if (!is_open_node(node->path)) {
            static constexpr const char* kTypeNames[] = {"gamepad", "touchpad", "motion"};
            std::cout << "[ds4linux] Found " << (node->generic ? "generic " : "")
                      << kTypeNames[static_cast<int>(node->type)] << ": " << node->path
                      << " (" << node->name << ") phys=" << node->physical_id << "\n";
        }
        nodes.push_back(std::move(*node));
    }

    // Second pass: for each gamepad node, find its sibling touchpad & motion
    int opened = 0;
    for (const auto& gp : nodes) {
        if (gp.type != NodeType::Gamepad) continue;

        std::string tp_path, mo_path;
        for (const auto& n : nodes) {
            if (!gp.physical_id.empty() && n.physical_id == gp.physical_id &&
                n.path != gp.path) {
                if (n.type == NodeType::Touchpad && tp_path.empty()) tp_path = n.path;
                if (n.type == NodeType::Motion   && mo_path.empty()) mo_path = n.path;
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
        auto attach = [&](std::unique_ptr<InputDevice>& dev, std::string& dev_path,
                          const std::string& node, const char* what) {
            if (node.empty() || dev) return false;
            try {
                dev = std::make_unique<InputDevice>(node);
            } catch (const std::exception& ex) {
                std::cerr << "[ds4linux] Could not open " << what << " " << node << ": "
                          << ex.what() << "\n";
                return false;
            }
            dev_path = node;
            fds_changed_ = true;
            std::cout << "[ds4linux] " << what << " active: " << node << "\n";
            return true;
        };
        if (attach(slot.physical_touchpad, slot.touchpad_path, tp_path, "touchpad")) {
            slot.touch_x = slot.physical_touchpad->abs_range(ABS_MT_POSITION_X);
            slot.touch_y = slot.physical_touchpad->abs_range(ABS_MT_POSITION_Y);
        }
        if (attach(slot.physical_motion, slot.motion_path, mo_path, "motion sensors")) {
            slot.gyro_res  = slot.physical_motion->abs_range(ABS_RX).resolution;
            slot.accel_res = slot.physical_motion->abs_range(ABS_X).resolution;
        }
    }

    std::cout << "[ds4linux] Scan complete — " << opened
              << " new controller(s), " << slots_.size()
              << " slot(s) open.\n";
}

// ── Hotplug ──────────────────────────────────────────────────────────────────

bool DeviceManager::is_open_node(const std::string& evdev_path) const {
    for (const auto& [path, slot] : slots_) {
        if (path == evdev_path || slot.touchpad_path == evdev_path ||
            slot.motion_path == evdev_path) {
            return true;
        }
    }
    return false;
}

void DeviceManager::on_device_added(const std::string& evdev_path) {
    // Only a new controller node is worth a rescan; most "add" events are
    // other input devices or the nodes of our own virtual DS4
    if (is_open_node(evdev_path) || !probe_node(evdev_path)) return;
    // A full scan applies the same filtering and pairs sibling nodes
    scan();
}

void DeviceManager::on_device_removed(const std::string& evdev_path) {
    if (slots_.count(evdev_path)) {
        std::cout << "[ds4linux] Controller removed: " << evdev_path << "\n";
        close_slot(evdev_path);
        return;
    }
    // A touchpad or motion node of an open controller
    for (auto& [path, slot] : slots_) {
        if (slot.physical_touchpad && slot.touchpad_path == evdev_path) detach_touchpad(slot);
        if (slot.physical_motion && slot.motion_path == evdev_path) detach_motion(slot);
    }
}

void DeviceManager::detach_touchpad(ControllerSlot& slot) {
    std::cerr << "[ds4linux] Touchpad gone: " << slot.touchpad_path << "\n";
    slot.physical_touchpad.reset();
    slot.touchpad_path.clear();
    for (auto& t : slot.ds4_state.touches) t.active = false;
    slot.ds4_state.touchpad = false;
    fds_changed_ = true;
}

void DeviceManager::detach_motion(ControllerSlot& slot) {
    std::cerr << "[ds4linux] Motion sensors gone: " << slot.motion_path << "\n";
    slot.physical_motion.reset();
    slot.motion_path.clear();
    slot.last_motion_sync = {};
    fds_changed_ = true;
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

// ── Translate loop ───────────────────────────────────────────────────────────

void DeviceManager::translate_events() {
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

    std::vector<std::string> disconnected;  // closed after the loop

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
            std::cerr << "[ds4linux] Controller disconnected: " << path << "\n";
            disconnected.push_back(path);
            continue;
        }

        // ── Virtual output (rumble/LED from applications → physical) ────
        auto out = slot.virtual_dev->take_output();
        if (out.heavy >= 0) {
            slot.game_heavy = out.heavy;
            slot.game_light = out.light;
        }
        apply_rumble(slot, now);
        if (out.led_changed) {
            slot.physical->set_lightbar(out.led_r, out.led_g, out.led_b);
        }

        // ── Motion events (physical → DS4 report IMU fields) ────────────
        if (slot.physical_motion) {
            ok = slot.physical_motion->read_events(
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
            if (!ok) detach_motion(slot);
        }

        // ── Touchpad events (physical → DS4 report touch points) ────────
        if (slot.physical_touchpad) {
            ok = slot.physical_touchpad->read_events(
                [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                    if (type == EV_KEY && code == BTN_LEFT) {
                        // BTN_LEFT on the touchpad node is the touchpad click
                        slot.ds4_state.touchpad = (value != 0);
                    } else if (type == EV_SYN && code == SYN_REPORT) {
                        if (refresh_touches(slot)) {
                            emit(slot);
                            frame_end = input_sync = false;
                        } else {
                            input_sync = true;
                        }
                    }
                }
            );
            if (!ok) detach_touchpad(slot);
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
            emit(slot);
        }
    }

    for (const auto& path : disconnected) close_slot(path);
}

/// Map the raw state through the slot's profile and send it to the output.
void DeviceManager::emit(ControllerSlot& slot) {
    slot.output_state = apply_profile(slot.ds4_state, slot.profile);
    slot.virtual_dev->send_report(slot.output_state);
}

/// Drive the motors with the test rumble while it lasts, otherwise the
/// applications' level scaled by the profile; write only on a change.
void DeviceManager::apply_rumble(ControllerSlot& slot,
                                 std::chrono::steady_clock::time_point now) {
    const double k = slot.profile.rumble_strength;
    const bool testing = now < slot.test_rumble_until;
    const int heavy = testing ? slot.test_heavy : scale_rumble(slot.game_heavy, k);
    const int light = testing ? slot.test_light : scale_rumble(slot.game_light, k);
    if (heavy == slot.sent_heavy && light == slot.sent_light) return;
    if (g_debug) {
        std::fprintf(stderr, "[ds4linux] debug: %s motors heavy=%d light=%d "
                     "(%s, profile \"%s\" strength %.2f)\n",
                     slot.evdev_path.c_str(), heavy, light, testing ? "test" : "app",
                     slot.profile.name.c_str(), k);
    }
    slot.physical->set_rumble(static_cast<std::uint8_t>(heavy),
                              static_cast<std::uint8_t>(light));
    slot.sent_heavy = heavy;
    slot.sent_light = light;
}

// ── Profile ──────────────────────────────────────────────────────────────────

void DeviceManager::create_output(ControllerSlot& slot, OutputMode mode) {
    // Build the new output before dropping the old one, so a failed switch
    // leaves the controller working.  Throws if no output can be created.
    std::unique_ptr<VirtualOutput> out;
    OutputMode created = mode;
    if (mode == OutputMode::X360) {
        try {
            out = std::make_unique<VirtualX360>();
        } catch (const std::exception& ex) {
            std::cerr << "[ds4linux] " << ex.what() << "\n";
            if (slot.virtual_dev) return;  // keep the current output
            std::cerr << "[ds4linux] Falling back to DS4 output\n";
            created = OutputMode::DS4;
        }
    }
    if (!out) {
        try {
            out = std::make_unique<VirtualDevice>(slot.virtual_mac);
        } catch (const std::exception& ex) {
            if (!slot.virtual_dev) throw;
            std::cerr << "[ds4linux] " << ex.what() << " — keeping the current output\n";
            return;
        }
    }
    slot.virtual_dev = std::move(out);
    slot.output_mode = created;
    // The games' rumble belonged to the old device, which can no longer
    // stop it: start the new one silent
    slot.game_heavy = slot.game_light = 0;
    fds_changed_ = true;
}

/// Make `p` the slot's profile: create or switch the virtual output for its
/// output mode (a failed switch keeps the old output; with none yet, it
/// throws), set the lightbar and publish the state through the new
/// mapping.  Rumble scaling applies on the next pass.
void DeviceManager::use_profile(ControllerSlot& slot, const Profile& p) {
    slot.profile = p;
    if (!slot.virtual_dev || p.output_mode != slot.output_mode) {
        create_output(slot, p.output_mode);
    }
    slot.physical->set_lightbar(p.lightbar_color.r, p.lightbar_color.g, p.lightbar_color.b);
    emit(slot);
}

ControllerSlot* DeviceManager::find_slot(const std::string& evdev_path) {
    auto it = slots_.find(evdev_path);
    return it == slots_.end() ? nullptr : &it->second;
}

bool DeviceManager::set_profile(const std::string& evdev_path, const Profile& p) {
    auto* slot = find_slot(evdev_path);
    if (slot) use_profile(*slot, p);
    return slot != nullptr;
}

void DeviceManager::reapply_profile(const std::string& name) {
    if (!resolver_) return;
    for (auto& [path, slot] : slots_) {
        if (slot.profile.name == name) set_profile(path, resolver_(slot.controller_key));
    }
}

bool DeviceManager::set_lightbar(const std::string& evdev_path,
                                 std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto* slot = find_slot(evdev_path);
    if (slot) slot->physical->set_lightbar(r, g, b);
    return slot != nullptr;
}

bool DeviceManager::test_rumble(const std::string& evdev_path, std::uint8_t heavy,
                                std::uint8_t light, std::chrono::milliseconds duration) {
    auto* slot = find_slot(evdev_path);
    if (!slot) return false;
    const auto now = std::chrono::steady_clock::now();
    slot->test_heavy        = heavy;
    slot->test_light        = light;
    slot->test_rumble_until = now + duration;
    apply_rumble(*slot, now);
    return true;
}

std::optional<std::pair<DS4InputState, DS4InputState>>
DeviceManager::input_state(const std::string& evdev_path) const {
    auto it = slots_.find(evdev_path);
    if (it == slots_.end()) return std::nullopt;
    return std::make_pair(it->second.ds4_state, it->second.output_state);
}

static const char* model_name(ControllerModel m) {
    switch (m) {
    case ControllerModel::DS4v1:         return "DualShock 4 (v1)";
    case ControllerModel::DS4v2:         return "DualShock 4 (v2)";
    case ControllerModel::DualSense:     return "DualSense";
    case ControllerModel::DualSenseEdge: return "DualSense Edge";
    case ControllerModel::Generic:       return "Generic gamepad";
    }
    return "Unknown";
}

std::vector<DeviceInfo> DeviceManager::device_info() const {
    std::vector<DeviceInfo> out;
    for (const auto& [path, slot] : slots_) {
        DeviceInfo d;
        d.path       = path;
        d.name       = slot.physical->name();
        d.model      = model_name(slot.physical->model());
        d.connection = slot.physical->connection() == ConnectionType::Bluetooth
                           ? "bluetooth" : "usb";
        char mac[18];
        std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                      slot.virtual_mac[0], slot.virtual_mac[1], slot.virtual_mac[2],
                      slot.virtual_mac[3], slot.virtual_mac[4], slot.virtual_mac[5]);
        d.mac            = mac;
        d.controller_key = slot.controller_key;
        d.profile        = slot.profile.name;
        d.output_mode    = slot.output_mode;
        d.battery        = slot.ds4_state.battery;
        d.has_touchpad   = static_cast<bool>(slot.physical_touchpad);
        d.has_motion     = static_cast<bool>(slot.physical_motion);
        out.push_back(std::move(d));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    return out;
}

std::vector<int> DeviceManager::poll_fds() const {
    std::vector<int> fds;
    for (const auto& [path, slot] : slots_) {
        if (slot.physical)          fds.push_back(slot.physical->fd());
        if (slot.physical_touchpad) fds.push_back(slot.physical_touchpad->fd());
        if (slot.physical_motion)   fds.push_back(slot.physical_motion->fd());
        if (slot.virtual_dev)       fds.push_back(slot.virtual_dev->output_fd());
    }
    return fds;
}

bool DeviceManager::take_fds_changed() noexcept {
    return std::exchange(fds_changed_, false);
}

// ── Internal ─────────────────────────────────────────────────────────────────

bool DeviceManager::open_slot(const std::string& evdev_path,
                              const std::string& physical_id) {
    try {
        ControllerSlot slot;
        slot.evdev_path  = evdev_path;
        slot.physical    = std::make_unique<InputDevice>(evdev_path);
        slot.gamepad_map = build_gamepad_map(*slot.physical);
        slot.physical->replay_current_state(
            [&](std::uint16_t type, std::uint16_t code, std::int32_t value) {
                apply_gamepad_event(slot, type, code, value);
            });

        const std::string uniq = slot.physical->uniq();
        slot.controller_key = !uniq.empty()        ? uniq
                            : !physical_id.empty() ? physical_id
                                                   : evdev_path;
        slot.virtual_mac = allocate_virtual_mac(slot.controller_key, uniq);
        // Creates the virtual output and publishes the seeded state (held
        // buttons, deflected sticks)
        use_profile(slot, resolver_ ? resolver_(slot.controller_key) : Profile{});

        std::cout << "[ds4linux] Opened controller: "
                  << slot.physical->name()
                  << " @ " << evdev_path
                  << " (profile \"" << slot.profile.name << "\")\n";

        // Games and Steam must only see the virtual controller: hide every
        // node of the physical one (evdev and hidraw, also ones added later)
        slot.hidden.hide(physical_id);

        slots_.emplace(evdev_path, std::move(slot));
        fds_changed_ = true;
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] Could not open " << evdev_path << ": "
                  << ex.what() << "\n";
        return false;
    }
}

void DeviceManager::close_slot(const std::string& evdev_path) {
    if (slots_.erase(evdev_path)) fds_changed_ = true;
}

} // namespace ds4linux::daemon

// ds4linux::daemon — VirtualX360 (uinput Xbox 360 controller)

#include "ds4linux/virtual_x360.h"
#include "ds4linux/debug.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
#include <unordered_map>

namespace ds4linux::daemon {

// Identity of a wired Xbox 360 controller as created by the xpad driver.
static constexpr std::uint16_t kVidMicrosoft = 0x045E;
static constexpr std::uint16_t kPidX360      = 0x028E;
static constexpr std::uint16_t kX360Version  = 0x0110;
static constexpr int           kMaxFfEffects = 16;
static constexpr std::size_t   kReportEvents = 19;  // buttons + axes per report

using Clock = std::chrono::steady_clock;

struct VirtualX360::Impl {
    int fd = -1;

    struct Effect {
        std::uint16_t strong = 0, weak = 0;  // FF_RUMBLE magnitudes
        std::uint16_t length_ms = 0;         // 0 = until stopped
        Clock::time_point stop_at{};         // playing while now < stop_at
    };
    std::unordered_map<int, Effect> effects;
    int last_heavy = 0, last_light = 0;

    std::array<input_event, kReportEvents> sent{};  // last values written
    bool sent_valid = false;

    ~Impl() {
        if (fd >= 0) {
            ::ioctl(fd, UI_DEV_DESTROY);
            ::close(fd);
        }
    }

    void handle_ff_upload(std::int32_t request_id) {
        struct uinput_ff_upload up{};
        up.request_id = static_cast<std::uint32_t>(request_id);
        if (::ioctl(fd, UI_BEGIN_FF_UPLOAD, &up) < 0) return;
        if (up.effect.type == FF_RUMBLE) {
            auto& e = effects[up.effect.id];
            e.strong    = up.effect.u.rumble.strong_magnitude;
            e.weak      = up.effect.u.rumble.weak_magnitude;
            e.length_ms = up.effect.replay.length;
            up.retval = 0;
            if (g_debug) {
                std::fprintf(stderr, "[ds4linux] debug: x360 FF upload id=%d strong=%u "
                             "weak=%u length=%ums\n", up.effect.id, e.strong, e.weak,
                             unsigned{e.length_ms});
            }
        } else {
            up.retval = -EINVAL;
        }
        // The application's EVIOCSFF blocks until this completes
        ::ioctl(fd, UI_END_FF_UPLOAD, &up);
    }

    void handle_ff_erase(std::int32_t request_id) {
        struct uinput_ff_erase er{};
        er.request_id = static_cast<std::uint32_t>(request_id);
        if (::ioctl(fd, UI_BEGIN_FF_ERASE, &er) < 0) return;
        effects.erase(static_cast<int>(er.effect_id));
        er.retval = 0;
        ::ioctl(fd, UI_END_FF_ERASE, &er);
    }

    void handle_ff_play(int effect_id, std::int32_t value, Clock::time_point now) {
        auto it = effects.find(effect_id);
        if (g_debug) {
            std::fprintf(stderr, "[ds4linux] debug: x360 FF play id=%d value=%d%s\n",
                         effect_id, value, it == effects.end() ? " (unknown effect)" : "");
        }
        if (it == effects.end()) return;
        auto& e = it->second;
        if (value <= 0) {
            e.stop_at = {};  // stopped
        } else if (e.length_ms == 0) {
            e.stop_at = Clock::time_point::max();  // until stopped
        } else {
            // `value` is a repeat count; play `length` that many times
            // (capped at an hour so the deadline cannot overflow)
            const std::int64_t ms = std::min<std::int64_t>(
                std::int64_t{e.length_ms} * value, 3'600'000);
            e.stop_at = now + std::chrono::milliseconds(ms);
        }
    }
};

VirtualX360::VirtualX360()
    : impl_(std::make_unique<Impl>())
{
    impl_->fd = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (impl_->fd < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to open /dev/uinput");
    }
    const int fd = impl_->fd;

    ::ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int key : {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT,
                    BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR}) {
        ::ioctl(fd, UI_SET_KEYBIT, key);
    }

    ::ioctl(fd, UI_SET_EVBIT, EV_ABS);
    auto setup_abs = [&](std::uint16_t code, int min, int max, int fuzz, int flat) {
        struct uinput_abs_setup abs{};
        abs.code = code;
        abs.absinfo.minimum = min;
        abs.absinfo.maximum = max;
        abs.absinfo.fuzz    = fuzz;
        abs.absinfo.flat    = flat;
        ::ioctl(fd, UI_ABS_SETUP, &abs);
    };
    for (std::uint16_t axis : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) {
        setup_abs(axis, -32768, 32767, 16, 128);
    }
    setup_abs(ABS_Z,  0, 255, 0, 0);
    setup_abs(ABS_RZ, 0, 255, 0, 0);
    setup_abs(ABS_HAT0X, -1, 1, 0, 0);
    setup_abs(ABS_HAT0Y, -1, 1, 0, 0);

    ::ioctl(fd, UI_SET_EVBIT, EV_FF);
    ::ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    ::ioctl(fd, UI_SET_PHYS, "ds4linux/x360");

    struct uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor  = kVidMicrosoft;
    setup.id.product = kPidX360;
    setup.id.version = kX360Version;
    std::strncpy(setup.name, "Microsoft X-Box 360 pad", UINPUT_MAX_NAME_SIZE - 1);
    setup.ff_effects_max = kMaxFfEffects;

    if (::ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ::ioctl(fd, UI_DEV_CREATE) < 0) {
        throw std::system_error(errno, std::system_category(),
                                "Failed to create virtual Xbox 360 controller");
    }
    std::cout << "[ds4linux] Virtual Xbox 360 controller created via uinput\n";
}

VirtualX360::~VirtualX360() = default;

int VirtualX360::output_fd() const noexcept { return impl_->fd; }

/// DS4 stick byte (0..255, 0x80 centre, 0 = left/up) → xpad axis value.
/// xpad reports up as negative, matching the DS4's orientation.
static std::int32_t stick_to_x360(std::uint8_t v) {
    return v >= 0x80 ? (v - 0x80) * 32767 / 127 : (v - 0x80) * 256;
}

void VirtualX360::send_report(const DS4InputState& s) {
    // (type, code, value) of every control, in a fixed order
    const std::array<input_event, kReportEvents> now_state = {{
        {{}, EV_KEY, BTN_A,      s.cross},
        {{}, EV_KEY, BTN_B,      s.circle},
        {{}, EV_KEY, BTN_X,      s.square},
        {{}, EV_KEY, BTN_Y,      s.triangle},
        {{}, EV_KEY, BTN_TL,     s.l1},
        {{}, EV_KEY, BTN_TR,     s.r1},
        {{}, EV_KEY, BTN_SELECT, s.share},
        {{}, EV_KEY, BTN_START,  s.options},
        {{}, EV_KEY, BTN_MODE,   s.ps},
        {{}, EV_KEY, BTN_THUMBL, s.l3},
        {{}, EV_KEY, BTN_THUMBR, s.r3},
        {{}, EV_ABS, ABS_X,  stick_to_x360(s.lx)},
        {{}, EV_ABS, ABS_Y,  stick_to_x360(s.ly)},
        {{}, EV_ABS, ABS_RX, stick_to_x360(s.rx)},
        {{}, EV_ABS, ABS_RY, stick_to_x360(s.ry)},
        {{}, EV_ABS, ABS_Z,  s.l2},
        {{}, EV_ABS, ABS_RZ, s.r2},
        {{}, EV_ABS, ABS_HAT0X, s.hat_x},
        {{}, EV_ABS, ABS_HAT0Y, s.hat_y},
    }};

    // Frames arrive at the controller's report rate, mostly carrying only
    // motion data the Xbox 360 pad lacks: send just the changed controls,
    // in one write, and nothing at all if none changed.
    auto& d = *impl_;
    std::array<input_event, kReportEvents + 1> batch{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < kReportEvents; ++i) {
        if (d.sent_valid && now_state[i].value == d.sent[i].value) continue;
        batch[n++] = now_state[i];
    }
    if (n == 0) return;
    batch[n++] = input_event{{}, EV_SYN, SYN_REPORT, 0};
    [[maybe_unused]] auto _ = ::write(d.fd, batch.data(), n * sizeof(input_event));
    d.sent = now_state;
    d.sent_valid = true;
}

RumbleOutput VirtualX360::take_output() {
    auto& d = *impl_;
    const auto now = Clock::now();

    struct input_event ev{};
    while (::read(d.fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
            d.handle_ff_upload(ev.value);
        } else if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
            d.handle_ff_erase(ev.value);
        } else if (ev.type == EV_FF) {
            d.handle_ff_play(ev.code, ev.value, now);
        }
    }

    // Combined level of all playing effects; expire finished ones
    int heavy = 0, light = 0;
    for (auto& [id, e] : d.effects) {
        if (now >= e.stop_at) continue;
        heavy = std::max(heavy, e.strong >> 8);
        light = std::max(light, e.weak >> 8);
    }

    RumbleOutput out;
    if (heavy != d.last_heavy || light != d.last_light) {
        out.heavy = d.last_heavy = heavy;
        out.light = d.last_light = light;
    }
    return out;
}

} // namespace ds4linux::daemon

// ds4linux::daemon — Profile mapping implementation

#include "ds4linux/mapping.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ds4linux::daemon {

// ── Deadzones ────────────────────────────────────────────────────────────────
// Stick bytes are 0..255 with 0x80 at rest.  They are normalised to -1..1
// (128 steps below centre, 127 above) so the centre maps back to exactly 0x80.

static double stick_to_unit(std::uint8_t v) {
    return v >= 0x80 ? (v - 0x80) / 127.0 : (v - 0x80) / 128.0;
}

static std::uint8_t unit_to_stick(double f) {
    f = std::clamp(f, -1.0, 1.0);
    const long v = 0x80 + std::lround(f >= 0 ? f * 127.0 : f * 128.0);
    return static_cast<std::uint8_t>(std::clamp(v, 0L, 255L));
}

/// Rescale a magnitude through a deadzone.  Below `inner` → 0; from inner to
/// outer → 0..1 linearly; at or beyond outer → at least 1 (values past full
/// deflection, e.g. stick corners, are kept).  inner=0, outer=1 is identity.
static double rescale(double m, const DeadzoneParams& dz) {
    if (m <= dz.inner) return 0.0;
    if (m >= dz.outer) return std::max(1.0, m);
    return (m - dz.inner) / (dz.outer - dz.inner);
}

static void apply_stick_deadzone(std::uint8_t& x, std::uint8_t& y, const DeadzoneParams& dz) {
    if (dz.type == DeadzoneType::None) return;
    double fx = stick_to_unit(x), fy = stick_to_unit(y);

    if (dz.type == DeadzoneType::Circular) {
        const double m = std::hypot(fx, fy);
        if (m <= 0.0) return;
        const double k = rescale(m, dz) / m;
        fx *= k;
        fy *= k;
    } else {
        fx = std::copysign(rescale(std::fabs(fx), dz), fx);
        fy = std::copysign(rescale(std::fabs(fy), dz), fy);
    }
    x = unit_to_stick(fx);
    y = unit_to_stick(fy);
}

static void apply_trigger_deadzone(std::uint8_t& t, const DeadzoneParams& dz) {
    if (dz.type == DeadzoneType::None) return;
    const double f = std::min(1.0, rescale(t / 255.0, dz));
    t = static_cast<std::uint8_t>(std::lround(f * 255.0));
}

// ── Button remapping ─────────────────────────────────────────────────────────

using ButtonSet = std::array<bool, kDS4ButtonCount>;

static bool& at(ButtonSet& s, DS4Button b) { return s[static_cast<std::size_t>(b)]; }

static ButtonSet read_buttons(const DS4InputState& s) {
    ButtonSet b{};
    at(b, DS4Button::Cross)    = s.cross;
    at(b, DS4Button::Circle)   = s.circle;
    at(b, DS4Button::Square)   = s.square;
    at(b, DS4Button::Triangle) = s.triangle;
    at(b, DS4Button::L1)       = s.l1;
    at(b, DS4Button::R1)       = s.r1;
    at(b, DS4Button::L2)       = s.l2_btn;
    at(b, DS4Button::R2)       = s.r2_btn;
    at(b, DS4Button::Share)    = s.share;
    at(b, DS4Button::Options)  = s.options;
    at(b, DS4Button::L3)       = s.l3;
    at(b, DS4Button::R3)       = s.r3;
    at(b, DS4Button::PS)       = s.ps;
    at(b, DS4Button::Touchpad) = s.touchpad;
    at(b, DS4Button::DpadUp)    = s.hat_y < 0;
    at(b, DS4Button::DpadDown)  = s.hat_y > 0;
    at(b, DS4Button::DpadLeft)  = s.hat_x < 0;
    at(b, DS4Button::DpadRight) = s.hat_x > 0;
    return b;
}

static void write_buttons(DS4InputState& s, ButtonSet& b) {
    s.cross    = at(b, DS4Button::Cross);
    s.circle   = at(b, DS4Button::Circle);
    s.square   = at(b, DS4Button::Square);
    s.triangle = at(b, DS4Button::Triangle);
    s.l1       = at(b, DS4Button::L1);
    s.r1       = at(b, DS4Button::R1);
    s.l2_btn   = at(b, DS4Button::L2);
    s.r2_btn   = at(b, DS4Button::R2);
    s.share    = at(b, DS4Button::Share);
    s.options  = at(b, DS4Button::Options);
    s.l3       = at(b, DS4Button::L3);
    s.r3       = at(b, DS4Button::R3);
    s.ps       = at(b, DS4Button::PS);
    s.touchpad = at(b, DS4Button::Touchpad);
    s.hat_x = int{at(b, DS4Button::DpadRight)} - int{at(b, DS4Button::DpadLeft)};
    s.hat_y = int{at(b, DS4Button::DpadDown)}  - int{at(b, DS4Button::DpadUp)};
}

static void apply_button_map(DS4InputState& s, const ButtonMap& map) {
    if (map.empty()) return;

    const ButtonSet in = read_buttons(s);
    ButtonSet out{};
    auto target_of = [&](DS4Button b) {
        auto it = map.find(b);
        return it != map.end() ? it->second : b;
    };

    // Analog triggers follow their button: an L2/R2 that stays L2/R2 keeps
    // its analog value; a button mapped onto L2/R2 drives it fully.
    std::uint8_t l2 = 0, r2 = 0;
    for (std::size_t i = 0; i < kDS4ButtonCount; ++i) {
        const auto src = static_cast<DS4Button>(i);
        const auto dst = target_of(src);
        if (dst == DS4Button::None) continue;
        if (in[i]) at(out, dst) = true;

        const std::uint8_t level =
            src == DS4Button::L2 ? s.l2 : src == DS4Button::R2 ? s.r2
                                        : (in[i] ? 255 : 0);
        if (dst == DS4Button::L2) l2 = std::max(l2, level);
        if (dst == DS4Button::R2) r2 = std::max(r2, level);
    }
    write_buttons(s, out);
    s.l2 = l2;
    s.r2 = r2;
}

// ── Public API ───────────────────────────────────────────────────────────────

DS4InputState apply_profile(const DS4InputState& raw, const Profile& p) {
    DS4InputState s = raw;
    apply_stick_deadzone(s.lx, s.ly, p.left_stick_dz);
    apply_stick_deadzone(s.rx, s.ry, p.right_stick_dz);
    apply_trigger_deadzone(s.l2, p.left_trigger_dz);
    apply_trigger_deadzone(s.r2, p.right_trigger_dz);
    apply_button_map(s, p.button_map);
    return s;
}

std::uint8_t scale_rumble(int value, double strength) {
    const double v = std::clamp(value, 0, 255) * std::clamp(strength, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(v));
}

} // namespace ds4linux::daemon

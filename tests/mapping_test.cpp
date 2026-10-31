// Tests for the profile mapping stage (deadzones, remaps, rumble scaling)
// and profile validation.

#include "ds4linux/mapping.h"

#include <cstdio>
#include <string>
using namespace ds4linux; using namespace ds4linux::daemon;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main() {
    Profile none; none.left_stick_dz.type = DeadzoneType::None; none.right_stick_dz.type = DeadzoneType::None;
    Profile ident; ident.left_stick_dz = {DeadzoneType::Circular, 0.0, 1.0};
    Profile sq; sq.left_stick_dz = {DeadzoneType::Square, 0.0, 1.0};
    // identity for every stick value (none, circular 0..1 on-axis, square 0..1)
    for (int v = 0; v < 256; ++v) {
        DS4InputState s; s.lx = v; s.ly = 0x80;
        CHECK(apply_profile(s, none).lx == v);
        CHECK(apply_profile(s, ident).lx == v);
        CHECK(apply_profile(s, sq).lx == v);
    }
    // corners kept with circular 0..1
    { DS4InputState s; s.lx = 255; s.ly = 0; auto o = apply_profile(s, ident); CHECK(o.lx == 255 && o.ly == 0); }
    // centre stays exactly 0x80 with default deadzone
    { Profile d; DS4InputState s; auto o = apply_profile(s, d); CHECK(o.lx == 0x80 && o.ly == 0x80 && o.rx == 0x80); }
    // inside inner deadzone → centre
    { Profile d; d.left_stick_dz = {DeadzoneType::Circular, 0.2, 1.0}; DS4InputState s; s.lx = 0x80 + 20; s.ly = 0x80 + 10;
      auto o = apply_profile(s, d); CHECK(o.lx == 0x80 && o.ly == 0x80); }
    // just past inner → small, monotonic; outer 0.5 → full at half deflection
    { Profile d; d.left_stick_dz = {DeadzoneType::Circular, 0.0, 0.5}; DS4InputState s; s.lx = 0x80 + 64; s.ly = 0x80;
      auto o = apply_profile(s, d); CHECK(o.lx == 255); s.lx = 0x80 - 64; o = apply_profile(s, d); CHECK(o.lx <= 1); }
    { Profile d; d.left_stick_dz = {DeadzoneType::Square, 0.1, 1.0}; int prev = 0x80; bool mono = true;
      for (int v = 0x80; v < 256; ++v) { DS4InputState s; s.lx = v; int o = apply_profile(s, d).lx; if (o < prev) mono = false; prev = o; }
      CHECK(mono && prev == 255); }
    // trigger deadzone
    { Profile d; d.left_trigger_dz = {DeadzoneType::Circular, 0.1, 0.9}; DS4InputState s;
      s.l2 = 20; CHECK(apply_profile(s, d).l2 == 0); s.l2 = 240; CHECK(apply_profile(s, d).l2 == 255);
      s.l2 = 128; int o = apply_profile(s, d).l2; CHECK(o > 120 && o < 136); }
    // button remap: swap, disable, onto trigger, trigger elsewhere, dpad
    { Profile d; d.button_map = {{DS4Button::Cross, DS4Button::Circle}, {DS4Button::Circle, DS4Button::Cross},
                                 {DS4Button::PS, DS4Button::None}, {DS4Button::Square, DS4Button::R2},
                                 {DS4Button::L2, DS4Button::L1}, {DS4Button::DpadUp, DS4Button::Triangle}};
      DS4InputState s; s.cross = true; s.ps = true; s.square = true; s.l2_btn = true; s.l2 = 200; s.hat_y = -1; s.hat_x = 1;
      auto o = apply_profile(s, d);
      CHECK(o.circle && !o.cross); CHECK(!o.ps); CHECK(!o.square && o.r2_btn && o.r2 == 255);
      CHECK(o.l1 && !o.l2_btn && o.l2 == 0); CHECK(o.triangle && o.hat_y == 0 && o.hat_x == 1); }
    // unmapped L2 keeps analog even when partially pressed (button not yet down)
    { Profile d; d.button_map = {{DS4Button::Cross, DS4Button::Circle}}; DS4InputState s; s.l2 = 17;
      CHECK(apply_profile(s, d).l2 == 17); }
    CHECK(scale_rumble(255, 0.6) == 153); CHECK(scale_rumble(300, 2.0) == 255); CHECK(scale_rumble(10, -1) == 0);

    // Profile validation: out-of-range values are clamped, unknown keys ignored
    { Profile p = profile_from_json(R"({"name":"X","output_mode":7,"rumble_strength":"loud",
          "left_stick_dz":{"inner":5,"outer":-3,"type":99},
          "button_map":{"1":"2","cross":5,"l1":"r1","ps":"none"}})");
      CHECK(p.output_mode == OutputMode::X360); CHECK(p.rumble_strength == 1.0);
      CHECK(p.left_stick_dz.type == DeadzoneType::Square);
      CHECK(p.left_stick_dz.inner < p.left_stick_dz.outer && p.left_stick_dz.outer <= 1.0);
      CHECK(p.button_map.size() == 2 && p.button_map.at(DS4Button::L1) == DS4Button::R1);
      CHECK(p.button_map.at(DS4Button::PS) == DS4Button::None); }
    { Profile p; p.name = "Round trip"; p.output_mode = OutputMode::X360; p.rumble_strength = 0.25;
      p.button_map[DS4Button::DpadUp] = DS4Button::Triangle;
      Profile q = profile_from_json(profile_to_json(p));
      CHECK(q.name == p.name && q.output_mode == p.output_mode && q.rumble_strength == 0.25);
      CHECK(q.button_map == p.button_map); }
    { Profile p = profile_from_json(R"({"output_mode":1})"); CHECK(p.name.empty() && !is_valid_profile_name(p.name)); }
    { bool threw = false; try { (void)profile_from_json("{oops"); } catch (const std::exception&) { threw = true; }
      CHECK(threw); }
    CHECK(is_valid_profile_name("Racing 2.0_v-1")); CHECK(!is_valid_profile_name(""));
    CHECK(!is_valid_profile_name("../x")); CHECK(!is_valid_profile_name(".hidden"));
    CHECK(!is_valid_profile_name(std::string(65, 'a'))); CHECK(!is_valid_profile_name("a/b"));
    std::printf(fails ? "%d failure(s)\n" : "all mapping tests passed\n", fails);
    return fails != 0;
}

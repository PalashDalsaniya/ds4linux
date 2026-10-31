// Tests the virtual Xbox 360 controller against the real kernel: identity,
// input values as a game reads them, and the force-feedback upload / play /
// erase handshake.  Needs write access to /dev/uinput; skipped otherwise.

#include "ds4linux/virtual_x360.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
using namespace ds4linux::daemon;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

/// Event nodes that look like a ds4linux virtual Xbox 360 pad.
static std::set<std::string> x360_nodes() {
    std::set<std::string> found;
    DIR* d = opendir("/dev/input");
    if (!d) return found;
    while (auto* e = readdir(d)) {
        if (std::strncmp(e->d_name, "event", 5)) continue;
        std::string p = std::string("/dev/input/") + e->d_name;
        int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char phys[64] = {}; ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        input_id id{}; ioctl(fd, EVIOCGID, &id);
        if (!std::strcmp(phys, "ds4linux/x360") && id.vendor == 0x045e && id.product == 0x028e) found.insert(p);
        close(fd);
    }
    closedir(d);
    return found;
}

/// Open the node created after `before` was taken (other pads may exist,
/// e.g. from a running daemon).
static int find_new_node(const std::set<std::string>& before) {
    for (int tries = 0; tries < 50; ++tries) {
        for (const auto& p : x360_nodes()) {
            if (before.count(p)) continue;
            int fd = open(p.c_str(), O_RDWR | O_NONBLOCK);
            if (fd >= 0) return fd;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return -1;
}

int main() {
    if (access("/dev/uinput", W_OK) != 0) {
        std::printf("no access to /dev/uinput — skipped\n");
        return 77;
    }
    const auto before = x360_nodes();
    VirtualX360 pad;
    int fd = find_new_node(before);
    CHECK(fd >= 0);
    if (fd < 0) { std::printf("created device not accessible — skipped\n"); return 77; }
    char name[64] = {}; ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    CHECK(std::string(name) == "Microsoft X-Box 360 pad");
    input_absinfo ai{}; ioctl(fd, EVIOCGABS(ABS_X), &ai); CHECK(ai.minimum == -32768 && ai.maximum == 32767);

    // Input: press A, stick full left/up, R2 half
    DS4InputState s; s.cross = true; s.lx = 0; s.ly = 0; s.r2 = 128; s.hat_x = 1;
    pad.send_report(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    int a = -1, x = 1, y = 1, rz = -1, hx = 0; input_event ev{};
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY && ev.code == BTN_A) a = ev.value;
        if (ev.type == EV_ABS && ev.code == ABS_X) x = ev.value;
        if (ev.type == EV_ABS && ev.code == ABS_Y) y = ev.value;
        if (ev.type == EV_ABS && ev.code == ABS_RZ) rz = ev.value;
        if (ev.type == EV_ABS && ev.code == ABS_HAT0X) hx = ev.value;
    }
    CHECK(a == 1); CHECK(x == -32768); CHECK(y == -32768); CHECK(rz == 128); CHECK(hx == 1);
    s = {}; s.lx = 255; pad.send_report(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) if (ev.type == EV_ABS && ev.code == ABS_X) x = ev.value;
    CHECK(x == 32767);

    // Force feedback: the upload blocks until the daemon side answers
    std::atomic<bool> done{false}; ff_effect eff{}; int rc = -1;
    std::thread game([&] {
        eff.type = FF_RUMBLE; eff.id = -1; eff.u.rumble.strong_magnitude = 0xC000;
        eff.u.rumble.weak_magnitude = 0x4000; eff.replay.length = 150;
        rc = ioctl(fd, EVIOCSFF, &eff); done = true;
    });
    for (int i = 0; i < 200 && !done; ++i) { (void)pad.take_output(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    game.join();
    CHECK(rc == 0); CHECK(eff.id >= 0);
    input_event play{}; play.type = EV_FF; play.code = eff.id; play.value = 1;
    CHECK(write(fd, &play, sizeof(play)) == sizeof(play));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto out = pad.take_output();
    CHECK(out.heavy == 0xC0); CHECK(out.light == 0x40);
    auto again = pad.take_output(); CHECK(again.heavy == -1);   // no duplicate updates
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // replay.length expires
    out = pad.take_output(); CHECK(out.heavy == 0 && out.light == 0);
    // erase
    std::atomic<bool> erased{false}; int erc = -1;
    std::thread g2([&] { erc = ioctl(fd, EVIOCRMFF, eff.id); erased = true; });
    for (int i = 0; i < 200 && !erased; ++i) { (void)pad.take_output(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    g2.join(); CHECK(erc == 0);
    close(fd);
    std::printf(fails ? "%d failure(s)\n" : "all x360 tests passed\n", fails);
    return fails != 0;
}

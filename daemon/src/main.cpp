// ds4linux daemon — main entry point
//
// Headless daemon: grabs DualSense/DS4 controllers, creates virtual DS4 or
// Xbox 360 devices, and forwards input + rumble. Hides the real controller
// from other applications (including Steam).  The CLI (ds4linux-ctl) and GUI
// control it over a Unix socket.
//
// Runs as: systemd service (root) → /usr/bin/ds4linux-daemon

#include "ds4linux/constants.h"
#include "ds4linux/debug.h"
#include "ds4linux/device_manager.h"
#include "ds4linux/ipc_handler.h"
#include "ds4linux/ipc_server.h"
#include "ds4linux/node_hider.h"
#include "ds4linux/profile_manager.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libudev.h>
#include <memory>
#include <sstream>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

using namespace ds4linux;
using namespace ds4linux::daemon;

// ── Globals (signal safe) ────────────────────────────────────────────────────
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int /*sig*/) {
    g_running = 0;
}

// ── Command line ─────────────────────────────────────────────────────────────

static void print_usage() {
    std::cout <<
        "Usage: ds4linux-daemon [options]\n"
        "\n"
        "Options:\n"
        "  --generic             Also emulate a DS4 for non-Sony gamepads\n"
        "  --generic=VID:PID,..  Only for these gamepads (hex IDs, e.g. 046d:c21f)\n"
        "  --socket=PATH         Control socket (default " << kSocketPath << ")\n"
        "  --state-dir=DIR       Profiles and settings (default " << kStateDir << ")\n"
        "  --debug               Log rumble / lightbar traffic from games\n"
        "  --version             Print version and exit\n"
        "  -h, --help            Show this help\n";
}

/// Parse "046d:c21f,045e:028e" into VID:PID pairs.  Returns false on error.
static bool parse_id_list(const std::string& list,
                          std::vector<std::pair<std::uint16_t, std::uint16_t>>& out) {
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        unsigned vid = 0, pid = 0;
        char tail = 0;
        if (std::sscanf(item.c_str(), "%x:%x%c", &vid, &pid, &tail) != 2 ||
            vid > 0xFFFF || pid > 0xFFFF) {
            return false;
        }
        out.emplace_back(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid));
    }
    return !out.empty();
}

// ── Hotplug (udev monitor) ───────────────────────────────────────────────────

/// Kernel-level "input" add / remove events, after udev has processed them
/// (so device nodes exist and have their permissions).
class HotplugMonitor {
public:
    HotplugMonitor() {
        udev_ = udev_new();
        if (!udev_) return;
        mon_ = udev_monitor_new_from_netlink(udev_, "udev");
        if (!mon_ ||
            udev_monitor_filter_add_match_subsystem_devtype(mon_, "input", nullptr) < 0 ||
            udev_monitor_enable_receiving(mon_) < 0) {
            reset();
        }
    }
    ~HotplugMonitor() { reset(); }
    HotplugMonitor(const HotplugMonitor&) = delete;
    HotplugMonitor& operator=(const HotplugMonitor&) = delete;

    [[nodiscard]] int fd() const { return mon_ ? udev_monitor_get_fd(mon_) : -1; }

    /// Drain pending events, passing (action, /dev/input/eventN) for evdev nodes.
    template <typename F>
    void drain(F&& on_event) {
        if (!mon_) return;
        while (struct udev_device* dev = udev_monitor_receive_device(mon_)) {
            const char* action  = udev_device_get_action(dev);
            const char* sysname = udev_device_get_sysname(dev);
            if (action && sysname && std::strncmp(sysname, "event", 5) == 0) {
                on_event(std::string(action), std::string("/dev/input/") + sysname);
            }
            udev_device_unref(dev);
        }
    }

private:
    void reset() {
        if (mon_) udev_monitor_unref(mon_);
        if (udev_) udev_unref(udev_);
        mon_ = nullptr;
        udev_ = nullptr;
    }
    struct udev*         udev_ = nullptr;
    struct udev_monitor* mon_  = nullptr;
};

// ── Epoll set ────────────────────────────────────────────────────────────────
// Rebuilt from scratch whenever the device fds change (controllers or virtual
// outputs added / removed), which is rare and avoids stale registrations when
// fd numbers get reused.  IPC clients are added as they connect; closing a
// client fd removes it from the set by itself.

static void epoll_add(int epfd, int fd) {
    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) perror("epoll_ctl ADD");
}

static int build_epoll(const DeviceManager& devmgr, const IpcServer* ipc, int hotplug_fd) {
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        return -1;
    }
    if (hotplug_fd >= 0) epoll_add(epfd, hotplug_fd);
    for (int fd : devmgr.poll_fds()) epoll_add(epfd, fd);
    if (ipc) {
        epoll_add(epfd, ipc->fd());
        for (int fd : ipc->clients()) epoll_add(epfd, fd);
    }
    return epfd;
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    DeviceManagerOptions options;
    std::string socket_path(kSocketPath);
    std::string state_dir(kStateDir);
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return EXIT_SUCCESS;
        } else if (arg == "--version") {
            std::cout << "ds4linux-daemon " << kVersion << "\n";
            return EXIT_SUCCESS;
        } else if (arg == "--debug") {
            g_debug = true;
        } else if (arg == "--generic") {
            options.generic_enabled = true;
        } else if (arg.rfind("--generic=", 0) == 0) {
            options.generic_enabled = true;
            if (!parse_id_list(arg.substr(10), options.generic_ids)) {
                std::cerr << "[ds4linux] Invalid --generic list: " << arg.substr(10)
                          << " (expected VID:PID[,VID:PID...] in hex)\n";
                return EXIT_FAILURE;
            }
        } else if (arg.rfind("--socket=", 0) == 0 && arg.size() > 9) {
            socket_path = arg.substr(9);
        } else if (arg.rfind("--state-dir=", 0) == 0 && arg.size() > 12) {
            state_dir = arg.substr(12);
        } else {
            std::cerr << "[ds4linux] Unknown option: " << arg << "\n";
            print_usage();
            return EXIT_FAILURE;
        }
    }

    std::cout << "[ds4linux] Daemon v" << kVersion << " starting\n";
    if (options.generic_enabled) {
        std::cout << "[ds4linux] Generic gamepad emulation enabled ("
                  << (options.generic_ids.empty() ? "all gamepads"
                                                  : "selected gamepads")
                  << ")\n";
    }

    // ── Signal handling ──────────────────────────────────────────────────────
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    // Controllers hidden by a daemon that didn't exit cleanly
    NodeHider::clear_stale_rules();

    // ── Hotplug monitor (before the first scan, so nothing is missed) ───────
    HotplugMonitor hotplug;
    if (hotplug.fd() < 0) {
        std::cerr << "[ds4linux] udev monitor unavailable — controllers connected "
                     "later need `ds4linux-ctl rescan`\n";
    }

    // ── Profiles + device manager ────────────────────────────────────────────
    ProfileManager profiles(state_dir);
    DeviceManager devmgr(options);
    devmgr.set_profile_resolver([&](const std::string& key) {
        return profiles.profile_for(key);
    });
    devmgr.scan();

    if (devmgr.slots().empty()) {
        std::cerr << "[ds4linux] No controllers found yet — waiting for one "
                     "to be connected.\n";
    }

    // ── IPC (CLI / GUI control socket) ───────────────────────────────────────
    std::unique_ptr<IpcServer> ipc;
    try {
        ipc = std::make_unique<IpcServer>(socket_path);
        std::cout << "[ds4linux] Control socket: " << socket_path << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] Control socket unavailable (" << ex.what()
                  << ") — ds4linux-ctl and the GUI will not work\n";
    }
    IpcHandler handler(devmgr, profiles);

    // ── Main loop ────────────────────────────────────────────────────────────
    int epfd = -1;
    struct epoll_event events[kEpollMaxEvents];
    std::cout << "[ds4linux] Entering main loop (Ctrl+C to stop)\n";

    while (g_running) {
        if (epfd < 0 || devmgr.take_fds_changed()) {
            if (epfd >= 0) ::close(epfd);
            epfd = build_epoll(devmgr, ipc.get(), hotplug.fd());
            if (epfd < 0) return EXIT_FAILURE;
        }

        int nfds = ::epoll_wait(epfd, events, kEpollMaxEvents, kEpollTimeoutMs);

        for (int i = 0; i < nfds; ++i) {
            const int fd = events[i].data.fd;
            if (fd == hotplug.fd()) {
                hotplug.drain([&](const std::string& action, const std::string& path) {
                    if (action == "add") devmgr.on_device_added(path);
                    else if (action == "remove") devmgr.on_device_removed(path);
                });
            } else if (ipc && fd == ipc->fd()) {
                for (int client; (client = ipc->accept_client()) >= 0;) epoll_add(epfd, client);
            } else if (ipc && ipc->is_client(fd)) {
                ipc->read_client(fd, [&](int client, const std::string& msg) {
                    ipc->send(client, handler.handle(msg));
                });
            }
        }

        // Every wake-up, whatever caused it: reads are non-blocking, and
        // rumble / force-feedback deadlines must expire even while idle
        devmgr.translate_events();
    }

    std::cout << "[ds4linux] Daemon shutting down\n";
    if (epfd >= 0) ::close(epfd);
    return EXIT_SUCCESS;
}

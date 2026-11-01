// ds4linux daemon — main entry point
//
// Headless daemon: grabs DualSense/DS4 controllers, creates virtual DS4
// devices, and forwards input + rumble. Hides the real controller from
// other applications (including Steam).
//
// Runs as: systemd service (root) → /usr/bin/ds4linux-daemon

#include "ds4linux/constants.h"
#include "ds4linux/device_manager.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <libudev.h>
#include <sstream>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <unordered_map>

using namespace ds4linux;
using namespace ds4linux::daemon;

// ── Globals (signal safe) ────────────────────────────────────────────────────
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int /*sig*/) {
    g_running = 0;
}

// ── Epoll helpers ────────────────────────────────────────────────────────────

static void epoll_add(int epfd, int fd, uint32_t events = EPOLLIN) {
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl ADD");
    }
}

// ── Command line ─────────────────────────────────────────────────────────────

static void print_usage() {
    std::cout <<
        "Usage: ds4linux-daemon [options]\n"
        "\n"
        "Options:\n"
        "  --generic             Also emulate a DS4 for non-Sony gamepads\n"
        "  --generic=VID:PID,..  Only for these gamepads (hex IDs, e.g. 046d:c21f)\n"
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

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    DeviceManagerOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return EXIT_SUCCESS;
        } else if (arg == "--version") {
            std::cout << "ds4linux-daemon " << kVersion << "\n";
            return EXIT_SUCCESS;
        } else if (arg == "--generic") {
            options.generic_enabled = true;
        } else if (arg.rfind("--generic=", 0) == 0) {
            options.generic_enabled = true;
            if (!parse_id_list(arg.substr(10), options.generic_ids)) {
                std::cerr << "[ds4linux] Invalid --generic list: " << arg.substr(10)
                          << " (expected VID:PID[,VID:PID...] in hex)\n";
                return EXIT_FAILURE;
            }
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

    // ── udev monitor ─────────────────────────────────────────────────────────
    struct udev* udev = udev_new();
    if (!udev) {
        std::cerr << "[ds4linux] Could not create udev context\n";
        return EXIT_FAILURE;
    }
    struct udev_monitor* udev_monitor =
        udev_monitor_new_from_netlink(udev, "udev");
    if (!udev_monitor ||
        udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "input", nullptr) < 0 ||
        udev_monitor_enable_receiving(udev_monitor) < 0) {
        std::cerr << "[ds4linux] Could not start udev monitor\n";
        if (udev_monitor) udev_monitor_unref(udev_monitor);
        udev_unref(udev);
        return EXIT_FAILURE;
    }
    const int udev_fd = udev_monitor_get_fd(udev_monitor);
    if (udev_fd < 0) {
        std::cerr << "[ds4linux] Could not get udev monitor fd\n";
        udev_monitor_unref(udev_monitor);
        udev_unref(udev);
        return EXIT_FAILURE;
    }

    // ── Device manager ───────────────────────────────────────────────────────
    DeviceManager devmgr(options);
    devmgr.scan();

    // ── epoll setup ──────────────────────────────────────────────────────────
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        udev_monitor_unref(udev_monitor);
        udev_unref(udev);
        return EXIT_FAILURE;
    }

    // Register all physical + virtual device fds
    std::unordered_map<int, std::string> registered_fds;

    auto sync_epoll = [&]() {
        for (auto& [path, slot] : devmgr.slots()) {
            auto reg = [&](int fd, const char* label) {
                if (fd >= 0 && !registered_fds.count(fd)) {
                    epoll_add(epfd, fd);
                    registered_fds[fd] = path;
                    std::cout << "[ds4linux] epoll: " << label
                              << " fd=" << fd << " (" << path << ")\n";
                }
            };
            if (slot.physical)          reg(slot.physical->fd(),          "gamepad");
            if (slot.physical_touchpad) reg(slot.physical_touchpad->fd(), "touchpad");
            if (slot.physical_motion)   reg(slot.physical_motion->fd(),   "motion");
            // Rumble/LED output from applications (collected by the UHID thread)
            if (slot.virtual_dev)       reg(slot.virtual_dev->output_fd(), "uhid-output");
        }
    };

    auto unregister_path = [&](const std::string& path) {
        for (auto it = registered_fds.begin(); it != registered_fds.end();) {
            if (it->second != path) {
                ++it;
                continue;
            }
            if (::epoll_ctl(epfd, EPOLL_CTL_DEL, it->first, nullptr) < 0) {
                perror("epoll_ctl DEL");
            }
            it = registered_fds.erase(it);
        }
    };

    epoll_add(epfd, udev_fd);
    sync_epoll();

    if (devmgr.slots().empty()) {
        std::cerr << "[ds4linux] No controllers found. "
                     "Make sure the daemon is running as root.\n";
    }

    // ── Main loop ────────────────────────────────────────────────────────────
    struct epoll_event events[kEpollMaxEvents];
    std::cout << "[ds4linux] Entering main loop (Ctrl+C to stop)\n";

    while (g_running) {
        int nfds = ::epoll_wait(epfd, events, kEpollMaxEvents, 200 /*ms*/);

        if (nfds > 0) {
            bool rescan = false;
            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd != udev_fd) continue;

                while (struct udev_device* device =
                           udev_monitor_receive_device(udev_monitor)) {
                    const char* action = udev_device_get_action(device);
                    const char* sysname = udev_device_get_sysname(device);
                    if (action && sysname && !std::strncmp(sysname, "event", 5)) {
                        const std::string path = std::string("/dev/input/") + sysname;
                        if (!std::strcmp(action, "remove")) {
                            if (devmgr.slots().count(path)) {
                                unregister_path(path);
                                devmgr.on_device_removed(path);
                            }
                        } else if (!std::strcmp(action, "add")) {
                            rescan = true;
                        }
                    }
                    udev_device_unref(device);
                }
            }
            if (rescan) {
                devmgr.scan();
                sync_epoll();
            }
            for (const auto& path : devmgr.translate_events()) {
                unregister_path(path);
                devmgr.on_device_removed(path);
            }
        }
    }

    std::cout << "[ds4linux] Daemon shutting down\n";
    ::close(epfd);
    udev_monitor_unref(udev_monitor);
    udev_unref(udev);
    return EXIT_SUCCESS;
}

// ds4linux daemon — main entry point
//
// Headless daemon: grabs DualSense/DS4 controllers, creates virtual DS4 or
// Xbox 360 devices, and forwards input + rumble. Hides the real controller
// from other applications (including Steam).  The CLI (ds4linux-ctl) and GUI
// control it over a Unix socket.
//
// Runs as: systemd service (root) → /usr/bin/ds4linux-daemon

#include "ds4linux/constants.h"
#include "ds4linux/device_manager.h"
#include "ds4linux/ipc_handler.h"
#include "ds4linux/ipc_server.h"
#include "ds4linux/profile_manager.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

// ── Epoll set ────────────────────────────────────────────────────────────────
// Rebuilt from scratch whenever the set of fds changes (controllers or
// virtual outputs added / removed, IPC clients connecting).  Rebuilding is
// rare and avoids stale registrations when fd numbers get reused.

static int build_epoll(const DeviceManager& devmgr, const IpcServer* ipc) {
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        return -1;
    }
    auto add = [&](int fd) {
        struct epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) perror("epoll_ctl ADD");
    };
    for (int fd : devmgr.poll_fds()) add(fd);
    if (ipc) {
        add(ipc->fd());
        for (int fd : ipc->clients()) add(fd);
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

    // ── Profiles + device manager ────────────────────────────────────────────
    ProfileManager profiles(state_dir);
    DeviceManager devmgr(options);
    devmgr.set_profile_resolver([&](const std::string& key) {
        return profiles.profile_for(key);
    });
    devmgr.scan();

    if (devmgr.slots().empty()) {
        std::cerr << "[ds4linux] No controllers found. "
                     "Make sure the daemon is running as root.\n";
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
    bool ipc_clients_changed = false;

    // ── Main loop ────────────────────────────────────────────────────────────
    int epfd = -1;
    struct epoll_event events[kEpollMaxEvents];
    std::cout << "[ds4linux] Entering main loop (Ctrl+C to stop)\n";

    while (g_running) {
        if (epfd < 0 || devmgr.take_fds_changed() || ipc_clients_changed) {
            if (epfd >= 0) ::close(epfd);
            epfd = build_epoll(devmgr, ipc.get());
            if (epfd < 0) return EXIT_FAILURE;
            ipc_clients_changed = false;
        }

        int nfds = ::epoll_wait(epfd, events, kEpollMaxEvents, 200 /*ms*/);

        bool device_activity = false;
        for (int i = 0; i < nfds; ++i) {
            const int fd = events[i].data.fd;
            if (ipc && fd == ipc->fd()) {
                while (ipc->accept_client() >= 0) ipc_clients_changed = true;
            } else if (ipc && ipc->is_client(fd)) {
                bool alive = ipc->read_client(fd, [&](int client, const std::string& msg) {
                    ipc->send(client, handler.handle(msg));
                });
                if (!alive) ipc_clients_changed = true;
            } else {
                device_activity = true;
            }
        }

        // Also run on timeouts so rumble deadlines expire while idle
        if (device_activity || nfds == 0) {
            devmgr.translate_events();
        }
    }

    std::cout << "[ds4linux] Daemon shutting down\n";
    if (epfd >= 0) ::close(epfd);
    return EXIT_SUCCESS;
}

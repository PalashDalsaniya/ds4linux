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
#include <cstdlib>
#include <iostream>
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

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "[ds4linux] Daemon v" << kVersion << " starting\n";

    // ── Signal handling ──────────────────────────────────────────────────────
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    // ── Device manager ───────────────────────────────────────────────────────
    DeviceManager devmgr;
    devmgr.scan();

    // ── epoll setup ──────────────────────────────────────────────────────────
    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
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
            // UHID fd for output events (rumble/LED from applications)
            if (slot.virtual_dev)       reg(slot.virtual_dev->fd(),       "uhid-gamepad");
        }
    };

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
            devmgr.translate_events();
        }
    }

    std::cout << "[ds4linux] Daemon shutting down\n";
    ::close(epfd);
    return EXIT_SUCCESS;
}

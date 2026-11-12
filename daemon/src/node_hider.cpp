// ds4linux::daemon — NodeHider

#include "ds4linux/node_hider.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

// Sorted after the rules that tag controllers "uaccess" (60-steam-input,
// 70-uaccess) and before 73-seat-late, which turns the tag into an ACL.
static constexpr const char* kRulesDir  = "/run/udev/rules.d";
static constexpr const char* kRulesFile = "/run/udev/rules.d/72-ds4linux-hidden.rules";

// Hidden devices (sysfs paths) and how many hiders use each
static std::map<std::string, int>& hidden_devices() {
    static std::map<std::string, int> devices;
    return devices;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

extern "C" char** environ;

/// Run a command and wait for it.  Returns false if it couldn't run or failed.
/// (posix_spawn: safe in this multi-threaded process, and no fork copy.)
static bool run(std::vector<const char*> argv) {
    argv.push_back(nullptr);
    pid_t pid = 0;
    if (::posix_spawnp(&pid, argv[0], nullptr, nullptr,
                       const_cast<char* const*>(argv.data()), environ) != 0) {
        return false;
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Write the rules for the currently hidden devices (or remove the file),
/// then make udev load them.
static void write_rules() {
    std::error_code ec;
    const auto& devices = hidden_devices();
    if (devices.empty()) {
        fs::remove(kRulesFile, ec);
    } else {
        fs::create_directories(kRulesDir, ec);
        const std::string tmp = std::string(kRulesFile) + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << "# Written by ds4linux-daemon: controllers it is using right now.\n"
                   "# Removed when it releases them (or stops).\n";
            for (const auto& [sysfs, n] : devices) {
                // hide() only accepts paths without glob / list characters
                // DEVPATH is the sysfs path without the "/sys" prefix
                out << "SUBSYSTEM==\"input|hidraw\", DEVPATH==\"" << sysfs.substr(4)
                    << "/*\", MODE:=\"0600\", TAG-=\"uaccess\"\n";
            }
        }
        fs::rename(tmp, kRulesFile, ec);
        if (ec) {
            std::cerr << "[ds4linux] Could not write " << kRulesFile << ": "
                      << ec.message() << "\n";
        }
    }
    // Bounded: this runs on the event loop, and a busy udevd must not
    // freeze every controller for the default 60 s
    if (!run({"udevadm", "control", "--timeout=5", "--reload"})) {
        std::cerr << "[ds4linux] `udevadm control --reload` failed; the physical "
                     "controller may stay visible to other programs\n";
    }
}

/// A device below the controller that has a device node.
struct Node {
    fs::path    sysfs;    // its sysfs directory
    std::string devnode;  // /dev/...
};

/// Every device node below a sysfs device: evdev, joydev (jsN), hidraw ...
static std::vector<Node> device_nodes(const std::string& sysfs) {
    std::vector<Node> nodes;
    std::error_code ec;
    fs::recursive_directory_iterator it(sysfs, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 3) {  // hidraw/hidrawN, input/inputN/eventN are shallow
            it.disable_recursion_pending();
            continue;
        }
        if (it->path().filename() != "uevent") continue;
        std::ifstream in(it->path());
        for (std::string line; std::getline(in, line);) {
            if (line.starts_with("DEVNAME=")) {
                nodes.push_back({it->path().parent_path(), "/dev/" + line.substr(8)});
                break;
            }
        }
    }
    return nodes;
}

/// Ask udev to re-process a device with the current rules.
static void trigger_change(const Node& n) {
    std::ofstream(n.sysfs / "uevent") << "change";
}

/// Warn about processes that opened the node before it was hidden: they
/// keep their file descriptor and still see the physical controller.
static void warn_existing_users(const std::vector<Node>& devnodes) {
    const std::string self = std::to_string(::getpid());
    std::error_code ec;
    fs::directory_iterator procs("/proc", ec), end;
    for (; !ec && procs != end; procs.increment(ec)) {
        const auto pid = procs->path().filename().string();
        if (pid == self || pid.find_first_not_of("0123456789") != std::string::npos) continue;

        std::error_code fd_ec;
        fs::directory_iterator fds(procs->path() / "fd", fd_ec);
        for (; !fd_ec && fds != end; fds.increment(fd_ec)) {
            std::error_code link_ec;
            const auto target = fs::read_symlink(fds->path(), link_ec).string();
            if (link_ec) continue;
            bool match = false;
            for (const auto& n : devnodes) match = match || target == n.devnode;
            if (!match) continue;

            std::string comm;
            std::ifstream(procs->path() / "comm") >> comm;
            std::cerr << "[ds4linux] " << (comm.empty() ? "a process" : comm)
                      << " (pid " << pid << ") already has " << target
                      << " open and will still see the physical controller; "
                         "restart it to avoid a duplicate pad\n";
        }
    }
}

// ── NodeHider ────────────────────────────────────────────────────────────────

NodeHider::~NodeHider() { release(); }

NodeHider::NodeHider(NodeHider&& other) noexcept
    : device_(std::exchange(other.device_, {})) {}

NodeHider& NodeHider::operator=(NodeHider&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, {});
    }
    return *this;
}

void NodeHider::hide(const std::string& sysfs_path) {
    if (sysfs_path.empty() || !sysfs_path.starts_with("/sys/") || sysfs_path == device_) return;
    if (sysfs_path.find_first_of("*?[]|\"\\") != std::string::npos) {
        std::cerr << "[ds4linux] Not hiding " << sysfs_path
                  << ": unexpected characters in its sysfs path\n";
        return;
    }
    release();
    device_ = sysfs_path;
    if (hidden_devices()[sysfs_path]++ > 0) return;  // already hidden

    write_rules();

    const auto nodes = device_nodes(sysfs_path);
    for (const auto& node : nodes) {
        // Take effect right away; the "change" event below makes udev apply
        // the rule too (dropping the uaccess tag that logind would act on)
        const char* path = node.devnode.c_str();
        ::chmod(path, 0600);
        if (::removexattr(path, "system.posix_acl_access") < 0 &&
            errno != ENODATA && errno != ENOTSUP && errno != ENOENT) {
            std::cerr << "[ds4linux] Could not remove the ACL of " << path << ": "
                      << std::strerror(errno) << "\n";
        }
        trigger_change(node);
    }
    warn_existing_users(nodes);
}

void NodeHider::release() noexcept {
    if (device_.empty()) return;
    const std::string device = std::exchange(device_, {});
    try {
        auto& devices = hidden_devices();
        auto it = devices.find(device);
        if (it == devices.end() || --it->second > 0) return;
        devices.erase(it);

        write_rules();
        // Re-apply the normal mode and uaccess ACL (if the device still exists)
        for (const auto& node : device_nodes(device)) trigger_change(node);
    } catch (...) {
    }
}

void NodeHider::clear_stale_rules() {
    std::error_code ec;
    if (!fs::exists(kRulesFile, ec)) return;
    std::cerr << "[ds4linux] Removing controller-hiding rules left by a previous run\n";
    fs::remove(kRulesFile, ec);
    run({"udevadm", "control", "--timeout=5", "--reload"});
    run({"udevadm", "trigger", "--settle", "--action=change",
         "--subsystem-match=input", "--subsystem-match=hidraw"});
}

} // namespace ds4linux::daemon

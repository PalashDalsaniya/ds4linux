// ds4linux::daemon — NodeHider

#include "ds4linux/node_hider.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

/// Warn about processes that opened the node before it was hidden: they
/// keep their file descriptor and still see the physical controller.
static void warn_existing_users(const std::string& devnode) {
    const std::string self = std::to_string(::getpid());
    std::error_code ec;
    for (const auto& proc : fs::directory_iterator("/proc", ec)) {
        const auto pid = proc.path().filename().string();
        if (pid == self || pid.find_first_not_of("0123456789") != std::string::npos) continue;

        std::error_code fd_ec;
        for (const auto& fd : fs::directory_iterator(proc.path() / "fd", fd_ec)) {
            std::error_code link_ec;
            if (fs::read_symlink(fd.path(), link_ec) != devnode) continue;

            std::string comm;
            std::ifstream(proc.path() / "comm") >> comm;
            std::cerr << "[ds4linux] " << (comm.empty() ? "a process" : comm)
                      << " (pid " << pid << ") already has " << devnode
                      << " open and will still see the physical controller; "
                         "restart it to avoid a duplicate pad\n";
            break;
        }
    }
}

NodeHider::~NodeHider() { release_all(); }

NodeHider::NodeHider(NodeHider&& other) noexcept
    : nodes_(std::exchange(other.nodes_, {})) {}

NodeHider& NodeHider::operator=(NodeHider&& other) noexcept {
    if (this != &other) {
        release_all();
        nodes_ = std::exchange(other.nodes_, {});
    }
    return *this;
}

void NodeHider::hide(const std::string& devnode) {
    struct stat st{};
    if (::stat(devnode.c_str(), &st) < 0 || !S_ISCHR(st.st_mode)) return;
    for (const auto& n : nodes_) {
        if (n.rdev == st.st_rdev) return;  // already hidden
    }

    if (::chmod(devnode.c_str(), 0600) < 0) {
        std::cerr << "[ds4linux] Could not hide " << devnode << ": "
                  << std::strerror(errno) << "\n";
        return;
    }
    // Drop the uaccess ACL that gives the logged-in user access
    if (::removexattr(devnode.c_str(), "system.posix_acl_access") < 0 &&
        errno != ENODATA && errno != ENOTSUP) {
        std::cerr << "[ds4linux] Could not remove the ACL of " << devnode << ": "
                  << std::strerror(errno) << "\n";
    }
    nodes_.push_back({devnode, st.st_rdev});
    warn_existing_users(devnode);
}

void NodeHider::hide_hidraw_of(const std::string& hid_sysfs_path) {
    if (hid_sysfs_path.empty()) return;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(hid_sysfs_path) / "hidraw", ec)) {
        hide("/dev/" + e.path().filename().string());
    }
}

void NodeHider::release(const std::string& devnode) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
                           [&](const Node& n) { return n.path == devnode; });
    if (it == nodes_.end()) return;
    restore(*it);
    nodes_.erase(it);
}

/// Ask udev to re-apply the node's normal mode and ACLs.  Only if the path
/// still names the same device: a node that went away needs nothing, and
/// its name may already belong to a new device.
void NodeHider::restore(const Node& n) {
    struct stat st{};
    if (::stat(n.path.c_str(), &st) < 0 || st.st_rdev != n.rdev) return;
    const auto uevent = "/sys/dev/char/" + std::to_string(major(n.rdev)) + ":" +
                        std::to_string(minor(n.rdev)) + "/uevent";
    std::ofstream(uevent) << "change";
}

void NodeHider::release_all() noexcept {
    for (const auto& n : nodes_) {
        try {
            restore(n);
        } catch (...) {
        }
    }
    nodes_.clear();
}

} // namespace ds4linux::daemon

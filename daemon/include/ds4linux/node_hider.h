#pragma once
// ds4linux::daemon — hide a physical controller from other programs while in use
//
// While the daemon drives a controller, games and Steam must only see the
// virtual one.  The evdev nodes are grabbed (EVIOCGRAB), but hidraw nodes
// cannot be, so all of the controller's nodes are made root-only for as
// long as the daemon uses it.
//
// This is done through udev rather than by changing the nodes behind its
// back: a runtime rule (/run/udev/rules.d) matching everything below the
// controller's HID device sets MODE 0600 and drops the "uaccess" tag.
// Without the tag, systemd-logind doesn't re-grant the logged-in user's ACL
// on login or seat changes, and nodes the kernel creates later are hidden
// as they appear.  Releasing the controller removes its rule and asks udev
// to re-process the nodes, which restores their normal mode and ACL.
//
// The rule lives in /run, so it never survives a reboot; the service's
// ExecStopPost removes it after a crash, and a new daemon clears stale rules.

#include <string>

namespace ds4linux::daemon {

class NodeHider {
public:
    NodeHider() = default;
    ~NodeHider();                      // releases the hidden device, if any

    NodeHider(NodeHider&& other) noexcept;
    NodeHider& operator=(NodeHider&& other) noexcept;
    NodeHider(const NodeHider&) = delete;
    NodeHider& operator=(const NodeHider&) = delete;

    /// Hide every node below the device at `sysfs_path` (the controller's
    /// HID device, or its parent for a generic pad).  Several hiders may
    /// share a device; it is released when the last one goes.
    void hide(const std::string& sysfs_path);

    /// Remove rules left behind by a daemon that didn't exit cleanly.
    /// Call once at startup, before any controller is opened.
    static void clear_stale_rules();

private:
    void release() noexcept;

    std::string device_;  // sysfs path, empty = nothing hidden
};

} // namespace ds4linux::daemon

#pragma once
// ds4linux::daemon — hide a physical controller's device nodes while in use
//
// While the daemon drives a controller, games and Steam must only see the
// virtual one.  The evdev nodes are grabbed (EVIOCGRAB), but hidraw nodes
// cannot be, so every node of the controller is made root-only (mode 0600,
// no ACL) for as long as the daemon uses it.  Releasing a node asks udev to
// re-apply its normal permissions (a "change" uevent), which also restores
// the logged-in user's uaccess ACL, so the raw controller works again as
// soon as the daemon stops or lets go of it.

#include <string>
#include <sys/types.h>
#include <vector>

namespace ds4linux::daemon {

class NodeHider {
public:
    NodeHider() = default;
    ~NodeHider();                      // releases every hidden node

    NodeHider(NodeHider&& other) noexcept;
    NodeHider& operator=(NodeHider&& other) noexcept;
    NodeHider(const NodeHider&) = delete;
    NodeHider& operator=(const NodeHider&) = delete;

    /// Hide one device node (/dev/input/eventN, /dev/hidrawN).
    void hide(const std::string& devnode);

    /// Hide the hidraw node(s) of a HID device, given its sysfs path.
    void hide_hidraw_of(const std::string& hid_sysfs_path);

    /// Release one node early (e.g. a touchpad node that went away).
    void release(const std::string& devnode);

private:
    struct Node {
        std::string path;
        dev_t       rdev = 0;
    };
    static void restore(const Node& n);
    void release_all() noexcept;

    std::vector<Node> nodes_;
};

} // namespace ds4linux::daemon

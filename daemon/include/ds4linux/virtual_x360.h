#pragma once
// ds4linux::daemon — Virtual Xbox 360 controller (uinput, xpad-compatible)
//
// Mirrors the evdev device the kernel's xpad driver creates for a wired
// Xbox 360 pad (045e:028e), so SDL, Steam and Wine recognise it as one.
// Rumble from applications arrives as force-feedback effects.

#include "ds4linux/virtual_device.h"

#include <memory>

namespace ds4linux::daemon {

class VirtualX360 final : public VirtualOutput {
public:
    /// Create the uinput device.  Throws std::system_error on failure.
    VirtualX360();
    ~VirtualX360() override;

    VirtualX360(const VirtualX360&) = delete;
    VirtualX360& operator=(const VirtualX360&) = delete;

    void send_report(const DS4InputState& state) override;
    RumbleOutput take_output() override;
    [[nodiscard]] int output_fd() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon

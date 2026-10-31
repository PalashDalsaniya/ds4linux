#pragma once
// ds4linux::daemon — IPC request handler: turns JSON requests from the CLI /
// GUI into DeviceManager and ProfileManager calls.

#include <string>

namespace ds4linux::daemon {

class DeviceManager;
class ProfileManager;

class IpcHandler {
public:
    IpcHandler(DeviceManager& devices, ProfileManager& profiles);

    /// Handle one request and return the JSON response.  Never throws:
    /// failures become {"type":"Error","message":...}.
    [[nodiscard]] std::string handle(const std::string& request);

private:
    DeviceManager&  devices_;
    ProfileManager& profiles_;
};

} // namespace ds4linux::daemon

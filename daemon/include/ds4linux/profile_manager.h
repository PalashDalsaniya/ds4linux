#pragma once
// ds4linux::daemon — Profile manager: load / save / list profiles on disk.

#include <ds4linux/profile.h>

#include <string>
#include <vector>

namespace ds4linux::daemon {

class ProfileManager {
public:
    ProfileManager();

    /// Ensure the profile directory exists.
    void init_directories();

    /// Load a profile by name (without .json extension).
    [[nodiscard]] Profile load(const std::string& name) const;

    /// Save a profile (writes <profiles_dir>/<name>.json).
    void save(const Profile& p) const;

    /// List all available profile names.
    [[nodiscard]] std::vector<std::string> list() const;

    /// Delete a profile by name.
    bool remove(const std::string& name) const;

    /// Get / set the currently active profile name.
    [[nodiscard]] const std::string& active_name() const noexcept { return active_; }
    void set_active(const std::string& name) { active_ = name; }

private:
    std::string active_ = "Default";
};

} // namespace ds4linux::daemon

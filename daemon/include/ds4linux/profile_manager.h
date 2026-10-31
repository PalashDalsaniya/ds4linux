#pragma once
// ds4linux::daemon — Profile manager: load / save / list profiles on disk and
// remember which profile each controller uses.

#include <ds4linux/profile.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace ds4linux::daemon {

class ProfileManager {
public:
    /// Profiles live in <state_dir>/profiles, assignments in
    /// <state_dir>/assignments.json.  Never throws; problems are logged.
    explicit ProfileManager(std::string state_dir);

    /// Load a profile by name.  "Default" always exists (built in if no
    /// file is present).  Throws std::runtime_error if not found / invalid.
    [[nodiscard]] Profile load(const std::string& name) const;

    /// Save a profile (atomically writes <profiles_dir>/<name>.json).
    /// Throws std::runtime_error on an invalid name, when creating it would
    /// exceed the profile limit, or on I/O failure.
    void save(const Profile& p) const;

    /// List all available profile names (always includes "Default").
    [[nodiscard]] std::vector<std::string> list() const;

    /// Delete a profile by name.  The Default profile cannot be deleted.
    /// Throws std::runtime_error on failure.
    void remove(const std::string& name);

    /// Profile assigned to a controller (by stable key, e.g. its MAC).
    [[nodiscard]] std::string assigned(const std::string& controller_key) const;

    /// Assign a profile to a controller and persist the assignment.
    void assign(const std::string& controller_key, const std::string& profile);

    /// Profile for a controller, falling back to Default if the assigned
    /// profile is missing or unreadable.
    [[nodiscard]] Profile profile_for(const std::string& controller_key) const;

private:
    [[nodiscard]] std::string path_for(const std::string& name) const;
    void load_assignments();
    void save_assignments() const;

    std::string profiles_dir_;
    std::string assignments_path_;
    std::unordered_map<std::string, std::string> assignments_;
};

} // namespace ds4linux::daemon

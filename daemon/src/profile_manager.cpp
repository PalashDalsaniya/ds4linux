// ds4linux::daemon — ProfileManager implementation

#include "ds4linux/profile_manager.h"
#include "ds4linux/constants.h"
#include "ds4linux/file_util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ds4linux::daemon {

// Clients reach the daemon over a world-writable socket; bound how many
// files they can make it (root) write.
static constexpr std::size_t kMaxProfiles = 256;

ProfileManager::ProfileManager(std::string state_dir)
    : profiles_dir_((fs::path(state_dir) / kProfilesDirName).string()),
      assignments_path_((fs::path(state_dir) / kAssignmentsFile).string())
{
    std::error_code ec;
    fs::create_directories(profiles_dir_, ec);
    if (ec) {
        std::cerr << "[ds4linux] Cannot create profile directory " << profiles_dir_
                  << ": " << ec.message() << " (profiles cannot be saved)\n";
    }
    load_assignments();
}

std::string ProfileManager::path_for(const std::string& name) const {
    if (!is_valid_profile_name(name)) {
        throw std::runtime_error("Invalid profile name: \"" + name + "\"");
    }
    return (fs::path(profiles_dir_) / (name + ".json")).string();
}

Profile ProfileManager::load(const std::string& name) const {
    const auto path = path_for(name);
    if (!fs::exists(path)) {
        if (name == kDefaultProfile) return Profile{};
        throw std::runtime_error("Profile not found: " + name);
    }
    Profile p = profile_from_json(read_file(path));
    p.name = name;  // the file name is authoritative
    return p;
}

void ProfileManager::save(const Profile& p) const {
    const auto path = path_for(p.name);
    if (!fs::exists(path) && list().size() >= kMaxProfiles) {
        throw std::runtime_error("Too many profiles (limit " +
                                 std::to_string(kMaxProfiles) + ")");
    }
    write_file_atomic(path, profile_to_json(p));
}

std::vector<std::string> ProfileManager::list() const {
    std::vector<std::string> names{std::string(kDefaultProfile)};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(profiles_dir_, ec)) {
        if (entry.path().extension() != ".json") continue;
        auto name = entry.path().stem().string();
        if (is_valid_profile_name(name) && name != kDefaultProfile) names.push_back(name);
    }
    std::sort(names.begin() + 1, names.end());
    return names;
}

void ProfileManager::remove(const std::string& name) {
    if (name == kDefaultProfile) {
        throw std::runtime_error("The Default profile cannot be deleted");
    }
    std::error_code ec;
    if (!fs::remove(path_for(name), ec)) {
        throw std::runtime_error("Profile not found: " + name);
    }
    if (std::erase_if(assignments_, [&](const auto& a) { return a.second == name; })) {
        save_assignments();
    }
}

std::string ProfileManager::assigned(const std::string& controller_key) const {
    auto it = assignments_.find(controller_key);
    return it != assignments_.end() ? it->second : std::string(kDefaultProfile);
}

void ProfileManager::assign(const std::string& controller_key, const std::string& profile) {
    if (profile == kDefaultProfile) assignments_.erase(controller_key);
    else assignments_[controller_key] = profile;
    save_assignments();
}

Profile ProfileManager::profile_for(const std::string& controller_key) const {
    const auto name = assigned(controller_key);
    try {
        return load(name);
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] " << ex.what() << " — using Default\n";
        try { return load(std::string(kDefaultProfile)); } catch (...) { return Profile{}; }
    }
}

void ProfileManager::load_assignments() {
    if (!fs::exists(assignments_path_)) return;
    try {
        auto j = json::parse(read_file(assignments_path_));
        for (const auto& [key, val] : j.items()) {
            if (val.is_string() && is_valid_profile_name(val.get<std::string>())) {
                assignments_[key] = val.get<std::string>();
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] Ignoring unreadable " << assignments_path_ << ": "
                  << ex.what() << "\n";
    }
}

void ProfileManager::save_assignments() const {
    json j = json::object();
    for (const auto& [key, name] : assignments_) j[key] = name;
    try {
        write_file_atomic(assignments_path_, j.dump(4));
    } catch (const std::exception& ex) {
        std::cerr << "[ds4linux] " << ex.what() << "\n";
    }
}

} // namespace ds4linux::daemon

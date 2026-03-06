// ds4linux::daemon — ProfileManager implementation

#include "ds4linux/profile_manager.h"
#include "ds4linux/constants.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ds4linux::daemon {

ProfileManager::ProfileManager() {
    init_directories();
}

void ProfileManager::init_directories() {
    auto dir = profiles_dir();
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

    // Write default profile if it doesn't exist
    auto def_path = fs::path(dir) / kDefaultProfileName;
    if (!fs::exists(def_path)) {
        Profile def;
        std::ofstream ofs(def_path);
        ofs << profile_to_json(def);
    }
}

Profile ProfileManager::load(const std::string& name) const {
    auto path = fs::path(profiles_dir()) / (name + ".json");
    if (!fs::exists(path)) {
        throw std::runtime_error("Profile not found: " + name);
    }
    std::ifstream ifs(path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return profile_from_json(content);
}

void ProfileManager::save(const Profile& p) const {
    auto path = fs::path(profiles_dir()) / (p.name + ".json");
    std::ofstream ofs(path);
    if (!ofs) {
        throw std::runtime_error("Cannot write profile: " + path.string());
    }
    ofs << profile_to_json(p);
}

std::vector<std::string> ProfileManager::list() const {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(profiles_dir())) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

bool ProfileManager::remove(const std::string& name) const {
    auto path = fs::path(profiles_dir()) / (name + ".json");
    return fs::remove(path);
}

} // namespace ds4linux::daemon

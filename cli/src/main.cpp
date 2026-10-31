// ds4linux-ctl — command-line control for the ds4linux daemon
//
// Talks to the daemon over its Unix socket (length-prefixed JSON, see
// common/include/ds4linux/ipc_protocol.h).

#include "ds4linux/constants.h"
#include "ds4linux/ipc_protocol.h"
#include "ds4linux/profile.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;
using namespace ds4linux;

namespace {

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

volatile std::sig_atomic_t g_stop = 0;

void print_usage() {
    std::cout <<
R"(Usage: ds4linux-ctl [--socket PATH] [-d DEVICE] <command> [args]

Controllers (DEVICE = number from `list`, or an evdev path; default: all):
  list                               Connected controllers, battery, profile
  rescan                             Look for newly connected controllers
  lightbar <#RRGGBB | R G B>         Set the lightbar colour until the next profile change
  rumble <HEAVY> <LIGHT> [MS]        Test rumble (0-255 each, default 500 ms)
  test                               Live view of a controller's input (Ctrl+C to stop)

Profiles:
  profile list                       Available profiles
  profile show <NAME>                Print a profile as JSON
  profile use <NAME>                 Use a profile on the selected controller(s)
  profile new <NAME> [--from BASE]   Create a profile (copy of Default or BASE)
  profile delete <NAME>              Delete a profile
  profile set <NAME> <KEY> <VALUE>   Change a setting:
      output        ds4 | x360
      lightbar      #RRGGBB
      rumble        0-100 (percent)
      left-stick, right-stick        INNER[,OUTER[,circular|square|none]] (percent)
      l2, r2                         INNER[,OUTER] trigger deadzone (percent)
  profile remap <NAME> <FROM> <TO>   Remap a button (TO may be "none" to disable)
  profile unmap <NAME> <FROM>        Remove a remap
  profile import <FILE>              Save a profile from a JSON file
  profile export <NAME> [FILE]       Write a profile as JSON (stdout by default)

Buttons: cross circle square triangle l1 r1 l2 r2 share options l3 r3 ps
         touchpad dpad_up dpad_down dpad_left dpad_right

Other:
  ping                               Check that the daemon is running
)";
}

// ── Daemon connection ────────────────────────────────────────────────────────

class Client {
public:
    explicit Client(const std::string& path) {
        struct sockaddr_un addr{};
        if (path.size() >= sizeof(addr.sun_path)) {
            throw std::runtime_error("Socket path too long: " + path);
        }
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0) throw std::runtime_error(std::strerror(errno));
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            const int err = errno;
            ::close(fd_);
            throw std::runtime_error("Cannot connect to the daemon at " + path + " (" +
                                     std::strerror(err) + "). Is ds4linux-daemon running?");
        }
    }
    ~Client() { if (fd_ >= 0) ::close(fd_); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// Send a request and wait for its response.  Throws on daemon errors.
    json request(json req) {
        const auto frame = ipc::encode_message(req.dump());
        std::size_t sent = 0;
        while (sent < frame.size()) {
            ssize_t n = ::send(fd_, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) throw std::runtime_error("Lost connection to the daemon");
            sent += static_cast<std::size_t>(n);
        }
        for (;;) {
            if (auto msg = ipc::decode_message(buf_)) {
                json resp = json::parse(*msg);
                if (resp.value("type", "") == "Error") {
                    throw std::runtime_error(resp.value("message", "daemon error"));
                }
                return resp;
            }
            std::uint8_t tmp[4096];
            ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
            if (n < 0 && errno == EINTR) {
                if (g_stop) throw std::runtime_error("interrupted");
                continue;
            }
            if (n <= 0) throw std::runtime_error("Lost connection to the daemon");
            buf_.insert(buf_.end(), tmp, tmp + n);
        }
    }

private:
    int fd_ = -1;
    std::vector<std::uint8_t> buf_;
};

// ── Argument helpers ─────────────────────────────────────────────────────────

int parse_int(const std::string& s, int lo, int hi, const char* what) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used == s.size() && v >= lo && v <= hi) return v;
    } catch (...) {}
    throw UsageError(std::string(what) + " must be a number from " +
                     std::to_string(lo) + " to " + std::to_string(hi));
}

std::array<int, 3> parse_hex_color(std::string s) {
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    unsigned v = 0;
    char tail = 0;
    if (s.size() != 6 || std::sscanf(s.c_str(), "%6x%c", &v, &tail) != 1) {
        throw UsageError("Colour must look like #RRGGBB");
    }
    return {static_cast<int>(v >> 16), static_cast<int>((v >> 8) & 0xFF),
            static_cast<int>(v & 0xFF)};
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) out.push_back(item);
    return out;
}

void require_button(const std::string& name, bool allow_none) {
    auto b = ds4_button_from_string(name);
    if (!b || (!allow_none && *b == DS4Button::None)) {
        throw UsageError("Unknown button: " + name);
    }
}

std::string read_text_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("Cannot read " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// ── Commands ─────────────────────────────────────────────────────────────────

void cmd_list(Client& c) {
    const auto devs = c.request({{"type", "ListDevices"}})["devices"];
    if (devs.empty()) {
        std::cout << "No controllers connected.\n";
        return;
    }
    int idx = 1;
    for (const auto& d : devs) {
        const int pct = d["battery"]["percent"].get<int>();
        std::cout << idx++ << ". " << d["model"].get<std::string>()
                  << " (" << d["connection"].get<std::string>() << ")  "
                  << d["path"].get<std::string>() << "\n"
                  << "   battery: "
                  << (pct >= 0 ? std::to_string(pct) + "% " + d["battery"]["status"].get<std::string>()
                               : std::string("n/a"))
                  << "   profile: " << d["profile"].get<std::string>()
                  << "   output: " << d["output_mode"].get<std::string>()
                  << "   touchpad: " << (d["touchpad"].get<bool>() ? "yes" : "no")
                  << "   motion: "   << (d["motion"].get<bool>() ? "yes" : "no") << "\n";
    }
}

/// Fetch, modify and save a profile.
template <typename F>
void edit_profile(Client& c, const std::string& name, F&& change) {
    json p = c.request({{"type", "LoadProfile"}, {"name", name}})["profile"];
    change(p);
    p["name"] = name;
    c.request({{"type", "SaveProfile"}, {"profile", p}});
}

json parse_deadzone(const std::string& value, bool stick, const json& current) {
    const auto parts = split(value, ',');
    if (parts.empty() || parts.size() > (stick ? 3u : 2u)) {
        throw UsageError(stick ? "Use INNER[,OUTER[,circular|square|none]]"
                               : "Use INNER[,OUTER]");
    }
    json dz = current.is_object() ? current : json::object();
    dz["inner"] = parse_int(parts[0], 0, 95, "Inner deadzone") / 100.0;
    if (parts.size() > 1) dz["outer"] = parse_int(parts[1], 5, 100, "Outer deadzone") / 100.0;
    else if (!dz.contains("outer")) dz["outer"] = 1.0;
    if (stick && parts.size() > 2) {
        const auto& t = parts[2];
        if (t == "none") dz["type"] = 0;
        else if (t == "circular") dz["type"] = 1;
        else if (t == "square") dz["type"] = 2;
        else throw UsageError("Deadzone type must be circular, square or none");
    } else if (!stick) {
        dz["type"] = dz["inner"].get<double>() > 0 || dz["outer"].get<double>() < 1 ? 1 : 0;
    } else if (!dz.contains("type") || dz["type"] == 0) {
        dz["type"] = 1;
    }
    return dz;
}

void cmd_profile(Client& c, const std::vector<std::string>& a, const json& device) {
    if (a.empty()) throw UsageError("Missing profile command");
    const auto& sub = a[0];
    auto need = [&](std::size_t n) {
        if (a.size() < n + 1) throw UsageError("Not enough arguments for profile " + sub);
    };

    if (sub == "list") {
        const json names = c.request({{"type", "ListProfiles"}})["profiles"];
        for (const auto& n : names) {
            std::cout << n.get<std::string>() << "\n";
        }
    } else if (sub == "show") {
        need(1);
        std::cout << c.request({{"type", "LoadProfile"}, {"name", a[1]}})["profile"].dump(4) << "\n";
    } else if (sub == "use") {
        need(1);
        json req = {{"type", "SetActiveProfile"}, {"name", a[1]}};
        if (!device.is_null()) req["device"] = device;
        c.request(req);
        std::cout << "Using profile \"" << a[1] << "\"\n";
    } else if (sub == "new") {
        need(1);
        std::string base(kDefaultProfile);
        if (a.size() >= 4 && a[2] == "--from") base = a[3];
        else if (a.size() != 2) throw UsageError("Use: profile new <NAME> [--from BASE]");
        const auto names = c.request({{"type", "ListProfiles"}})["profiles"];
        for (const auto& n : names) {
            if (n == a[1]) throw std::runtime_error("Profile \"" + a[1] + "\" already exists");
        }
        json p = c.request({{"type", "LoadProfile"}, {"name", base}})["profile"];
        p["name"] = a[1];
        c.request({{"type", "SaveProfile"}, {"profile", p}});
        std::cout << "Created profile \"" << a[1] << "\" from \"" << base << "\"\n";
    } else if (sub == "delete") {
        need(1);
        c.request({{"type", "DeleteProfile"}, {"name", a[1]}});
        std::cout << "Deleted profile \"" << a[1] << "\"\n";
    } else if (sub == "set") {
        need(3);
        const auto& key = a[2];
        const auto& value = a[3];
        edit_profile(c, a[1], [&](json& p) {
            if (key == "output") {
                if (value == "ds4") p["output_mode"] = 0;
                else if (value == "x360") p["output_mode"] = 1;
                else throw UsageError("Output must be ds4 or x360");
            } else if (key == "lightbar") {
                const auto rgb = parse_hex_color(value);
                p["lightbar_color"] = {{"r", rgb[0]}, {"g", rgb[1]}, {"b", rgb[2]}};
            } else if (key == "rumble") {
                p["rumble_strength"] = parse_int(value, 0, 100, "Rumble") / 100.0;
            } else if (key == "left-stick" || key == "right-stick") {
                const char* k = key == "left-stick" ? "left_stick_dz" : "right_stick_dz";
                p[k] = parse_deadzone(value, true, p.value(k, json::object()));
            } else if (key == "l2" || key == "r2") {
                const char* k = key == "l2" ? "left_trigger_dz" : "right_trigger_dz";
                p[k] = parse_deadzone(value, false, p.value(k, json::object()));
            } else {
                throw UsageError("Unknown setting: " + key);
            }
        });
        std::cout << "Updated \"" << a[1] << "\"\n";
    } else if (sub == "remap") {
        need(3);
        require_button(a[2], false);
        require_button(a[3], true);
        edit_profile(c, a[1], [&](json& p) { p["button_map"][a[2]] = a[3]; });
        std::cout << "Remapped " << a[2] << " -> " << a[3] << " in \"" << a[1] << "\"\n";
    } else if (sub == "unmap") {
        need(2);
        require_button(a[2], false);
        edit_profile(c, a[1], [&](json& p) {
            if (p.contains("button_map")) p["button_map"].erase(a[2]);
        });
        std::cout << "Removed remap of " << a[2] << " in \"" << a[1] << "\"\n";
    } else if (sub == "import") {
        need(1);
        json p = json::parse(read_text_file(a[1]));
        c.request({{"type", "SaveProfile"}, {"profile", p}});
        std::cout << "Imported profile \"" << p.value("name", "") << "\"\n";
    } else if (sub == "export") {
        need(1);
        const auto text = c.request({{"type", "LoadProfile"}, {"name", a[1]}})["profile"].dump(4);
        if (a.size() > 2) {
            std::ofstream ofs(a[2]);
            if (!(ofs << text << "\n")) throw std::runtime_error("Cannot write " + a[2]);
        } else {
            std::cout << text << "\n";
        }
    } else {
        throw UsageError("Unknown profile command: " + sub);
    }
}

// ── Live tester ──────────────────────────────────────────────────────────────

std::string bar(int value, int max, int width) {
    const int filled = max > 0 ? value * width / max : 0;
    return "[" + std::string(static_cast<std::size_t>(filled), '#') +
           std::string(static_cast<std::size_t>(width - filled), '.') + "]";
}

std::string stick(int x, int y) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "x %4d  y %4d", x - 128, y - 128);
    return buf;
}

void print_state(const json& s, const char* title) {
    const auto& b = s["buttons"];
    std::string pressed;
    for (const auto& [name, val] : b.items()) {
        if (val.get<bool>()) pressed += name + " ";
    }
    const int dx = s["dpad"]["x"], dy = s["dpad"]["y"];
    if (dy < 0) pressed += "dpad_up ";
    if (dy > 0) pressed += "dpad_down ";
    if (dx < 0) pressed += "dpad_left ";
    if (dx > 0) pressed += "dpad_right ";

    std::printf("\033[1m%s\033[0m\033[K\n", title);
    std::printf("  left stick   %s     right stick  %s\033[K\n",
                stick(s["lx"], s["ly"]).c_str(), stick(s["rx"], s["ry"]).c_str());
    std::printf("  L2 %3d %s   R2 %3d %s\033[K\n",
                s["l2"].get<int>(), bar(s["l2"], 255, 20).c_str(),
                s["r2"].get<int>(), bar(s["r2"], 255, 20).c_str());
    std::printf("  buttons  %s\033[K\n", pressed.empty() ? "-" : pressed.c_str());
}

void cmd_test(Client& c, const json& device) {
    std::signal(SIGINT, [](int) { g_stop = 1; });
    json req = {{"type", "GetInputState"}};
    if (!device.is_null()) req["device"] = device;

    bool first = true;
    while (!g_stop) {
        json st;
        try {
            st = c.request(req);
        } catch (const std::exception&) {
            if (g_stop) break;
            throw;
        }
        if (first) std::printf("\033[2J");  // clear once the controller is known
        first = false;
        const auto& in = st["input"];
        std::printf("\033[H\033[1mds4linux controller test\033[0m — %s   (Ctrl+C to stop)\033[K\n\n",
                    st["path"].get<std::string>().c_str());
        print_state(in, "Input (physical controller)");
        std::printf("  gyro  %6d %6d %6d    accel %6d %6d %6d\033[K\n",
                    in["gyro"][0].get<int>(), in["gyro"][1].get<int>(), in["gyro"][2].get<int>(),
                    in["accel"][0].get<int>(), in["accel"][1].get<int>(), in["accel"][2].get<int>());
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& t = in["touches"][i];
            if (t["active"].get<bool>()) {
                std::printf("  touch %zu  x %4d  y %4d\033[K\n", i + 1,
                            t["x"].get<int>(), t["y"].get<int>());
            } else {
                std::printf("  touch %zu  -\033[K\n", i + 1);
            }
        }
        std::printf("\n");
        print_state(st["output"], "Output (after profile: deadzones, remaps)");
        std::printf("\033[J");
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string socket_path(kSocketPath);
    json device;  // null = all / default
    std::vector<std::string> args;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw UsageError("Missing value for " + a);
                return argv[++i];
            };
            if (a == "-h" || a == "--help") {
                print_usage();
                return 0;
            } else if (a == "--socket") {
                socket_path = value();
            } else if (a.rfind("--socket=", 0) == 0) {
                socket_path = a.substr(9);
            } else if (a == "-d" || a == "--device") {
                const auto v = value();
                if (!v.empty() && v.find_first_not_of("0123456789") == std::string::npos) {
                    device = parse_int(v, 1, 64, "Device number");
                } else {
                    device = v;
                }
            } else {
                args.push_back(a);
            }
        }
        if (args.empty()) {
            print_usage();
            return 2;
        }

        Client client(socket_path);
        const auto& cmd = args[0];
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        auto with_device = [&](json req) {
            if (!device.is_null()) req["device"] = device;
            return req;
        };

        if (cmd == "ping") {
            std::cout << "ds4linux daemon " << client.request({{"type", "Ping"}})["version"].get<std::string>()
                      << " is running\n";
        } else if (cmd == "list" || cmd == "status") {
            cmd_list(client);
        } else if (cmd == "rescan") {
            client.request({{"type", "RescanDevices"}});
            cmd_list(client);
        } else if (cmd == "lightbar") {
            std::array<int, 3> rgb{};
            if (rest.size() == 1) {
                rgb = parse_hex_color(rest[0]);
            } else if (rest.size() == 3) {
                for (int k = 0; k < 3; ++k) rgb[k] = parse_int(rest[k], 0, 255, "Colour value");
            } else {
                throw UsageError("Use: lightbar <#RRGGBB | R G B>");
            }
            client.request(with_device({{"type", "SetLightbarColor"},
                                        {"r", rgb[0]}, {"g", rgb[1]}, {"b", rgb[2]}}));
        } else if (cmd == "rumble") {
            if (rest.size() < 2 || rest.size() > 3) throw UsageError("Use: rumble <HEAVY> <LIGHT> [MS]");
            json req = {{"type", "SetRumble"},
                        {"heavy", parse_int(rest[0], 0, 255, "Heavy motor")},
                        {"light", parse_int(rest[1], 0, 255, "Light motor")}};
            if (rest.size() == 3) req["duration_ms"] = parse_int(rest[2], 0, 5000, "Duration");
            client.request(with_device(req));
        } else if (cmd == "test") {
            cmd_test(client, device);
        } else if (cmd == "profile") {
            cmd_profile(client, rest, device);
        } else {
            throw UsageError("Unknown command: " + cmd);
        }
    } catch (const UsageError& ex) {
        std::cerr << "ds4linux-ctl: " << ex.what() << "\n(see ds4linux-ctl --help)\n";
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "ds4linux-ctl: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

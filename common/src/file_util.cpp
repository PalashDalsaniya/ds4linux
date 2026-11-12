// ds4linux — file helpers

#include "ds4linux/file_util.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ds4linux {

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("Cannot read " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void write_file_atomic(const std::string& path, const std::string& content) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::trunc);
        if (!ofs) throw std::runtime_error("Cannot write " + tmp);
        ofs << content;
        if (!ofs.flush()) throw std::runtime_error("Cannot write " + tmp);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("Cannot write " + path);
    }
}

} // namespace ds4linux

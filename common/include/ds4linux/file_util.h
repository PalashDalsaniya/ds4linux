#pragma once
// ds4linux — small file helpers shared by the daemon and the CLI

#include <string>

namespace ds4linux {

/// Whole file as a string.  Throws std::runtime_error if it can't be read.
[[nodiscard]] std::string read_file(const std::string& path);

/// Write via a temporary file + rename, so a crash never leaves half a file.
/// Throws std::runtime_error on failure.
void write_file_atomic(const std::string& path, const std::string& content);

} // namespace ds4linux

#pragma once
// ds4linux — IPC protocol definitions
//
// Wire format (over Unix domain socket):
//   [ uint32_t length ][ JSON payload ]
//
// All messages are JSON objects with at minimum a "type" field.  Requests
// flow client (CLI / GUI) → daemon, responses daemon → client; the request
// types are documented in daemon/src/ipc_handler.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ds4linux::ipc {

// ── Serialisation helpers ────────────────────────────────────────────────────

/// Encode a JSON string into a length-prefixed wire frame.
[[nodiscard]] std::vector<std::uint8_t> encode_message(const std::string& json_payload);

/// Attempt to decode one complete message from a receive buffer.
/// On success, returns the JSON payload and erases consumed bytes from `buf`.
/// Returns std::nullopt if the buffer does not yet contain a full frame.
[[nodiscard]] std::optional<std::string> decode_message(std::vector<std::uint8_t>& buf);

} // namespace ds4linux::ipc

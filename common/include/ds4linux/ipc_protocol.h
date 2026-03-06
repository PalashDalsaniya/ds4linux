#pragma once
// ds4linux — IPC protocol definitions
//
// Wire format (over Unix domain socket):
//   [ uint32_t length ][ JSON payload ]
//
// All messages are JSON objects with at minimum a "type" field.
// Requests flow GUI → Daemon; Responses/Events flow Daemon → GUI.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ds4linux::ipc {

// ── Message types ────────────────────────────────────────────────────────────
enum class MessageType : std::uint8_t {
    // Requests (GUI → Daemon)
    Ping                  = 0x01,
    ListDevices           = 0x02,
    GetDeviceStatus       = 0x03,
    RescanDevices         = 0x04,
    SetLightbarColor      = 0x10,
    SetRumble             = 0x11,
    LoadProfile           = 0x20,
    SaveProfile           = 0x21,
    ListProfiles          = 0x22,
    SetActiveProfile      = 0x23,

    // Responses / Events (Daemon → GUI)
    Pong                  = 0x81,
    DeviceList            = 0x82,
    DeviceStatus          = 0x83,
    ProfileList           = 0xA2,
    Ok                    = 0xF0,
    Error                 = 0xFF,

    // Async events (Daemon → GUI)
    DeviceConnected       = 0xC0,
    DeviceDisconnected    = 0xC1,
    BatteryUpdate         = 0xC2,
};

// ── Serialisation helpers ────────────────────────────────────────────────────

/// Encode a JSON string into a length-prefixed wire frame.
[[nodiscard]] std::vector<std::uint8_t> encode_message(const std::string& json_payload);

/// Attempt to decode one complete message from a receive buffer.
/// On success, returns the JSON payload and erases consumed bytes from `buf`.
/// Returns std::nullopt if the buffer does not yet contain a full frame.
[[nodiscard]] std::optional<std::string> decode_message(std::vector<std::uint8_t>& buf);

/// Convert MessageType to its string name (for JSON "type" field).
[[nodiscard]] const char* message_type_to_string(MessageType t) noexcept;

/// Parse a string back to MessageType. Returns nullopt on unknown strings.
[[nodiscard]] std::optional<MessageType> string_to_message_type(const std::string& s) noexcept;

} // namespace ds4linux::ipc

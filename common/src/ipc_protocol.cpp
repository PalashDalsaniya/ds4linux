// ds4linux — IPC protocol implementation

#include "ds4linux/ipc_protocol.h"
#include "ds4linux/constants.h"

#include <cstring>
#include <stdexcept>

namespace ds4linux::ipc {

// ── Wire encoding ────────────────────────────────────────────────────────────

std::vector<std::uint8_t> encode_message(const std::string& json_payload) {
    const auto len = static_cast<std::uint32_t>(json_payload.size());
    if (len > kIpcMaxMessageBytes) {
        throw std::runtime_error("IPC message exceeds maximum size");
    }

    std::vector<std::uint8_t> frame(sizeof(len) + len);

    // Network byte order (big-endian) length prefix
    const std::uint32_t net_len = __builtin_bswap32(len);
    std::memcpy(frame.data(), &net_len, sizeof(net_len));
    std::memcpy(frame.data() + sizeof(net_len), json_payload.data(), len);
    return frame;
}

std::optional<std::string> decode_message(std::vector<std::uint8_t>& buf) {
    constexpr auto header_size = sizeof(std::uint32_t);

    if (buf.size() < header_size) {
        return std::nullopt;
    }

    std::uint32_t net_len = 0;
    std::memcpy(&net_len, buf.data(), sizeof(net_len));
    const std::uint32_t payload_len = __builtin_bswap32(net_len);

    if (payload_len > kIpcMaxMessageBytes) {
        throw std::runtime_error("IPC frame too large — possible corruption");
    }

    if (buf.size() < header_size + payload_len) {
        return std::nullopt; // incomplete frame
    }

    std::string payload(
        reinterpret_cast<const char*>(buf.data() + header_size),
        payload_len
    );

    buf.erase(buf.begin(), buf.begin() + static_cast<long>(header_size + payload_len));
    return payload;
}

// ── MessageType ↔ string mapping ─────────────────────────────────────────────

#define CASE_STR(e) case MessageType::e: return #e

const char* message_type_to_string(MessageType t) noexcept {
    switch (t) {
        CASE_STR(Ping);
        CASE_STR(ListDevices);
        CASE_STR(GetDeviceStatus);
        CASE_STR(RescanDevices);
        CASE_STR(SetLightbarColor);
        CASE_STR(SetRumble);
        CASE_STR(LoadProfile);
        CASE_STR(SaveProfile);
        CASE_STR(ListProfiles);
        CASE_STR(SetActiveProfile);
        CASE_STR(Pong);
        CASE_STR(DeviceList);
        CASE_STR(DeviceStatus);
        CASE_STR(ProfileList);
        CASE_STR(Ok);
        CASE_STR(Error);
        CASE_STR(DeviceConnected);
        CASE_STR(DeviceDisconnected);
        CASE_STR(BatteryUpdate);
    }
    return "Unknown";
}

#undef CASE_STR

std::optional<MessageType> string_to_message_type(const std::string& s) noexcept {
    #define CHECK(e) if (s == #e) return MessageType::e
    CHECK(Ping);
    CHECK(ListDevices);
    CHECK(GetDeviceStatus);
    CHECK(RescanDevices);
    CHECK(SetLightbarColor);
    CHECK(SetRumble);
    CHECK(LoadProfile);
    CHECK(SaveProfile);
    CHECK(ListProfiles);
    CHECK(SetActiveProfile);
    CHECK(Pong);
    CHECK(DeviceList);
    CHECK(DeviceStatus);
    CHECK(ProfileList);
    CHECK(Ok);
    CHECK(Error);
    CHECK(DeviceConnected);
    CHECK(DeviceDisconnected);
    CHECK(BatteryUpdate);
    #undef CHECK
    return std::nullopt;
}

} // namespace ds4linux::ipc

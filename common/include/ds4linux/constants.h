#pragma once
// ds4linux — shared constants
// All magic numbers and paths live here.

#include <cstdint>
#include <string_view>

namespace ds4linux {

// ── Version ──────────────────────────────────────────────────────────────────
inline constexpr std::string_view kVersion = "0.1.0";

// ── IPC ──────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kSocketPath = "/run/ds4linux.sock";
inline constexpr std::uint32_t    kIpcMaxMessageBytes = 64 * 1024; // 64 KiB

// ── USB Vendor / Product IDs ─────────────────────────────────────────────────
inline constexpr std::uint16_t kVidSony         = 0x054C;
inline constexpr std::uint16_t kPidDS4v1        = 0x05C4;  // DualShock 4 v1
inline constexpr std::uint16_t kPidDS4v2        = 0x09CC;  // DualShock 4 v2
inline constexpr std::uint16_t kPidDualSense    = 0x0CE6;  // DualSense
inline constexpr std::uint16_t kPidDualSenseEdge = 0x0DF2; // DualSense Edge

// ── Virtual device names ─────────────────────────────────────────────────────
inline constexpr std::string_view kVirtualDS4Name        = "DS4Linux Virtual DS4";
inline constexpr std::string_view kVirtualDS4TouchpadName = "DS4Linux Virtual DS4 Touchpad";
inline constexpr std::string_view kVirtualDS4MotionName   = "DS4Linux Virtual DS4 Motion Sensors";

// ── Profile paths ────────────────────────────────────────────────────────────
inline constexpr std::string_view kConfigDirName     = "ds4linux";
inline constexpr std::string_view kProfilesDirName   = "profiles";
inline constexpr std::string_view kDefaultProfileName = "default.json";

// ── Polling ──────────────────────────────────────────────────────────────────
inline constexpr int kEpollMaxEvents = 16;
inline constexpr int kEpollTimeoutMs = -1; // block until event

// ── Lightbar defaults ────────────────────────────────────────────────────────
inline constexpr std::uint8_t kDefaultLedR = 0x00;
inline constexpr std::uint8_t kDefaultLedG = 0x00;
inline constexpr std::uint8_t kDefaultLedB = 0xFF;

// ── DS4 axis ranges (real DualShock 4 / DualSense uses 0–255 for everything) ─
inline constexpr int kDS4StickMin   = 0;
inline constexpr int kDS4StickMax   = 255;
inline constexpr int kDS4TriggerMin = 0;
inline constexpr int kDS4TriggerMax = 255;

// ── DualSense touchpad (from hid-playstation kernel driver) ──────────────────
inline constexpr int kTouchpadMaxX      = 1919;
inline constexpr int kTouchpadMaxY      = 1079;
inline constexpr int kTouchpadMaxSlots  = 2;     // multi-touch: 2 fingers

// ── DualSense motion sensors (from hid-playstation kernel driver) ────────────
// Gyroscope: ±2000 deg/s, 16-bit signed → range approx -32768..32767
inline constexpr int kMotionGyroMin  = -32768;
inline constexpr int kMotionGyroMax  =  32767;
// Accelerometer: ±4g, 16-bit signed
inline constexpr int kMotionAccelMin = -32768;
inline constexpr int kMotionAccelMax =  32767;

} // namespace ds4linux

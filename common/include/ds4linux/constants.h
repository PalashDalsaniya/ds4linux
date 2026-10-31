#pragma once
// ds4linux — shared constants
// All magic numbers and paths live here.

#include <cstdint>
#include <string_view>

namespace ds4linux {

// ── Version ──────────────────────────────────────────────────────────────────
inline constexpr std::string_view kVersion = "0.1.1";

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
// UHID phys string of the virtual DS4.  USB-style so it looks like real
// hardware, but unique so the scanner can recognise and skip its own devices.
inline constexpr std::string_view kVirtualDS4Phys        = "usb-ds4linux-virtual/input0";

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

// ── Virtual DS4 touchpad (DS4 pad is 1920x942, from hid-sony / hid-playstation) ─
inline constexpr int kDS4TouchpadMaxX = 1919;
inline constexpr int kDS4TouchpadMaxY = 941;

// ── Virtual DS4 motion sensor scaling ────────────────────────────────────────
// Kernel drivers report motion axes normalised to their ABS resolution
// (hid-playstation: gyro 1024 units per deg/s, accel 8192 units per g).  The
// virtual DS4 reports raw values like real hardware: gyro at 16 units per
// deg/s (±2048 deg/s in int16) and accel at 8192 units per g.  The
// calibration feature report (0x02) encodes exactly these scales.
inline constexpr int kEvdevGyroPerDegS = 1024;  // fallback if resolution unset
inline constexpr int kDS4GyroPerDegS   = 16;
inline constexpr int kDS4AccelPerG     = 8192;

} // namespace ds4linux

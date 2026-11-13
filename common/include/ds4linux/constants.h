#pragma once
// ds4linux — shared constants
// All magic numbers and paths live here.

#include <cstdint>
#include <string_view>

namespace ds4linux {

// ── Version ──────────────────────────────────────────────────────────────────
// Set by CMake from project(VERSION ...), the single source of the version
#ifndef DS4LINUX_VERSION
#error "DS4LINUX_VERSION must be defined by the build (see common/CMakeLists.txt)"
#endif
inline constexpr std::string_view kVersion = DS4LINUX_VERSION;

// ── IPC ──────────────────────────────────────────────────────────────────────
inline constexpr std::string_view kSocketPath = "/run/ds4linux.sock";

/// systemd unit that runs the daemon (started on demand, not at boot)
inline constexpr std::string_view kServiceName = "ds4linux.service";
inline constexpr std::uint32_t    kIpcMaxMessageBytes = 64 * 1024; // 64 KiB

// ── USB Vendor / Product IDs ─────────────────────────────────────────────────
inline constexpr std::uint16_t kVidSony         = 0x054C;
inline constexpr std::uint16_t kPidDS4v1        = 0x05C4;  // DualShock 4 v1
inline constexpr std::uint16_t kPidDS4v2        = 0x09CC;  // DualShock 4 v2
inline constexpr std::uint16_t kPidDualSense    = 0x0CE6;  // DualSense
inline constexpr std::uint16_t kPidDualSenseEdge = 0x0DF2; // DualSense Edge

// ── Virtual DS4 identity ─────────────────────────────────────────────────────
// UHID phys string of the virtual DS4.  USB-style so it looks like real
// hardware, but unique so the scanner can recognise and skip its own devices.
inline constexpr std::string_view kVirtualDS4Phys        = "usb-ds4linux-virtual/input0";

// ── Daemon state (profiles, per-controller profile assignments) ─────────────
inline constexpr std::string_view kStateDir          = "/var/lib/ds4linux";
inline constexpr std::string_view kProfilesDirName   = "profiles";
inline constexpr std::string_view kAssignmentsFile   = "assignments.json";
inline constexpr std::string_view kDefaultProfile    = "Default";

// ── Polling ──────────────────────────────────────────────────────────────────
inline constexpr int kEpollMaxEvents = 16;
inline constexpr int kEpollTimeoutMs = 200; // also bounds timer latency

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

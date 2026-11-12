#pragma once
// ds4linux::daemon — debug logging switch

namespace ds4linux::daemon {

/// Log rumble / lightbar traffic (--debug).  Set once at startup, before any
/// device or I/O thread exists, and only read afterwards.
inline bool g_debug = false;

} // namespace ds4linux::daemon

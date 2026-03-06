cmake_minimum_required(VERSION 3.22)
project(ds4linux
    VERSION 0.1.0
    DESCRIPTION "DS4Windows clone for Linux — Sony controller bridge + profile manager"
    LANGUAGES CXX
)

# ── Global standards ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── CMake module path ─────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ── Build type default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Compiler hardening flags ──────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror=return-type
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ── Options ───────────────────────────────────────────────────────────────────
option(DS4LINUX_BUILD_GUI    "Build the Qt6 GUI client"       OFF)
option(DS4LINUX_BUILD_DAEMON "Build the backend daemon"       ON)

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(common)

if(DS4LINUX_BUILD_DAEMON)
    add_subdirectory(daemon)
endif()

if(DS4LINUX_BUILD_GUI)
    add_subdirectory(gui)
endif()

# ── Install targets (config, service) ────────────────────────────────────────
install(FILES config/ds4linux.service
    DESTINATION lib/systemd/system
)
install(FILES config/default_profile.json
    DESTINATION share/ds4linux
)

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
option(DS4LINUX_ENABLE_PACKAGING "Enable CPack configuration for DEB/RPM"  OFF)

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

# ── Packaging (DEB & RPM) ─────────────────────────────────────────────────────
if(DS4LINUX_ENABLE_PACKAGING)
    # Install system rules required for the package
    install(FILES config/99-ds4linux.rules
        DESTINATION lib/udev/rules.d
    )
    
    # Dynamically generate and install the uinput module load file
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/ds4linux.conf "uinput\n")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ds4linux.conf
        DESTINATION lib/modules-load.d
    )

    # CPack setup
    set(CPACK_PACKAGE_NAME "ds4linux")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Linux Sony controller bridge daemon")
    set(CPACK_PACKAGE_CONTACT "Palash Dalsaniya") 
    set(CPACK_PACKAGE_VENDOR "ds4linux")

    # Generate both DEB and RPM
    set(CPACK_GENERATOR "DEB;RPM")

    # DEB specific settings (Ubuntu/Debian/Mint)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libevdev2")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postinst")

    # RPM specific settings (Fedora/Bazzite)
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_RPM_PACKAGE_REQUIRES "libevdev")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postinst")

    include(CPack)
endif()
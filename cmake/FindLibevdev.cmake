# FindLibevdev.cmake
# Finds the libevdev library
#
# Exported targets:  Libevdev::Libevdev
# Exported variables:
#   Libevdev_FOUND
#   Libevdev_INCLUDE_DIRS
#   Libevdev_LIBRARIES

find_package(PkgConfig QUIET)
pkg_check_modules(PC_Libevdev QUIET libevdev)

find_path(Libevdev_INCLUDE_DIR
    NAMES libevdev/libevdev.h
    HINTS ${PC_Libevdev_INCLUDE_DIRS}
    PATH_SUFFIXES libevdev-1.0
)

find_library(Libevdev_LIBRARY
    NAMES evdev
    HINTS ${PC_Libevdev_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libevdev
    REQUIRED_VARS Libevdev_LIBRARY Libevdev_INCLUDE_DIR
)

if(Libevdev_FOUND AND NOT TARGET Libevdev::Libevdev)
    add_library(Libevdev::Libevdev UNKNOWN IMPORTED)
    set_target_properties(Libevdev::Libevdev PROPERTIES
        IMPORTED_LOCATION "${Libevdev_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Libevdev_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Libevdev_INCLUDE_DIR Libevdev_LIBRARY)

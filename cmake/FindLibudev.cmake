# FindLibudev.cmake
# Finds the libudev library
#
# Exported target: Libudev::Libudev

find_package(PkgConfig QUIET)
pkg_check_modules(PC_Libudev QUIET libudev)

find_path(Libudev_INCLUDE_DIR
    NAMES libudev.h
    HINTS ${PC_Libudev_INCLUDE_DIRS}
)

find_library(Libudev_LIBRARY
    NAMES udev
    HINTS ${PC_Libudev_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libudev
    REQUIRED_VARS Libudev_LIBRARY Libudev_INCLUDE_DIR
)

if(Libudev_FOUND AND NOT TARGET Libudev::Libudev)
    add_library(Libudev::Libudev UNKNOWN IMPORTED)
    set_target_properties(Libudev::Libudev PROPERTIES
        IMPORTED_LOCATION "${Libudev_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Libudev_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Libudev_INCLUDE_DIR Libudev_LIBRARY)

# daemon/ — ds4linux backend daemon

find_package(Libevdev REQUIRED)

add_executable(ds4linux-daemon
    src/main.cpp
    src/input_device.cpp
    src/virtual_device.cpp
    src/device_manager.cpp
)

target_include_directories(ds4linux-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ds4linux-daemon PRIVATE
    ds4linux::common
    Libevdev::Libevdev
)

target_compile_features(ds4linux-daemon PRIVATE cxx_std_20)

# Optimise for low latency in release builds
target_compile_options(ds4linux-daemon PRIVATE
    $<$<CONFIG:Release>:-march=native -flto>
)
target_link_options(ds4linux-daemon PRIVATE
    $<$<CONFIG:Release>:-flto>
)

install(TARGETS ds4linux-daemon DESTINATION bin)

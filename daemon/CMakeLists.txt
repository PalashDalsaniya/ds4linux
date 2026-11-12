# daemon/ — ds4linux backend daemon

find_package(Libevdev REQUIRED)
find_package(Libudev REQUIRED)

add_executable(ds4linux-daemon
    src/main.cpp
    src/input_device.cpp
    src/virtual_device.cpp
    src/device_manager.cpp
    src/virtual_x360.cpp
    src/mapping.cpp
    src/profile_manager.cpp
    src/ipc_server.cpp
    src/ipc_handler.cpp
)

target_include_directories(ds4linux-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ds4linux-daemon PRIVATE
    ds4linux::common
    Libevdev::Libevdev
    Libudev::Libudev
)

target_compile_features(ds4linux-daemon PRIVATE cxx_std_20)

# Optimise for low latency in release builds
target_compile_options(ds4linux-daemon PRIVATE
    $<$<CONFIG:Release>:-O3 -flto>
)
target_link_options(ds4linux-daemon PRIVATE
    $<$<CONFIG:Release>:-flto>
)

install(TARGETS ds4linux-daemon DESTINATION bin)

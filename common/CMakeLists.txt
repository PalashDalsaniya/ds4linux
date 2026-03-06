# common/ — shared library used by both daemon and GUI

add_library(ds4linux_common STATIC
    src/ipc_protocol.cpp
    src/profile.cpp
)

target_include_directories(ds4linux_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(nlohmann_json 3.11 REQUIRED)

target_link_libraries(ds4linux_common PUBLIC
    nlohmann_json::nlohmann_json
)

target_compile_features(ds4linux_common PUBLIC cxx_std_20)

# Alias so consumers can use ds4linux::common
add_library(ds4linux::common ALIAS ds4linux_common)

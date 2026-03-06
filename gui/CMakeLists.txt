# gui/ — ds4linux Qt6 frontend

set(CMAKE_AUTOMOC ON)   # Qt meta-object compiler
set(CMAKE_AUTORCC ON)   # Qt resource compiler

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(ds4linux-gui
    src/main.cpp
    src/main_window.cpp
    src/ipc_client.cpp
    src/device_widget.cpp
    src/led_picker.cpp
    src/profile_editor.cpp
    # Headers with Q_OBJECT — must be listed for AUTOMOC
    include/ds4linux/main_window.h
    include/ds4linux/ipc_client.h
    include/ds4linux/device_widget.h
    include/ds4linux/led_picker.h
    include/ds4linux/profile_editor.h
)

target_include_directories(ds4linux-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ds4linux-gui PRIVATE
    ds4linux::common
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

target_compile_features(ds4linux-gui PRIVATE cxx_std_20)

install(TARGETS ds4linux-gui DESTINATION bin)

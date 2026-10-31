# tests/ — unit and kernel-level tests (run with ctest)

find_package(Libevdev REQUIRED)

add_executable(mapping_test
    mapping_test.cpp
    ${PROJECT_SOURCE_DIR}/daemon/src/mapping.cpp
)
target_include_directories(mapping_test PRIVATE ${PROJECT_SOURCE_DIR}/daemon/include)
target_link_libraries(mapping_test PRIVATE ds4linux::common Libevdev::Libevdev)
add_test(NAME mapping COMMAND mapping_test)

add_executable(x360_test
    x360_test.cpp
    ${PROJECT_SOURCE_DIR}/daemon/src/virtual_x360.cpp
)
target_include_directories(x360_test PRIVATE ${PROJECT_SOURCE_DIR}/daemon/include)
target_link_libraries(x360_test PRIVATE ds4linux::common Libevdev::Libevdev pthread)
add_test(NAME x360_uinput COMMAND x360_test)
set_tests_properties(x360_uinput PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)

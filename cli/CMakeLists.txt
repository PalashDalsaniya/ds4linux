# cli/ — ds4linux-ctl command-line control tool

add_executable(ds4linux-ctl
    src/main.cpp
)

target_link_libraries(ds4linux-ctl PRIVATE
    ds4linux::common
)

target_compile_features(ds4linux-ctl PRIVATE cxx_std_20)

install(TARGETS ds4linux-ctl DESTINATION bin)

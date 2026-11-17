cmake_minimum_required(VERSION 3.22)
project(7th_sphere_portscan_rebuild LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure with a 64-bit Windows toolchain.")
endif()

add_executable(7th-sphere-portscan-rebuild WIN32
    src/main.cpp
)

target_compile_features(7th-sphere-portscan-rebuild PRIVATE cxx_std_17)
target_link_libraries(7th-sphere-portscan-rebuild PRIVATE ws2_32 comctl32 shell32)

if(MINGW)
    target_link_options(7th-sphere-portscan-rebuild PRIVATE
        -municode -static -static-libgcc -static-libstdc++
    )
endif()

set_target_properties(7th-sphere-portscan-rebuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist"
)

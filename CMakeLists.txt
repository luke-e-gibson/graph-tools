cmake_minimum_required(VERSION 3.16)

project(longest_path_editor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(longest-path-editor main.c)
target_link_libraries(longest-path-editor PRIVATE PkgConfig::SDL2 m)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(longest-path-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

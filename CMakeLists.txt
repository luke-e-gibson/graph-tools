cmake_minimum_required(VERSION 3.16)

project(longest_path_editor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(SDL2 CONFIG QUIET)
if (NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
endif()

add_executable(longest-path-editor main.c)
if (TARGET SDL2::SDL2)
    target_link_libraries(longest-path-editor PRIVATE SDL2::SDL2)
else()
    target_link_libraries(longest-path-editor PRIVATE PkgConfig::SDL2)
endif()

if (WIN32)
    target_link_libraries(longest-path-editor PRIVATE comdlg32)
else()
    target_link_libraries(longest-path-editor PRIVATE m)
endif()

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(longest-path-editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

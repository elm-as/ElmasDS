cmake_minimum_required(VERSION 3.24)

project(
    ElmasDS
    VERSION 1.0.1
    DESCRIPTION "Nintendo DS Emulator in strict C++20"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ELMASDS_BUILD_TESTS "Build ElmasDS unit and integration tests" ON)
option(ELMASDS_BUILD_FRONTEND "Build SDL3 + OpenGL 3.3 frontend executable" ON)
option(ELMASDS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on ElmasDS targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src)

if(ELMASDS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

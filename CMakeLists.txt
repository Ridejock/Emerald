cmake_minimum_required(VERSION 3.24)

project(Emerald
    VERSION 0.1.0
    DESCRIPTION "A small C++20 game engine built on SDL3"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Put all executables/DLLs in one place so the sandbox can find SDL3 at runtime on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(EMERALD_BUILD_SANDBOX "Build the sandbox example application" ON)
option(EMERALD_USE_IMGUI     "Enable Dear ImGui (SDL3 + SDL GPU backends)" OFF)
option(EMERALD_USE_ENTT      "Enable the EnTT entity-component system" OFF)
option(EMERALD_MATH_SIMD     "Use the SSE code paths of the math library on x86/x64" ON)
option(EMERALD_BUILD_TESTS   "Build the unit tests (run with ctest)" ON)
option(EMERALD_BUILD_BENCH   "Build the math micro-benchmark" OFF)

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED) # ThreadPool (pthreads on Linux/macOS)
include(cmake/Shaders.cmake)

# Warning flags for Emerald's own targets (not third-party code).
function(emerald_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Math library (header-only, include/Emerald/Math/)
# ---------------------------------------------------------------------------
add_library(EmeraldMath INTERFACE)
add_library(Emerald::Math ALIAS EmeraldMath)
target_include_directories(EmeraldMath INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(EmeraldMath INTERFACE cxx_std_20)
# Public, so every target including the math headers agrees on the implementation.
target_compile_definitions(EmeraldMath INTERFACE EMERALD_MATH_SIMD=$<BOOL:${EMERALD_MATH_SIMD}>)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
add_library(Emerald STATIC
    src/Core/Log.cpp
    src/Core/Window.cpp
    src/Core/Application.cpp
    src/Core/ThreadPool.cpp
    src/Memory/FrameArena.cpp
    src/Memory/TrackingResource.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/Shader.cpp
    src/Renderer/Pipeline.cpp
    src/Assets/Image.cpp
    src/Vendor/stb_image.cpp
    src/Vendor/stb_image_write.cpp
)
add_library(Emerald::Emerald ALIAS Emerald)

target_include_directories(Emerald PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Emerald
    PUBLIC  Emerald::Math SDL3::SDL3 spdlog::spdlog Threads::Threads
    PRIVATE Emerald::stb
)

target_compile_definitions(Emerald PUBLIC
    EMERALD_WITH_IMGUI=$<BOOL:${EMERALD_USE_IMGUI}>
    EMERALD_WITH_ENTT=$<BOOL:${EMERALD_USE_ENTT}>
)

if(EMERALD_USE_IMGUI)
    target_link_libraries(Emerald PUBLIC Emerald::imgui)
endif()
if(EMERALD_USE_ENTT)
    target_link_libraries(Emerald PUBLIC EnTT::EnTT)
endif()

emerald_set_warnings(Emerald)

# ---------------------------------------------------------------------------
# Sandbox
# ---------------------------------------------------------------------------
if(EMERALD_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

# ---------------------------------------------------------------------------
# Tests and benchmark
# ---------------------------------------------------------------------------
if(EMERALD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EMERALD_BUILD_BENCH)
    add_subdirectory(bench)
endif()

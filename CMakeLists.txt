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
option(EMERALD_USE_IMGUI     "Enable Dear ImGui (SDL3 + SDLRenderer3 backends)" OFF)
option(EMERALD_USE_ENTT      "Enable the EnTT entity-component system" OFF)

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
add_library(Emerald STATIC
    src/Core/Log.cpp
    src/Core/Window.cpp
    src/Core/Application.cpp
    src/Assets/Image.cpp
    src/Vendor/stb_image.cpp
)
add_library(Emerald::Emerald ALIAS Emerald)

target_include_directories(Emerald PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(Emerald
    PUBLIC  SDL3::SDL3 spdlog::spdlog
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

if(MSVC)
    target_compile_options(Emerald PRIVATE /W4 /permissive-)
else()
    target_compile_options(Emerald PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Sandbox
# ---------------------------------------------------------------------------
if(EMERALD_BUILD_SANDBOX)
    add_subdirectory(sandbox)
endif()

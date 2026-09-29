# Host-tool "superbuild" for the SDL_shadercross command line compiler.
#
# Emerald builds this as a separate CMake project (via ExternalProject, see cmake/Shaders.cmake)
# so that the heavy vendored dependencies (DirectXShaderCompiler / LLVM, SPIRV-Cross,
# SPIRV-Tools) never leak targets, flags or warnings into the engine build, and so that the
# tool is always built in Release no matter which engine preset is used.
#
# Output: <build>/bin/shadercross[.exe] with dxcompiler + dxil (.dll / .so / .dylib) copied next
# to it. Build the `shadercross_bundle` target (Shaders.cmake does): it builds the tool, copies
# the DXC libraries beside it and smoke-tests HLSL -> SPIR-V / DXIL.
cmake_minimum_required(VERSION 3.24)
project(EmeraldShadercross LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# $<1:...> keeps multi-config generators (Visual Studio) from adding a per-config subfolder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/bin>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/bin>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# Let the executable find libdxcompiler/libdxil next to itself.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_BUILD_RPATH "$ORIGIN")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(EMERALD_SDL3_TAG "release-3.4.16" CACHE STRING "SDL3 tag used for the shadercross tool")
set(EMERALD_SHADERCROSS_COMMIT "1ff05bec573988a98ef9e0260b4da44f512b8367"
    CACHE STRING "SDL_shadercross commit")

# --- SDL3 (static; the tool only needs SDL's core + GPU headers) -------------
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        ${EMERALD_SDL3_TAG}
    GIT_SHALLOW    TRUE
)

# --- SDL_shadercross (no releases yet: pinned to a commit) --------------------
# VENDORED pulls SPIRV-Cross, SPIRV-Tools and libsdl-org's DirectXShaderCompiler fork from
# git submodules, so HLSL -> SPIR-V / DXIL / MSL all work without any system packages.
set(SDLSHADERCROSS_VENDORED          ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_DXC               ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SHARED            OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC            ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI               ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI_STATIC        ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL           OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_TESTS             OFF CACHE BOOL "" FORCE)
# DXC only turns SPIR-V codegen on by itself when NOT WIN32 (DirectXShaderCompiler/CMakeLists.txt,
# "Enable SPIR-V CodeGen for Linux by default"); on Windows it relies on the cache value from
# cmake/caches/PredefinedParams.cmake. shadercross needs it on every platform: HLSL -> DXIL
# round-trips through SPIR-V (HLSL -> SPIR-V -> SPIRV-Cross HLSL -> DXIL), so force it.
set(ENABLE_SPIRV_CODEGEN             ON  CACHE BOOL "" FORCE)
FetchContent_Declare(SDL_shadercross
    GIT_REPOSITORY    https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG           ${EMERALD_SHADERCROSS_COMMIT}
    GIT_SUBMODULES_RECURSE TRUE
)

FetchContent_MakeAvailable(SDL3 SDL_shadercross)

# --- Make bin/ self-contained and verify it -----------------------------------
# DXC overrides CMAKE_RUNTIME_OUTPUT_DIRECTORY for its own targets (LLVM_BINARY_DIR/bin, plus a
# per-config folder with Visual Studio), so dxcompiler.dll / dxil.dll do NOT end up next to
# shadercross.exe. Windows has no RPATH: the loader then silently picks up some other
# dxcompiler.dll (Windows SDK bin dir on a VS Developer Prompt PATH, System32, ...) that was
# built without SPIR-V codegen, and every DXIL compile fails with "SPIR-V CodeGen not
# available". Copying both libraries beside the exe makes the application directory, which
# Windows searches first, provide the right ones. (Harmless on Linux/macOS, where RPATH
# $ORIGIN picks up the copies.)
add_custom_target(shadercross_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dxcompiler> $<TARGET_FILE:dxildll> $<TARGET_FILE_DIR:shadercross>
    COMMAND ${CMAKE_COMMAND}
            -DSHADERCROSS=$<TARGET_FILE:shadercross>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/probe
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Probe.cmake
    DEPENDS shadercross dxcompiler dxildll
    COMMENT "Copying DXC libraries next to shadercross and smoke-testing it"
    VERBATIM)

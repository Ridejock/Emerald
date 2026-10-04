# Third-party dependencies, fetched and pinned via FetchContent.
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# --- SDL3 -------------------------------------------------------------------
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
# Gamepads: joystick + HIDAPI (direct PS4/PS5/Switch drivers) are SDL's defaults; forced on so a
# cached or inherited setting cannot silently drop controller support from the static build.
set(SDL_JOYSTICK       ON  CACHE BOOL "" FORCE)
set(SDL_HIDAPI         ON  CACHE BOOL "" FORCE)
set(SDL_HIDAPI_JOYSTICK ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)

# --- spdlog -----------------------------------------------------------------
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
)

# --- stb (header-only, no releases: pinned to a commit) ----------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
)

# --- dr_libs (single-header decoders, no releases: pinned to a commit) --------
# Only dr_mp3.h is used. SOURCE_SUBDIR points at a folder that does not exist so FetchContent just
# downloads the headers and does not add dr_libs' own CMake project (tests etc.).
FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        dfe8377631000664666519fdb83da193fd8037f4
    GIT_SUBMODULES ""          # skip its test submodules (miniaudio)
    SOURCE_SUBDIR  do-not-add
)

# --- nlohmann/json (header-only; the small release archive, pinned by hash) ---------------
# Used to read TextureAtlas descriptions.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL3 spdlog stb dr_libs nlohmann_json)

add_library(emerald_stb INTERFACE)
add_library(Emerald::stb ALIAS emerald_stb)
target_include_directories(emerald_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(emerald_dr_libs INTERFACE)
add_library(Emerald::dr_libs ALIAS emerald_dr_libs)
target_include_directories(emerald_dr_libs SYSTEM INTERFACE ${dr_libs_SOURCE_DIR})

# --- Dear ImGui (optional) --------------------------------------------------
if(EMERALD_USE_IMGUI)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.92.9b-docking
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(emerald_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
    )
    add_library(Emerald::imgui ALIAS emerald_imgui)
    target_include_directories(emerald_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(emerald_imgui PUBLIC SDL3::SDL3)
endif()

# --- EnTT --------------------------------------------------------------------
# The entity layer (Entity/World.h) is built on it. Its include directory is marked SYSTEM, so
# warnings from inside EnTT's headers do not count against ours (-Wpedantic, /W4).
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)
get_target_property(EMERALD_ENTT_INCLUDES EnTT INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(EnTT PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${EMERALD_ENTT_INCLUDES}")

# --- Tracy profiler (optional: EMERALD_PROFILE) --------------------------------------------
# Only the client library (TracyClient.cpp, compiled with Tracy's own flags; its headers are
# SYSTEM includes, so its warnings do not count against ours). The viewer must be the same
# version: download it prebuilt from https://github.com/wolfpld/tracy/releases/tag/v0.14.1.
if(EMERALD_PROFILE)
    set(TRACY_ENABLE    ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE) # only collect while a viewer is connected
    FetchContent_Declare(tracy
        URL      https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
        URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(tracy)
endif()

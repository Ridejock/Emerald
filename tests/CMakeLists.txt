# Unit tests: small executables using the minimal harness in Test.h. Run them with `ctest`.

# Math tests with the implementation selected by EMERALD_MATH_SIMD.
add_executable(EmeraldMathTests TestMain.cpp MathTests.cpp)
target_link_libraries(EmeraldMathTests PRIVATE Emerald::Math)
emerald_set_warnings(EmeraldMathTests)
add_test(NAME MathTests COMMAND EmeraldMathTests)

# The same tests with SIMD forced off, so one ctest run covers both implementations. This target
# only uses the header-only math library (not Emerald::Emerald, which is built with the SIMD
# setting), so the two settings never mix in one executable.
if(EMERALD_MATH_SIMD)
    add_executable(EmeraldMathTestsScalar TestMain.cpp MathTests.cpp)
    target_include_directories(EmeraldMathTestsScalar PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(EmeraldMathTestsScalar PRIVATE EMERALD_MATH_SIMD=0)
    emerald_set_warnings(EmeraldMathTestsScalar)
    add_test(NAME MathTestsScalar COMMAND EmeraldMathTestsScalar)
endif()

# Logging (file sink) tests; these link the engine library.
add_executable(EmeraldLogTests TestMain.cpp LogTests.cpp)
target_link_libraries(EmeraldLogTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldLogTests)
add_test(NAME LogTests COMMAND EmeraldLogTests)

# Thread pool and std::pmr memory helpers; these link the engine library.
add_executable(EmeraldThreadPoolTests TestMain.cpp ThreadPoolTests.cpp)
target_link_libraries(EmeraldThreadPoolTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldThreadPoolTests)
add_test(NAME ThreadPoolTests COMMAND EmeraldThreadPoolTests)

add_executable(EmeraldMemoryTests TestMain.cpp MemoryTests.cpp)
target_link_libraries(EmeraldMemoryTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldMemoryTests)
add_test(NAME MemoryTests COMMAND EmeraldMemoryTests)

# Keyboard, gamepads, actions and fixed timestep; Renderer2D's CPU-side batching (no GPU needed).
add_executable(EmeraldInputTests TestMain.cpp InputTests.cpp GamepadTests.cpp)
target_link_libraries(EmeraldInputTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldInputTests)
add_test(NAME InputTests COMMAND EmeraldInputTests)

add_executable(EmeraldRenderer2DTests TestMain.cpp Renderer2DTests.cpp CrtEffectTests.cpp
    GpuDriverTests.cpp CameraTests.cpp AnimationTests.cpp)
target_link_libraries(EmeraldRenderer2DTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldRenderer2DTests)
add_test(NAME Renderer2DTests COMMAND EmeraldRenderer2DTests)

# Particle pool: spawning, physics, swap-remove, and the scalar and SSE updates agreeing.
add_executable(EmeraldParticleTests TestMain.cpp ParticleTests.cpp)
target_link_libraries(EmeraldParticleTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldParticleTests)
add_test(NAME ParticleTests COMMAND EmeraldParticleTests)

# Sound loading (WAV, MP3 via dr_mp3), mixer and synth; no audio device needed.
add_executable(EmeraldAudioTests TestMain.cpp AudioTests.cpp MixerTests.cpp)
target_link_libraries(EmeraldAudioTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldAudioTests)
add_test(NAME AudioTests COMMAND EmeraldAudioTests)

# Fonts (stb_truetype): metrics, measuring, layout and DrawString, using the sandbox's demo font.
add_executable(EmeraldFontTests TestMain.cpp FontTests.cpp)
target_link_libraries(EmeraldFontTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldFontTests PRIVATE
    EMERALD_TEST_FONT="${PROJECT_SOURCE_DIR}/sandbox/assets/fonts/PressStart2P-Regular.ttf")
emerald_set_warnings(EmeraldFontTests)
add_test(NAME FontTests COMMAND EmeraldFontTests)

# Collision: shape tests with normal and depth, raycasts, and the spatial hash vs brute force.
add_executable(EmeraldCollisionTests TestMain.cpp CollisionTests.cpp)
target_link_libraries(EmeraldCollisionTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldCollisionTests)
add_test(NAME CollisionTests COMMAND EmeraldCollisionTests)

# Asset manager bookkeeping: dedupe, reference counting, placeholders and hot reload (no GPU).
add_executable(EmeraldAssetTests TestMain.cpp AssetTests.cpp)
target_link_libraries(EmeraldAssetTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldAssetTests PRIVATE
    EMERALD_TEST_FONT="${PROJECT_SOURCE_DIR}/sandbox/assets/fonts/PressStart2P-Regular.ttf")
emerald_set_warnings(EmeraldAssetTests)
add_test(NAME AssetTests COMMAND EmeraldAssetTests)

# Easing curves, tweens (delay, repeat, yoyo, chaining, cancelling) and timers, at varied dt.
add_executable(EmeraldTweenTests TestMain.cpp TweenTests.cpp)
target_link_libraries(EmeraldTweenTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldTweenTests)
add_test(NAME TweenTests COMMAND EmeraldTweenTests)

# Tilemaps (Tiled JSON): the sandbox's sample room, flips, collision, errors and hot reload.
add_executable(EmeraldTilemapTests TestMain.cpp TilemapTests.cpp)
target_link_libraries(EmeraldTilemapTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldTilemapTests PRIVATE
    EMERALD_TEST_TILEMAPS="${PROJECT_SOURCE_DIR}/sandbox/assets/tilemaps")
emerald_set_warnings(EmeraldTilemapTests)
add_test(NAME TilemapTests COMMAND EmeraldTilemapTests)

# Scene stack: deferred changes and hook order, DrawBelow / UpdateBelow, transitions, input focus.
add_executable(EmeraldSceneTests TestMain.cpp SceneTests.cpp)
target_link_libraries(EmeraldSceneTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldSceneTests)
add_test(NAME SceneTests COMMAND EmeraldSceneTests)

# Entities: the World / Entity layer over EnTT, churn without leaks, and the core systems.
add_executable(EmeraldEntityTests TestMain.cpp EntityTests.cpp)
target_link_libraries(EmeraldEntityTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldEntityTests)
add_test(NAME EntityTests COMMAND EmeraldEntityTests)

# Platformer physics: slopes, one-way platforms, coyote time, jump buffer, and replaying the same
# input twice giving the same path bit for bit.
add_executable(EmeraldPlatformerTests TestMain.cpp PlatformerTests.cpp)
target_link_libraries(EmeraldPlatformerTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldPlatformerTests PRIVATE
    EMERALD_TEST_TILEMAPS="${PROJECT_SOURCE_DIR}/sandbox/assets/tilemaps")
emerald_set_warnings(EmeraldPlatformerTests)
add_test(NAME PlatformerTests COMMAND EmeraldPlatformerTests)

# Dev tools: command line options, input recordings, and a recorded platformer session replayed
# twice with bit-identical positions.
add_executable(EmeraldReplayTests TestMain.cpp ReplayTests.cpp)
target_link_libraries(EmeraldReplayTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldReplayTests PRIVATE
    EMERALD_TEST_TILEMAPS="${PROJECT_SOURCE_DIR}/sandbox/assets/tilemaps")
emerald_set_warnings(EmeraldReplayTests)
add_test(NAME ReplayTests COMMAND EmeraldReplayTests)

# The Python tools (tools/): their unit tests, and the asset checker on the sandbox's assets.
# Skipped if no Python 3 is found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME ToolsTests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/tools/tests)
    add_test(NAME SandboxAssets
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_assets.py
            ${PROJECT_SOURCE_DIR}/sandbox/assets)
endif()

# UI widgets: ids, layout, focus navigation, values from keys / mouse, repeat; no GPU needed.
add_executable(EmeraldUiTests TestMain.cpp UiTests.cpp)
target_link_libraries(EmeraldUiTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldUiTests)
add_test(NAME UiTests COMMAND EmeraldUiTests)

# Lighting math and the post-effect chain's order (no GPU).
add_executable(EmeraldLightingTests TestMain.cpp LightingTests.cpp)
target_link_libraries(EmeraldLightingTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldLightingTests)
add_test(NAME LightingTests COMMAND EmeraldLightingTests)

# Audio extras: spatial math, mixer groups, music streaming / crossfade (no device).
add_executable(EmeraldAudioExtrasTests TestMain.cpp AudioExtrasTests.cpp)
target_link_libraries(EmeraldAudioExtrasTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldAudioExtrasTests)
add_test(NAME AudioExtrasTests COMMAND EmeraldAudioExtrasTests)

# The versioned save system: format, migrations, atomic writes, slots (temp folder, no device).
add_executable(EmeraldSaveTests TestMain.cpp SaveTests.cpp)
target_link_libraries(EmeraldSaveTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldSaveTests)
add_test(NAME SaveTests COMMAND EmeraldSaveTests)

# Branching dialogue: decks, D3 sample behaviour, saves, hot reload and the box (no GPU).
add_executable(EmeraldDialogueTests TestMain.cpp DialogueTests.cpp)
target_link_libraries(EmeraldDialogueTests PRIVATE Emerald::Emerald)
target_compile_definitions(EmeraldDialogueTests PRIVATE
    EMERALD_TEST_FONT="${PROJECT_SOURCE_DIR}/sandbox/assets/fonts/PressStart2P-Regular.ttf")
emerald_set_warnings(EmeraldDialogueTests)
add_test(NAME DialogueTests COMMAND EmeraldDialogueTests)

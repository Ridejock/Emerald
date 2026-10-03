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

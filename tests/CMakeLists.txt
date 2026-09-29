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

add_executable(EmeraldRenderer2DTests TestMain.cpp Renderer2DTests.cpp)
target_link_libraries(EmeraldRenderer2DTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldRenderer2DTests)
add_test(NAME Renderer2DTests COMMAND EmeraldRenderer2DTests)

# Sound loading (WAV, MP3 via dr_mp3) and conversion to the mix format; no audio device needed.
add_executable(EmeraldAudioTests TestMain.cpp AudioTests.cpp)
target_link_libraries(EmeraldAudioTests PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldAudioTests)
add_test(NAME AudioTests COMMAND EmeraldAudioTests)

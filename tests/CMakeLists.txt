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

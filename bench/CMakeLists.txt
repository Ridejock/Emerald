# Micro-benchmark: scalar vs SSE Mat4 * Vec4 / Mat4 * Mat4. Build a Release config to get
# meaningful numbers.
add_executable(EmeraldMathBench MathBench.cpp)
target_link_libraries(EmeraldMathBench PRIVATE Emerald::Math)
emerald_set_warnings(EmeraldMathBench)

# Micro-benchmark: scalar vs SSE Mat4 * Vec4 / Mat4 * Mat4. Build a Release config to get
# meaningful numbers.
add_executable(EmeraldMathBench MathBench.cpp)
target_link_libraries(EmeraldMathBench PRIVATE Emerald::Math)
emerald_set_warnings(EmeraldMathBench)

# Particle update: scalar vs SSE over 10k / 100k / 1M particles (links the engine library).
add_executable(EmeraldParticleBench ParticleBench.cpp)
target_link_libraries(EmeraldParticleBench PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldParticleBench)

# Spatial hash: 10k moving circles (insert, update, queries, pairs) vs brute-force O(n^2).
add_executable(EmeraldCollisionBench CollisionBench.cpp)
target_link_libraries(EmeraldCollisionBench PRIVATE Emerald::Emerald)
emerald_set_warnings(EmeraldCollisionBench)

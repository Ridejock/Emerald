add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Emerald::Emerald)

# HLSL -> SPIR-V / DXIL / MSL (+ reflection JSON) into bin/shaders/ at build time.
emerald_add_shaders(Sandbox
    ${PROJECT_SOURCE_DIR}/shaders/Triangle.vert.hlsl
    ${PROJECT_SOURCE_DIR}/shaders/Triangle.frag.hlsl
)

emerald_set_warnings(Sandbox)

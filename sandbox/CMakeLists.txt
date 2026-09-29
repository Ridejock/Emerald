add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Emerald::Emerald)

# HLSL -> SPIR-V / DXIL / MSL (+ reflection JSON) into bin/shaders/ at build time: the engine's
# shaders (Renderer2D) plus the sandbox's own triangle shaders.
emerald_add_shaders(Sandbox
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.vert.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.frag.hlsl
)

emerald_set_warnings(Sandbox)

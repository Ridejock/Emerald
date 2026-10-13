add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Emerald::Emerald)

# HLSL -> SPIR-V / DXIL / MSL (+ reflection JSON) into bin/shaders/ at build time: the engine's
# shaders (Renderer2D) plus the sandbox's own triangle shaders.
emerald_add_shaders(Sandbox
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.vert.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.frag.hlsl
)

emerald_set_warnings(Sandbox)

# With hot reload (Debug builds) the sandbox reads its assets straight from this source folder,
# so editing e.g. assets/sprites/hero.png shows up in the running sandbox. Other builds use the
# copy next to the executable (and don't carry the source path).
target_compile_definitions(Sandbox PRIVATE
    $<$<CONFIG:Debug>:SANDBOX_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets">)

# The whole assets folder goes next to the executable on every build (not only when Sandbox
# relinks), so an edited or new file is never stale or missing there.
add_custom_target(SandboxAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Sandbox>/assets
    VERBATIM)
add_dependencies(Sandbox SandboxAssets)

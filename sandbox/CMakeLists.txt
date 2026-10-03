add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Emerald::Emerald)

# HLSL -> SPIR-V / DXIL / MSL (+ reflection JSON) into bin/shaders/ at build time: the engine's
# shaders (Renderer2D) plus the sandbox's own triangle shaders.
emerald_add_shaders(Sandbox
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.vert.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/Triangle.frag.hlsl
)

emerald_set_warnings(Sandbox)

# With hot reload (debug builds) the sandbox reads its assets straight from this source folder,
# so editing e.g. assets/sprites/hero.png shows up in the running sandbox. Release builds use
# the copy next to the executable.
target_compile_definitions(Sandbox PRIVATE SANDBOX_SOURCE_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# The text demo's font (Press Start 2P, SIL Open Font License; see assets/fonts/) goes next to the
# executable, where the sandbox looks for it with Paths::GetBasePath().
add_custom_command(TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Sandbox>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PressStart2P-Regular.ttf
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PressStart2P-OFL.txt
            $<TARGET_FILE_DIR:Sandbox>/assets/fonts/
    # The animated hero sheet (made by tools/sprites/make_hero.py).
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Sandbox>/assets/sprites
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/hero.png
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/hero.json
            $<TARGET_FILE_DIR:Sandbox>/assets/sprites/
    VERBATIM)

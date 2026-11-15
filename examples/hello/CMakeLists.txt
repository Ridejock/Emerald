# The hello-world program. Docs: docs/hello-world.md (the listing there is this main.cpp).

add_executable(Hello main.cpp)
target_link_libraries(Hello PRIVATE Emerald::Emerald)

# Engine shaders (Renderer2D) next to the executable, and EMERALD_SHADER_FORMATS for main.cpp.
emerald_add_shaders(Hello)

emerald_set_warnings(Hello)

add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE Emerald::Emerald)

if(MSVC)
    target_compile_options(Sandbox PRIVATE /W4 /permissive-)
else()
    target_compile_options(Sandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()

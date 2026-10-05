# stb_vorbis alone: stripped of AddressSanitizer. MSVC ASan + this TU OOMs GitHub-hosted
# Windows runners (lost communication mid-build). Decode of untrusted audio is not where we
# need ASan; Music.cpp / Mixer stay instrumented.

get_directory_property(_emerald_vorbis_compile COMPILE_OPTIONS)
set(_emerald_vorbis_compile_kept "")
foreach(_opt IN LISTS _emerald_vorbis_compile)
    if(NOT "${_opt}" MATCHES "fsanitize")
        list(APPEND _emerald_vorbis_compile_kept "${_opt}")
    endif()
endforeach()
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_emerald_vorbis_compile_kept}")

get_directory_property(_emerald_vorbis_link LINK_OPTIONS)
set(_emerald_vorbis_link_kept "")
foreach(_opt IN LISTS _emerald_vorbis_link)
    if(NOT "${_opt}" MATCHES "fsanitize")
        list(APPEND _emerald_vorbis_link_kept "${_opt}")
    endif()
endforeach()
set_directory_properties(PROPERTIES LINK_OPTIONS "${_emerald_vorbis_link_kept}")

add_library(EmeraldVorbis STATIC stb_vorbis.cpp)
add_library(Emerald::Vorbis ALIAS EmeraldVorbis)
target_link_libraries(EmeraldVorbis PUBLIC Emerald::stb)
if(MSVC)
    # Vendor C: silence noise from stb_vorbis.c (C4701 etc.); not our code to fix.
    target_compile_options(EmeraldVorbis PRIVATE /W0)
endif()

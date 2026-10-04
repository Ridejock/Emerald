# AddressSanitizer (EMERALD_ASAN, the debug-asan preset): finds out-of-bounds accesses,
# use-after-free and (on Linux) leaks at run time, at about 2x the cost.
#
# The flags go on everything built from here down (SDL and the other dependencies too), and on
# the Emerald target's interface, so a game that links Emerald is instrumented the same way.

set(EMERALD_ASAN_COMPILE_FLAGS "")
set(EMERALD_ASAN_LINK_FLAGS "")
if(EMERALD_ASAN)
    if(MSVC)
        set(EMERALD_ASAN_COMPILE_FLAGS /fsanitize=address)
        # ASan does not work with incremental linking or the /RTC run-time checks; take them out
        # of CMake's default flags.
        foreach(lang C CXX)
            string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_${lang}_FLAGS_DEBUG
                "${CMAKE_${lang}_FLAGS_DEBUG}")
        endforeach()
        foreach(kind EXE SHARED MODULE)
            string(REGEX REPLACE "/INCREMENTAL(:YES)?( |$)" "/INCREMENTAL:NO "
                CMAKE_${kind}_LINKER_FLAGS_DEBUG "${CMAKE_${kind}_LINKER_FLAGS_DEBUG}")
        endforeach()
        set(EMERALD_ASAN_LINK_FLAGS /INCREMENTAL:NO)
    else()
        set(EMERALD_ASAN_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer)
        set(EMERALD_ASAN_LINK_FLAGS -fsanitize=address)
    endif()
    add_compile_options(${EMERALD_ASAN_COMPILE_FLAGS})
    add_link_options(${EMERALD_ASAN_LINK_FLAGS})

    # MSVC: the programs need the ASan runtime DLL (next to cl.exe); copy it to the output folder
    # (build/<preset>/bin) so they and ctest run without changing PATH.
    if(MSVC)
        get_filename_component(emerald_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB emerald_asan_dlls "${emerald_cl_dir}/clang_rt.asan*_dynamic-x86_64.dll")
        if(emerald_asan_dlls)
            file(COPY ${emerald_asan_dlls} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        else()
            message(WARNING "EMERALD_ASAN: no clang_rt.asan DLL next to ${CMAKE_CXX_COMPILER}; "
                            "install the C++ AddressSanitizer component in the VS Installer")
        endif()
    endif()
endif()

# Puts the ASan flags on a target's interface, so whatever links it is instrumented too.
function(emerald_asan_target target)
    if(EMERALD_ASAN)
        target_compile_options(${target} INTERFACE ${EMERALD_ASAN_COMPILE_FLAGS})
        target_link_options(${target} INTERFACE ${EMERALD_ASAN_LINK_FLAGS})
    endif()
endfunction()

# Build-time HLSL shader compilation with SDL_shadercross.
#
#   emerald_add_shaders(<target> <file.vert.hlsl> <file.frag.hlsl> ...)
#
# compiles each HLSL file to every format in EMERALD_SHADER_FORMATS (SPIR-V for Vulkan, DXIL for
# Direct3D 12, MSL for Metal) plus a reflection .json, into <runtime output dir>/shaders/.
# The stage comes from the file name: *.vert.hlsl, *.frag.hlsl or *.comp.hlsl; the entry point
# is always `main`. Editing an .hlsl (or any .hlsli in the same folder) recompiles it.
# The target also gets EMERALD_SHADER_FORMATS defined as the matching SDL_GPUShaderFormat mask.
#
# Where the shadercross executable comes from:
#   1. EMERALD_SHADERCROSS_EXECUTABLE, if set (a prebuilt tool; e.g. from an SDL_shadercross
#      release or your own build), else
#   2. built from source (EMERALD_BUILD_SHADERCROSS=ON, default) as a separate host-tool project
#      (tools/shadercross) in EMERALD_SHADERCROSS_BUILD_DIR. That directory is shared by all
#      presets, so the (slow) DirectXShaderCompiler build only happens once, else
#   3. `shadercross` found on PATH.

option(EMERALD_BUILD_SHADERCROSS
    "Build SDL_shadercross from source when EMERALD_SHADERCROSS_EXECUTABLE is not set" ON)
set(EMERALD_SHADERCROSS_EXECUTABLE "" CACHE FILEPATH
    "Prebuilt shadercross executable to use instead of building it")
set(EMERALD_SHADERCROSS_BUILD_DIR "${PROJECT_SOURCE_DIR}/build/_shadercross" CACHE PATH
    "Build directory for the shadercross host tool (shared between presets)")
set(EMERALD_SHADER_FORMATS "SPIRV;DXIL;MSL" CACHE STRING
    "Shader formats to generate (any of SPIRV, DXIL, MSL)")

# Resolves the shadercross executable once (lazily, so projects that never compile shaders
# never build the tool). Sets the global properties EMERALD_SHADERCROSS / _DEPENDS / _FORMATS.
function(_emerald_setup_shadercross)
    get_property(done GLOBAL PROPERTY EMERALD_SHADERCROSS SET)
    if(done)
        return()
    endif()

    set(formats ${EMERALD_SHADER_FORMATS})
    set(depends "")
    set(prebuilt TRUE)

    if(EMERALD_SHADERCROSS_EXECUTABLE)
        set(exe "${EMERALD_SHADERCROSS_EXECUTABLE}")
    elseif(EMERALD_BUILD_SHADERCROSS)
        include(ExternalProject)
        set(prebuilt FALSE)
        set(exe "${EMERALD_SHADERCROSS_BUILD_DIR}/bin/shadercross${CMAKE_EXECUTABLE_SUFFIX}")
        set(tool_args -DCMAKE_BUILD_TYPE=Release)
        if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
            list(APPEND tool_args
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
        endif()
        ExternalProject_Add(emerald_shadercross
            SOURCE_DIR        "${PROJECT_SOURCE_DIR}/tools/shadercross"
            BINARY_DIR        "${EMERALD_SHADERCROSS_BUILD_DIR}"
            CMAKE_ARGS        ${tool_args}
            BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
                              --target shadercross
            INSTALL_COMMAND   ""
            BUILD_BYPRODUCTS  "${exe}"
            USES_TERMINAL_CONFIGURE TRUE
            USES_TERMINAL_BUILD     TRUE
        )
        set(depends emerald_shadercross "${exe}")
        message(STATUS "Emerald: shadercross will be built from source in ${EMERALD_SHADERCROSS_BUILD_DIR}"
                       " (first build compiles DirectXShaderCompiler and takes a while)")
    else()
        find_program(EMERALD_SHADERCROSS_FOUND NAMES shadercross)
        if(NOT EMERALD_SHADERCROSS_FOUND)
            message(FATAL_ERROR
                "shadercross not found. Set EMERALD_SHADERCROSS_EXECUTABLE, put shadercross on "
                "PATH, or enable EMERALD_BUILD_SHADERCROSS.")
        endif()
        set(exe "${EMERALD_SHADERCROSS_FOUND}")
    endif()

    # A prebuilt shadercross may lack DXC (DXIL output). Probe it once and drop DXIL with a
    # warning instead of failing the build; the engine then only loads the other formats.
    if(prebuilt AND "DXIL" IN_LIST formats)
        set(probe_dir "${CMAKE_BINARY_DIR}/shadercross-probe")
        file(WRITE "${probe_dir}/probe.frag.hlsl" "float4 main() : SV_Target0 { return 1; }\n")
        execute_process(
            COMMAND "${exe}" "${probe_dir}/probe.frag.hlsl" -s HLSL -d DXIL -t fragment
                    -o "${probe_dir}/probe.dxil"
            RESULT_VARIABLE probe_result OUTPUT_QUIET ERROR_QUIET)
        if(NOT probe_result EQUAL 0)
            message(WARNING "Emerald: '${exe}' cannot produce DXIL (no DXC?). DXIL shaders are "
                            "skipped, so the D3D12 backend will not find its shaders.")
            list(REMOVE_ITEM formats DXIL)
        endif()
    endif()

    message(STATUS "Emerald: shadercross = ${exe}; shader formats: ${formats}")
    set_property(GLOBAL PROPERTY EMERALD_SHADERCROSS "${exe}")
    set_property(GLOBAL PROPERTY EMERALD_SHADERCROSS_DEPENDS "${depends}")
    set_property(GLOBAL PROPERTY EMERALD_SHADERCROSS_FORMATS "${formats}")
endfunction()

function(emerald_add_shaders target)
    _emerald_setup_shadercross()
    get_property(shadercross GLOBAL PROPERTY EMERALD_SHADERCROSS)
    get_property(tool_depends GLOBAL PROPERTY EMERALD_SHADERCROSS_DEPENDS)
    get_property(formats GLOBAL PROPERTY EMERALD_SHADERCROSS_FORMATS)

    # Shaders go next to the executable, e.g. build/debug/bin/shaders/.
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi_config)
        set(out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders")
    else()
        set(out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    endif()

    set(ext_SPIRV spv)
    set(ext_DXIL dxil)
    set(ext_MSL msl)
    set(ext_JSON json)

    set(outputs "")
    foreach(src IN LISTS ARGN)
        get_filename_component(src "${src}" ABSOLUTE)
        get_filename_component(src_dir "${src}" DIRECTORY)
        get_filename_component(file_name "${src}" NAME)
        string(REGEX REPLACE "\\.hlsl$" "" base "${file_name}") # Triangle.vert

        if(base MATCHES "\\.vert$")
            set(stage vertex)
        elseif(base MATCHES "\\.frag$")
            set(stage fragment)
        elseif(base MATCHES "\\.comp$")
            set(stage compute)
        else()
            message(FATAL_ERROR "Cannot infer shader stage of '${file_name}' "
                                "(expected *.vert.hlsl, *.frag.hlsl or *.comp.hlsl)")
        endif()

        file(GLOB includes CONFIGURE_DEPENDS "${src_dir}/*.hlsli")
        foreach(format IN LISTS formats ITEMS JSON)
            set(out "${out_dir}/${base}.${ext_${format}}")
            add_custom_command(
                OUTPUT  "${out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                COMMAND "${shadercross}" "${src}" -s HLSL -d ${format} -t ${stage} -e main
                        -I "${src_dir}" -o "${out}"
                DEPENDS "${src}" ${includes} ${tool_depends}
                COMMENT "shadercross ${file_name} -> ${base}.${ext_${format}}"
                VERBATIM)
            list(APPEND outputs "${out}")
        endforeach()
    endforeach()

    add_custom_target(${target}_shaders ALL DEPENDS ${outputs} SOURCES ${ARGN})
    add_dependencies(${target} ${target}_shaders)

    # Tell the code which formats exist, e.g. for ApplicationSpec::ShaderFormats.
    list(TRANSFORM formats PREPEND "SDL_GPU_SHADERFORMAT_" OUTPUT_VARIABLE format_flags)
    list(JOIN format_flags "|" format_mask)
    target_compile_definitions(${target} PRIVATE "EMERALD_SHADER_FORMATS=(${format_mask})")
endfunction()

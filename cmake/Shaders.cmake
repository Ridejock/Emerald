# Build-time HLSL shader compilation with SDL_shadercross.
#
#   emerald_add_shaders(<target> [<file.vert.hlsl> <file.frag.hlsl> ...])
#
# Call it once for every executable using Emerald (even without shaders of its own): it also
# compiles the engine's built-in shaders (Emerald's shaders/ folder, e.g. Renderer2D) for it.
# Each HLSL file is compiled to every format in EMERALD_SHADER_FORMATS (SPIR-V for Vulkan, DXIL
# for Direct3D 12, MSL for Metal) plus a reflection .json, into a shaders/ folder next to the
# target's executable (its RUNTIME_OUTPUT_DIRECTORY, or its build folder if that is not set).
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
# CMAKE_SOURCE_DIR is the top-level project: Emerald itself, or the game that pulls it in.
set(EMERALD_SHADERCROSS_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/_shadercross" CACHE PATH
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

    # Relative to this file, not PROJECT_SOURCE_DIR: that is the calling project's when Emerald is
    # a subproject (FetchContent / add_subdirectory).
    get_filename_component(EMERALD_ROOT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)

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
        # The tool dir is shared, but ExternalProject's stamps live in each engine build dir. If
        # the tool dir is missing or predates the dxcompiler copy next to the exe (older Emerald),
        # drop the stamps so configure + build of the tool run again instead of being skipped.
        set(stamp_dir "${CMAKE_BINARY_DIR}/_emerald_shadercross-stamp")
        set(dxc_lib "${CMAKE_SHARED_LIBRARY_PREFIX}dxcompiler${CMAKE_SHARED_LIBRARY_SUFFIX}")
        if(NOT EXISTS "${EMERALD_SHADERCROSS_BUILD_DIR}/CMakeCache.txt" OR
           NOT EXISTS "${EMERALD_SHADERCROSS_BUILD_DIR}/bin/${dxc_lib}")
            file(REMOVE_RECURSE "${stamp_dir}")
        endif()
        ExternalProject_Add(emerald_shadercross
            STAMP_DIR         "${stamp_dir}"
            SOURCE_DIR        "${EMERALD_ROOT}/tools/shadercross"
            BINARY_DIR        "${EMERALD_SHADERCROSS_BUILD_DIR}"
            CMAKE_ARGS        ${tool_args}
            # shadercross_bundle = shadercross + dxcompiler/dxil copied next to it + smoke test.
            BUILD_COMMAND     ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
                              --target shadercross_bundle
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
            RESULT_VARIABLE probe_result OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_output)
        if(NOT probe_result EQUAL 0)
            string(STRIP "${probe_output}" probe_output)
            set(hint "")
            if(probe_output MATCHES "SPIR-V CodeGen not available")
                string(CONCAT hint " It loaded a dxcompiler without SPIR-V codegen (on Windows usually the "
                         "Windows SDK's or System32's dxcompiler.dll because the one built with "
                         "shadercross is not next to the .exe); shadercross needs it because "
                         "HLSL -> DXIL round-trips through SPIR-V. Put the matching dxcompiler.dll "
                         "and dxil.dll next to shadercross.")
            endif()
            message(WARNING "Emerald: '${exe}' cannot produce DXIL: ${probe_output}${hint} DXIL "
                            "shaders are skipped, so the D3D12 backend will not find its shaders.")
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
    get_target_property(exe_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT exe_dir)
        get_target_property(exe_dir ${target} BINARY_DIR)
    endif()
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(multi_config AND NOT exe_dir MATCHES "\\$<")
        set(out_dir "${exe_dir}/$<CONFIG>/shaders") # e.g. Visual Studio: bin/Debug/shaders
    else()
        set(out_dir "${exe_dir}/shaders")
    endif()

    # The engine's own shaders, then the target's.
    get_filename_component(engine_shader_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../shaders" ABSOLUTE)
    file(GLOB engine_shaders CONFIGURE_DEPENDS "${engine_shader_dir}/*.hlsl")
    set(sources ${engine_shaders} ${ARGN})

    set(ext_SPIRV spv)
    set(ext_DXIL dxil)
    set(ext_MSL msl)
    set(ext_JSON json)

    set(outputs "")
    set(shared_targets "")
    foreach(src IN LISTS sources)
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
            # Several executables sharing an output folder (e.g. all in bin/) share the engine
            # shaders: only the first gets the build rule, the others depend on its target.
            string(MD5 out_key "${out}")
            get_property(owner GLOBAL PROPERTY "EMERALD_SHADER_RULE_${out_key}")
            if(owner)
                list(APPEND shared_targets ${owner})
                continue()
            endif()
            set_property(GLOBAL PROPERTY "EMERALD_SHADER_RULE_${out_key}" ${target}_shaders)
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

    add_custom_target(${target}_shaders ALL DEPENDS ${outputs} SOURCES ${sources})
    add_dependencies(${target} ${target}_shaders)
    list(REMOVE_DUPLICATES shared_targets)
    foreach(shared IN LISTS shared_targets)
        add_dependencies(${target}_shaders ${shared})
    endforeach()

    # Tell the code which formats exist, e.g. for ApplicationSpec::ShaderFormats.
    list(TRANSFORM formats PREPEND "SDL_GPU_SHADERFORMAT_" OUTPUT_VARIABLE format_flags)
    list(JOIN format_flags "|" format_mask)
    target_compile_definitions(${target} PRIVATE "EMERALD_SHADER_FORMATS=(${format_mask})")
endfunction()

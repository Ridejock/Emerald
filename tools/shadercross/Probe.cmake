# Smoke test for a freshly built shadercross: HLSL -> SPIR-V and HLSL -> DXIL must both work.
#   cmake -DSHADERCROSS=<exe> -DWORK_DIR=<dir> -P Probe.cmake
file(WRITE "${WORK_DIR}/probe.frag.hlsl" "float4 main() : SV_Target0 { return 1; }\n")
foreach(format SPIRV DXIL)
    execute_process(
        COMMAND "${SHADERCROSS}" "${WORK_DIR}/probe.frag.hlsl" -s HLSL -d ${format} -t fragment
                -e main -o "${WORK_DIR}/probe.${format}"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        get_filename_component(bin_dir "${SHADERCROSS}" DIRECTORY)
        message(FATAL_ERROR
            "Emerald: the shadercross that was just built cannot compile HLSL -> ${format}:\n"
            "  ${out}\n"
            "If this says 'SPIR-V CodeGen not available', shadercross loaded a dxcompiler library "
            "other than the one built with it (e.g. the Windows SDK's or System32's "
            "dxcompiler.dll, which lack SPIR-V support). The right dxcompiler and dxil must sit "
            "next to the executable in ${bin_dir}. Delete the shadercross build directory and "
            "rebuild to regenerate it.")
    endif()
endforeach()
message(STATUS "shadercross smoke test passed (HLSL -> SPIR-V, DXIL)")

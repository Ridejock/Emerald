# Smoke test for a freshly built shadercross: HLSL -> SPIR-V and HLSL -> DXIL must both work.
#   cmake -DSHADERCROSS=<exe> -DWORK_DIR=<dir> -P Probe.cmake
# The probe samples a texture, like the sprite shader: some DXC builds (seen with GCC 14 on
# Linux) compile trivial shaders but fail validation on every texture sample.
file(WRITE "${WORK_DIR}/probe.frag.hlsl"
     "Texture2D<float4> T : register(t0, space2);\n"
     "SamplerState S : register(s0, space2);\n"
     "float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return T.Sample(S, uv); }\n")
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
            "rebuild to regenerate it.\n"
            "If it says 'sample_* instructions require resource to be declared to return UNORM, "
            "SNORM or FLOAT', DXC was miscompiled by the host compiler (GCC 14 does this): "
            "install clang, delete the shadercross build directory and reconfigure (Emerald then "
            "builds the tool with Clang), or use a prebuilt shadercross via "
            "EMERALD_SHADERCROSS_EXECUTABLE.")
    endif()
endforeach()
message(STATUS "shadercross smoke test passed (HLSL -> SPIR-V, DXIL)")

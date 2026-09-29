// Vertex shader for the sandbox triangle/quad.
//
// SDL GPU binding conventions for HLSL (see SDL_CreateGPUShader docs):
//   vertex stage:   textures/samplers/storage in space0, uniform buffers in space1
//   fragment stage: textures/samplers/storage in space2, uniform buffers in space3
// Vertex inputs use TEXCOORD<n> semantics, where n is the attribute "location" set on the CPU.

cbuffer Uniforms : register(b0, space1)
{
    // Projection * model, built on the CPU with Emerald::Mat4. Mat4 is column-major with column
    // vectors, which is HLSL's default cbuffer packing, so mul(matrix, vector) is M * p.
    column_major float4x4 Transform;
};

struct Input
{
    float2 Position : TEXCOORD0; // model space (for the sandbox: unit shapes)
    float3 Color : TEXCOORD1;
};

struct Output
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.Position = mul(Transform, float4(input.Position, 0.0f, 1.0f));
    output.Color = float4(input.Color, 1.0f);
    return output;
}

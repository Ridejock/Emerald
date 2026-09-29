// Vertex shader for the sandbox triangle/quad.
//
// SDL GPU binding conventions for HLSL (see SDL_CreateGPUShader docs):
//   vertex stage:   textures/samplers/storage in space0, uniform buffers in space1
//   fragment stage: textures/samplers/storage in space2, uniform buffers in space3
// Vertex inputs use TEXCOORD<n> semantics, where n is the attribute "location" set on the CPU.

cbuffer Transform : register(b0, space1)
{
    float2 Offset; // added to the position after scaling (NDC units)
    float2 Scale;
};

struct Input
{
    float2 Position : TEXCOORD0;
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
    output.Position = float4(input.Position * Scale + Offset, 0.0f, 1.0f);
    output.Color = float4(input.Color, 1.0f);
    return output;
}

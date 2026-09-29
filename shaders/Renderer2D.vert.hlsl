// Vertex shader for Emerald::Renderer2D (batched lines).
// Positions are already in world space (transforms are applied on the CPU while batching), so
// the only uniform is the batch's view-projection matrix.

cbuffer Uniforms : register(b0, space1)
{
    column_major float4x4 ViewProjection;
};

struct Input
{
    float2 Position : TEXCOORD0; // world space, e.g. pixels
    float4 Color : TEXCOORD1;    // UBYTE4_NORM: the GPU turns 0..255 into 0..1
};

struct Output
{
    float4 Color : TEXCOORD0;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.Position = mul(ViewProjection, float4(input.Position, 0.0f, 1.0f));
    output.Color = input.Color;
    return output;
}

// Vertex shader for Emerald::Renderer2D sprites (batched textured quads).
// Like the line shader, positions are already in world space; the only uniform is the batch's
// view-projection matrix.

cbuffer Uniforms : register(b0, space1)
{
    column_major float4x4 ViewProjection;
};

struct Input
{
    float2 Position : TEXCOORD0; // world space, e.g. pixels
    float2 TexCoord : TEXCOORD1; // 0..1 across the texture
    float4 Color : TEXCOORD2;    // tint, UBYTE4_NORM
};

struct Output
{
    float2 TexCoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.Position = mul(ViewProjection, float4(input.Position, 0.0f, 1.0f));
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}

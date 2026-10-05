// Lit sprites: world-space position and the sprite's rotation (for rotating the normal map)
// go to the fragment shader. ViewProjection is the same as the unlit Sprite.vert.

cbuffer Uniforms : register(b0, space1)
{
    column_major float4x4 ViewProjection;
};

struct Input
{
    float2 Position : TEXCOORD0; // world space
    float2 TexCoord : TEXCOORD1;
    float4 Color : TEXCOORD2;    // tint
    float2 CosSin : TEXCOORD3;   // (cos, sin) of the sprite's rotation
};

struct Output
{
    float2 TexCoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float2 WorldPos : TEXCOORD2;
    float2 CosSin : TEXCOORD3;
    float4 Position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.Position = mul(ViewProjection, float4(input.Position, 0.0f, 1.0f));
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    output.WorldPos = input.Position;
    output.CosSin = input.CosSin;
    return output;
}

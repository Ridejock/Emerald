// Fullscreen grade: multiply by Color and darken towards the corners by Vignette.

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer Uniforms : register(b0, space3)
{
    float3 Color;
    float Vignette;
};

float4 main(float2 texCoord : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    float3 rgb = Source.Sample(SourceSampler, texCoord).rgb * Color;
    const float2 c = texCoord * 2.0f - 1.0f;
    const float vignette = 1.0f - Vignette * dot(c, c);
    return float4(rgb * saturate(vignette), 1.0f);
}

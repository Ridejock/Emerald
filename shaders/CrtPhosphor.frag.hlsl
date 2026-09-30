// CRT post-process, pass 1: phosphor persistence. The new frame lights the phosphor; what was
// lit before fades by Decay per frame (computed from the afterglow half-life and frame time).
// Rendered into a 16-bit float texture so faint trails fade out smoothly instead of sticking.

Texture2D<float4> Scene : register(t0, space2);
SamplerState SceneSampler : register(s0, space2);
Texture2D<float4> History : register(t1, space2);
SamplerState HistorySampler : register(s1, space2);

cbuffer Uniforms : register(b0, space3)
{
    float Decay; // 0 = no afterglow
    float3 Padding;
};

float4 main(float2 texCoord : TEXCOORD0) : SV_Target0
{
    const float3 scene = Scene.Sample(SceneSampler, texCoord).rgb;
    // The small bias lets the tail reach black instead of creeping towards it forever.
    const float3 old = max(History.Sample(HistorySampler, texCoord).rgb * Decay - 0.002f, 0.0f);
    return float4(max(scene, old), 1.0f);
}

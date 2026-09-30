// CRT post-process, final pass: curved glass, the phosphor image plus two bloom levels, a slight
// chromatic fringe, vignette and the optional scanlines and aperture mask. See CrtEffect.h.

Texture2D<float4> Phosphor : register(t0, space2);
SamplerState PhosphorSampler : register(s0, space2);
Texture2D<float4> BloomNear : register(t1, space2);
SamplerState BloomNearSampler : register(s1, space2);
Texture2D<float4> BloomWide : register(t2, space2);
SamplerState BloomWideSampler : register(s2, space2);

cbuffer Uniforms : register(b0, space3)
{
    float2 Size;      // output size in pixels
    float2 Chroma;    // red/blue offset in UV at the screen edge (x, y)
    float Curvature;
    float Bloom;
    float Vignette;
    float Brightness;
    float Scanlines;
    float ScanlineCount;
    float Mask;
    float EdgeSoftness; // width of the curved screen edge's fade, in UV
};

float3 SampleScreen(float2 uv)
{
    const float3 nearGlow = BloomNear.Sample(BloomNearSampler, uv).rgb;
    const float3 wideGlow = BloomWide.Sample(BloomWideSampler, uv).rgb;
    return Phosphor.Sample(PhosphorSampler, uv).rgb + (nearGlow * 0.6f + wideGlow * 0.4f) * Bloom;
}

float4 main(float2 texCoord : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    // Barrel distortion: bend each axis by the other's distance from the center. The edge
    // centers stay put and the corners bend out of view, which rounds them like CRT glass.
    float2 c = texCoord * 2.0f - 1.0f;
    c *= 1.0f + Curvature * c.yx * c.yx;
    const float2 uv = c * 0.5f + 0.5f;

    // Red and blue sampled slightly outwards / inwards, more towards the edges.
    const float2 offset = c * Chroma;
    float3 color;
    color.r = SampleScreen(uv + offset).r;
    color.g = SampleScreen(uv).g;
    color.b = SampleScreen(uv - offset).b;

    // Darker towards the corners.
    const float2 v = uv * (1.0f - uv);
    color *= pow(saturate(16.0f * v.x * v.y), Vignette * 0.5f);

    // Faint dark gaps between scanlines, following the curved glass.
    const float scan = 0.5f + 0.5f * cos(6.2831853f * uv.y * ScanlineCount);
    color *= 1.0f - Scanlines * (1.0f - scan);

    // Aperture grille: every output pixel column favors one of red, green, blue.
    const int column = int(position.x) % 3;
    const float3 stripe = column == 0 ? float3(1, 0.4f, 0.4f)
                                      : (column == 1 ? float3(0.4f, 1, 0.4f) : float3(0.4f, 0.4f, 1));
    color *= lerp(float3(1, 1, 1), stripe * 1.5f, Mask);

    // Black outside the glass, with a soft edge.
    const float2 edge = smoothstep(0.0f, EdgeSoftness, uv) * smoothstep(0.0f, EdgeSoftness, 1.0f - uv);
    color *= edge.x * edge.y * Brightness;
    return float4(color, 1.0f);
}

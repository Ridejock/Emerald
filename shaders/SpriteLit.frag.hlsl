// Lit sprites: albedo * (ambient + sum of point/spot lights). Optional normal map in t1; a flat
// (0.5, 0.5, 1) texture stands in when the sprite has none. Matches Emerald::LightMath.

Texture2D<float4> AlbedoTexture : register(t0, space2);
SamplerState AlbedoSampler : register(s0, space2);
Texture2D<float4> NormalTexture : register(t1, space2);
SamplerState NormalSampler : register(s1, space2);

cbuffer Lighting : register(b0, space3)
{
    float4 Ambient;     // rgb
    float4 LightCount;  // x = count
    float4 Lights[32];  // 8 lights * 4 float4s: PosRadius, ColorIntensity, DirCos, FalloffPad
};

float Attenuation(float distance, float radius, float falloff)
{
    if (radius <= 0.0f)
        return 0.0f;
    const float t = saturate(1.0f - distance / radius);
    return pow(t, max(falloff, 1e-3f));
}

float SpotFactor(float cosAngle, float cosInner, float cosOuter)
{
    if (cosOuter <= -1.5f)
        return 1.0f;
    return smoothstep(cosOuter, cosInner, cosAngle);
}

float4 main(float2 texCoord : TEXCOORD0, float4 color : TEXCOORD1, float2 worldPos : TEXCOORD2,
            float2 cosSin : TEXCOORD3) : SV_Target0
{
    const float4 albedo = AlbedoTexture.Sample(AlbedoSampler, texCoord) * color;
    // Normal map: tangent-ish space with +Z toward the camera; rotate XY by the sprite.
    float3 n = NormalTexture.Sample(NormalSampler, texCoord).xyz * 2.0f - 1.0f;
    const float c = cosSin.x;
    const float s = cosSin.y;
    n.xy = float2(n.x * c - n.y * s, n.x * s + n.y * c);
    n = normalize(n);

    float3 lit = Ambient.rgb;
    const int count = (int)LightCount.x;
    for (int i = 0; i < 8; ++i) {
        if (i >= count)
            break;
        const float4 posRadius = Lights[i * 4 + 0];
        const float4 colorInt = Lights[i * 4 + 1];
        const float4 dirCos = Lights[i * 4 + 2];
        const float falloff = Lights[i * 4 + 3].x;

        const float3 toLight = float3(posRadius.xy - worldPos, posRadius.z);
        const float distance = length(toLight);
        const float3 L = distance > 1e-5f ? toLight / distance : float3(0.0f, 0.0f, 1.0f);
        const float ndotl = max(dot(n, L), 0.0f);
        const float atten = Attenuation(distance, posRadius.w, falloff);

        float spot = 1.0f;
        if (dirCos.w > -1.5f) {
            const float2 fromLight = worldPos - posRadius.xy;
            const float len = length(fromLight);
            const float cosAngle = len > 1e-5f ? dot(fromLight / len, dirCos.xy) : 1.0f;
            spot = SpotFactor(cosAngle, dirCos.z, dirCos.w);
        }
        lit += colorInt.rgb * (colorInt.a * ndotl * atten * spot);
    }
    return float4(albedo.rgb * lit, albedo.a);
}

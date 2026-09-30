// CRT post-process, bloom: one direction of a separable 9-tap Gaussian blur. Step is the UV
// distance between taps (horizontal or vertical); rendering into a smaller target downsamples.

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer Uniforms : register(b0, space3)
{
    float2 Step;
    float2 Padding;
};

float4 main(float2 texCoord : TEXCOORD0) : SV_Target0
{
    // exp(-k^2 / 8): a Gaussian with sigma = 2 taps.
    const float weights[5] = {1.0f, 0.8825f, 0.6065f, 0.3247f, 0.1353f};
    float3 sum = Source.Sample(SourceSampler, texCoord).rgb * weights[0];
    float total = weights[0];
    [unroll] for (int k = 1; k < 5; ++k) {
        sum += Source.Sample(SourceSampler, texCoord + Step * k).rgb * weights[k];
        sum += Source.Sample(SourceSampler, texCoord - Step * k).rgb * weights[k];
        total += 2.0f * weights[k];
    }
    return float4(sum / total, 1.0f);
}

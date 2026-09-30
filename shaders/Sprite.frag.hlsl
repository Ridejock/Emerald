// Fragment shader for Emerald::Renderer2D sprites: texture color times the tint. The pipeline
// blends with straight alpha (src * a + dst * (1 - a)).

// SDL GPU's convention for fragment shader resources: textures and samplers in space2.
Texture2D<float4> SpriteTexture : register(t0, space2);
SamplerState SpriteSampler : register(s0, space2);

float4 main(float2 texCoord : TEXCOORD0, float4 color : TEXCOORD1) : SV_Target0
{
    return SpriteTexture.Sample(SpriteSampler, texCoord) * color;
}

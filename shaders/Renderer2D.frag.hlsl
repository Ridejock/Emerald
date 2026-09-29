// Fragment shader for Emerald::Renderer2D: the vertex color, alpha-blended by the pipeline.

float4 main(float4 color : TEXCOORD0) : SV_Target0
{
    return color;
}

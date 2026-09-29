// Fragment shader: outputs the color interpolated across the triangle.

float4 main(float4 color : TEXCOORD0) : SV_Target0
{
    return color;
}

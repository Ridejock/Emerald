// Vertex shader for fullscreen passes (the CRT post-process): one triangle that covers the
// screen, generated from SV_VertexID, so there is no vertex buffer. Draw it with 3 vertices.

struct Output
{
    float2 TexCoord : TEXCOORD0; // 0..1, (0, 0) = top left
    float4 Position : SV_Position;
};

Output main(uint id : SV_VertexID)
{
    // Vertices (0, 0), (2, 0), (0, 2) in texture space; the screen is the part inside 0..1.
    const float2 uv = float2((id << 1) & 2, id & 2);
    Output output;
    output.TexCoord = uv;
    output.Position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

#pragma once

#include <span>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Opaque GPU objects. The engine maps them onto its graphics backend; game code never includes
// that backend's headers, and never names its types.
struct GpuDevice;
struct GpuTexture;
struct GpuSampler;
struct GpuBuffer;
struct GpuShader;
struct GpuGraphicsPipeline;
struct GpuCommandBuffer;
struct GpuRenderPass;
struct GpuTransferBuffer;
struct GpuFence;

// Straight RGBA in 0..1. The clear color and a few GPU calls take this; UI and sprites use Vec4.
struct Color {
    f32 R = 0.0f;
    f32 G = 0.0f;
    f32 B = 0.0f;
    f32 A = 1.0f;
};

// Integer rectangle in render-target pixels, origin at the top-left. W or H of 0 means "no clip".
struct PixelRect {
    i32 X = 0;
    i32 Y = 0;
    i32 W = 0;
    i32 H = 0;
};

// Shader binary kinds the application ships (see cmake/Shaders.cmake). A backend is used only
// when it accepts one of these. The bits are stable; EMERALD_SHADER_FORMATS is the same mask.
enum class ShaderFormat : u32 {
    None = 0,
    Private = 1u << 0,
    Spirv = 1u << 1,
    Dxbc = 1u << 2,
    Dxil = 1u << 3,
    Msl = 1u << 4,
    Metallib = 1u << 5,
};

[[nodiscard]] constexpr ShaderFormat operator|(ShaderFormat a, ShaderFormat b)
{
    return static_cast<ShaderFormat>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr ShaderFormat operator&(ShaderFormat a, ShaderFormat b)
{
    return static_cast<ShaderFormat>(static_cast<u32>(a) & static_cast<u32>(b));
}
// The numeric mask from EMERALD_SHADER_FORMATS (SPIR-V is bit 1, DXIL bit 3, MSL bit 4).
[[nodiscard]] constexpr ShaderFormat ShaderFormatFromMask(u32 mask)
{
    return static_cast<ShaderFormat>(mask);
}

// Pixel formats the renderer names. Other backend formats still round-trip through this type
// (GetSwapchainFormat into a pipeline) without the game naming them.
enum class GpuTextureFormat : i32 {
    Invalid = 0,
    R8G8B8A8Unorm = 4,
    B8G8R8A8Unorm = 12,
    R16G16B16A16Float = 29,
    R8G8B8A8UnormSrgb = 52,
    B8G8R8A8UnormSrgb = 53,
};

enum class GpuBufferUsage : u32 {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Indirect = 1u << 2,
};

[[nodiscard]] constexpr GpuBufferUsage operator|(GpuBufferUsage a, GpuBufferUsage b)
{
    return static_cast<GpuBufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

// How one vertex attribute is stored. The values the engine's own shaders use are named here.
enum class VertexElementFormat : i32 {
    Float = 9,
    Float2 = 10,
    Float3 = 11,
    Float4 = 12,
    UByte4Norm = 20,
};

enum class VertexInputRate : u32 {
    Vertex = 0,
    Instance = 1,
};

enum class PrimitiveType : u32 {
    TriangleList = 0,
    TriangleStrip = 1,
    LineList = 2,
    LineStrip = 3,
    PointList = 4,
};

struct VertexBufferDesc {
    u32 Slot = 0;
    u32 Pitch = 0; // bytes from one vertex to the next
    VertexInputRate InputRate = VertexInputRate::Vertex;
    u32 InstanceStepRate = 0; // must be 0
};

struct VertexAttribute {
    u32 Location = 0;
    u32 BufferSlot = 0;
    VertexElementFormat Format = VertexElementFormat::Float;
    u32 Offset = 0;
};

// One vertex buffer bound for a draw.
struct GpuBufferBinding {
    GpuBuffer* Buffer = nullptr;
    u32 Offset = 0;
};

// Custom draws inside OnRender. Released objects must not still be in use by a submitted frame;
// the backend defers the real free until the GPU is done with them.
void BindGraphicsPipeline(GpuRenderPass* pass, GpuGraphicsPipeline* pipeline);
void BindVertexBuffers(GpuRenderPass* pass, u32 firstSlot, std::span<const GpuBufferBinding> bindings);
void DrawPrimitives(GpuRenderPass* pass, u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0,
                    u32 firstInstance = 0);
void PushVertexUniformData(GpuCommandBuffer* commandBuffer, u32 slot, const void* data, u32 size);
void ReleaseShader(GpuDevice* device, GpuShader* shader);
void ReleaseGraphicsPipeline(GpuDevice* device, GpuGraphicsPipeline* pipeline);
void ReleaseBuffer(GpuDevice* device, GpuBuffer* buffer);

} // namespace Emerald

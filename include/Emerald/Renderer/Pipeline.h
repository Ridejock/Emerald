#pragma once

#include "Emerald/Renderer/Gpu.h"

#include <span>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// The handful of settings a simple 2D/3D graphics pipeline needs. Everything else uses sensible
// defaults (no depth buffer, no culling, filled triangles, 1x MSAA).
struct GraphicsPipelineDesc {
    GpuShader* VertexShader = nullptr;
    GpuShader* FragmentShader = nullptr;

    // How vertex data is laid out: one entry per bound vertex buffer, one per shader input.
    std::span<const VertexBufferDesc> VertexBuffers;
    std::span<const VertexAttribute> VertexAttributes;

    // Must match the texture the pipeline renders into (usually the swapchain format).
    GpuTextureFormat ColorFormat = GpuTextureFormat::Invalid;
    PrimitiveType Primitive = PrimitiveType::TriangleList;
    bool AlphaBlend = false;
    // With AlphaBlend: add the color instead of blending over it (src * srcAlpha + dst), for glows.
    bool AdditiveBlend = false;
};

// Returns nullptr on failure (logged). Release with ReleaseGraphicsPipeline.
// Shaders can be released right after the pipeline is created.
[[nodiscard]] GpuGraphicsPipeline* CreateGraphicsPipeline(GpuDevice* device,
                                                              const GraphicsPipelineDesc& desc);

} // namespace Emerald

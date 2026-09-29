#pragma once

#include <span>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// The handful of settings a simple 2D/3D graphics pipeline needs. Everything else uses sensible
// defaults (no depth buffer, no culling, filled triangles, 1x MSAA).
struct GraphicsPipelineDesc {
    SDL_GPUShader* VertexShader = nullptr;
    SDL_GPUShader* FragmentShader = nullptr;

    // How vertex data is laid out: one entry per bound vertex buffer, one per shader input.
    std::span<const SDL_GPUVertexBufferDescription> VertexBuffers;
    std::span<const SDL_GPUVertexAttribute> VertexAttributes;

    // Must match the texture the pipeline renders into (usually the swapchain format).
    SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUPrimitiveType Primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    bool AlphaBlend = false;
};

// Returns nullptr on failure (logged). Release with SDL_ReleaseGPUGraphicsPipeline.
// Shaders can be released right after the pipeline is created.
[[nodiscard]] SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(SDL_GPUDevice* device,
                                                              const GraphicsPipelineDesc& desc);

} // namespace Emerald

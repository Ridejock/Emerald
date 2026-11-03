#include "Emerald/Renderer/Gpu.h"

#include <vector>

#include "Platform/SdlCast.h"

namespace Emerald {

void BindGraphicsPipeline(GpuRenderPass* pass, GpuGraphicsPipeline* pipeline)
{
    SDL_BindGPUGraphicsPipeline(Sdl(pass), Sdl(pipeline));
}

void BindVertexBuffers(GpuRenderPass* pass, u32 firstSlot, std::span<const GpuBufferBinding> bindings)
{
    std::vector<SDL_GPUBufferBinding> sdl(bindings.size());
    for (usize i = 0; i < bindings.size(); ++i)
        sdl[i] = {Sdl(bindings[i].Buffer), bindings[i].Offset};
    SDL_BindGPUVertexBuffers(Sdl(pass), firstSlot, sdl.data(), static_cast<u32>(sdl.size()));
}

void DrawPrimitives(GpuRenderPass* pass, u32 vertexCount, u32 instanceCount, u32 firstVertex,
                    u32 firstInstance)
{
    SDL_DrawGPUPrimitives(Sdl(pass), vertexCount, instanceCount, firstVertex, firstInstance);
}

void PushVertexUniformData(GpuCommandBuffer* commandBuffer, u32 slot, const void* data, u32 size)
{
    SDL_PushGPUVertexUniformData(Sdl(commandBuffer), slot, data, size);
}

void ReleaseShader(GpuDevice* device, GpuShader* shader)
{
    if (shader)
        SDL_ReleaseGPUShader(Sdl(device), Sdl(shader));
}

void ReleaseGraphicsPipeline(GpuDevice* device, GpuGraphicsPipeline* pipeline)
{
    if (pipeline)
        SDL_ReleaseGPUGraphicsPipeline(Sdl(device), Sdl(pipeline));
}

void ReleaseBuffer(GpuDevice* device, GpuBuffer* buffer)
{
    if (buffer)
        SDL_ReleaseGPUBuffer(Sdl(device), Sdl(buffer));
}

} // namespace Emerald

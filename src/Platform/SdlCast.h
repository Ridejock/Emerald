#pragma once

// Engine-only. Included from src/ (the library's private include directory), never from a
// public header. Game code does not see these conversions.

#include <optional>

#include <SDL3/SDL.h>

#include "Emerald/Audio/Audio.h"
#include "Emerald/Core/Event.h"
#include "Emerald/Core/Window.h"
#include "Emerald/Renderer/Gpu.h"

namespace Emerald {

inline SDL_Window* Sdl(NativeWindow* window)
{
    return reinterpret_cast<SDL_Window*>(window);
}
inline NativeWindow* FromSdl(SDL_Window* window)
{
    return reinterpret_cast<NativeWindow*>(window);
}

inline SDL_GPUDevice* Sdl(GpuDevice* device)
{
    return reinterpret_cast<SDL_GPUDevice*>(device);
}
inline GpuDevice* FromSdl(SDL_GPUDevice* device)
{
    return reinterpret_cast<GpuDevice*>(device);
}
inline SDL_GPUTexture* Sdl(GpuTexture* texture)
{
    return reinterpret_cast<SDL_GPUTexture*>(texture);
}
inline GpuTexture* FromSdl(SDL_GPUTexture* texture)
{
    return reinterpret_cast<GpuTexture*>(texture);
}
inline SDL_GPUSampler* Sdl(GpuSampler* sampler)
{
    return reinterpret_cast<SDL_GPUSampler*>(sampler);
}
inline GpuSampler* FromSdl(SDL_GPUSampler* sampler)
{
    return reinterpret_cast<GpuSampler*>(sampler);
}
inline SDL_GPUBuffer* Sdl(GpuBuffer* buffer)
{
    return reinterpret_cast<SDL_GPUBuffer*>(buffer);
}
inline GpuBuffer* FromSdl(SDL_GPUBuffer* buffer)
{
    return reinterpret_cast<GpuBuffer*>(buffer);
}
inline SDL_GPUShader* Sdl(GpuShader* shader)
{
    return reinterpret_cast<SDL_GPUShader*>(shader);
}
inline GpuShader* FromSdl(SDL_GPUShader* shader)
{
    return reinterpret_cast<GpuShader*>(shader);
}
inline SDL_GPUGraphicsPipeline* Sdl(GpuGraphicsPipeline* pipeline)
{
    return reinterpret_cast<SDL_GPUGraphicsPipeline*>(pipeline);
}
inline GpuGraphicsPipeline* FromSdl(SDL_GPUGraphicsPipeline* pipeline)
{
    return reinterpret_cast<GpuGraphicsPipeline*>(pipeline);
}
inline SDL_GPUCommandBuffer* Sdl(GpuCommandBuffer* commandBuffer)
{
    return reinterpret_cast<SDL_GPUCommandBuffer*>(commandBuffer);
}
inline GpuCommandBuffer* FromSdl(SDL_GPUCommandBuffer* commandBuffer)
{
    return reinterpret_cast<GpuCommandBuffer*>(commandBuffer);
}
inline SDL_GPURenderPass* Sdl(GpuRenderPass* pass)
{
    return reinterpret_cast<SDL_GPURenderPass*>(pass);
}
inline GpuRenderPass* FromSdl(SDL_GPURenderPass* pass)
{
    return reinterpret_cast<GpuRenderPass*>(pass);
}
inline SDL_GPUTransferBuffer* Sdl(GpuTransferBuffer* buffer)
{
    return reinterpret_cast<SDL_GPUTransferBuffer*>(buffer);
}
inline GpuTransferBuffer* FromSdl(SDL_GPUTransferBuffer* buffer)
{
    return reinterpret_cast<GpuTransferBuffer*>(buffer);
}
inline SDL_GPUFence* Sdl(GpuFence* fence)
{
    return reinterpret_cast<SDL_GPUFence*>(fence);
}
inline GpuFence* FromSdl(SDL_GPUFence* fence)
{
    return reinterpret_cast<GpuFence*>(fence);
}

inline SDL_AudioStream* Sdl(AudioStream* stream)
{
    return reinterpret_cast<SDL_AudioStream*>(stream);
}
inline AudioStream* FromSdl(SDL_AudioStream* stream)
{
    return reinterpret_cast<AudioStream*>(stream);
}
inline SDL_Gamepad* Sdl(GamepadDevice* pad)
{
    return reinterpret_cast<SDL_Gamepad*>(pad);
}
inline GamepadDevice* FromSdl(SDL_Gamepad* pad)
{
    return reinterpret_cast<GamepadDevice*>(pad);
}

inline SDL_FColor Sdl(Color color)
{
    return {color.R, color.G, color.B, color.A};
}
inline SDL_Rect Sdl(PixelRect rect)
{
    return {rect.X, rect.Y, rect.W, rect.H};
}
inline SDL_GPUShaderFormat Sdl(ShaderFormat format)
{
    return static_cast<SDL_GPUShaderFormat>(format);
}
inline SDL_GPUTextureFormat Sdl(GpuTextureFormat format)
{
    return static_cast<SDL_GPUTextureFormat>(format);
}
inline GpuTextureFormat FromSdl(SDL_GPUTextureFormat format)
{
    return static_cast<GpuTextureFormat>(format);
}
inline SDL_GPUBufferUsageFlags Sdl(GpuBufferUsage usage)
{
    return static_cast<SDL_GPUBufferUsageFlags>(usage);
}
inline SDL_GPUPrimitiveType Sdl(PrimitiveType type)
{
    return static_cast<SDL_GPUPrimitiveType>(type);
}
inline SDL_GPUVertexElementFormat Sdl(VertexElementFormat format)
{
    return static_cast<SDL_GPUVertexElementFormat>(format);
}
inline SDL_GPUVertexInputRate Sdl(VertexInputRate rate)
{
    return static_cast<SDL_GPUVertexInputRate>(rate);
}
inline SDL_Scancode Sdl(Key key)
{
    return static_cast<SDL_Scancode>(key);
}
inline SDL_GamepadType Sdl(GamepadType type)
{
    return static_cast<SDL_GamepadType>(type);
}
inline GamepadType FromSdl(SDL_GamepadType type)
{
    return static_cast<GamepadType>(type);
}
inline SDL_GamepadButton Sdl(GamepadButton button)
{
    return static_cast<SDL_GamepadButton>(button);
}

// Fills an Emerald event from a backend event. Empty when the engine does not expose that kind
// (joysticks that are not gamepads, finger events, and so on).
[[nodiscard]] std::optional<Event> TranslateEvent(const SDL_Event& event);

} // namespace Emerald

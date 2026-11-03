#pragma once

#include "Emerald/Renderer/Gpu.h"

#include <string_view>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Describes a shader compiled at build time by SDL_shadercross (see shaders/ and
// cmake/Shaders.cmake). Files live next to the executable in shaders/<Name>.<ext>:
//   .spv (Vulkan), .dxil (D3D12), .msl (Metal) and .json (reflection).
struct ShaderDesc {
    std::string_view Name; // e.g. "Triangle.vert" - the stage is inferred from .vert/.frag
    // Resource counts SDL needs to build the pipeline layout. If <Name>.json exists these are
    // read from the shadercross reflection data instead, so you rarely need to set them.
    u32 Samplers = 0;
    u32 StorageTextures = 0;
    u32 StorageBuffers = 0;
    u32 UniformBuffers = 0;
};

// Loads the variant of a shader matching the device's backend (SDL_GetGPUShaderFormats).
// Returns nullptr on failure (logged). Release with ReleaseShader.
[[nodiscard]] GpuShader* LoadShader(GpuDevice* device, const ShaderDesc& desc);

} // namespace Emerald

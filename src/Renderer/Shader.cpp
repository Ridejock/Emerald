#include "Emerald/Renderer/Shader.h"

#include <array>
#include <charconv>
#include <string>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// One compiled variant of a shader, as produced by cmake/Shaders.cmake.
struct ShaderVariant {
    SDL_GPUShaderFormat Format;
    const char* Extension;
    const char* EntryPoint;
};

// Tried in order; the first one the device accepts and that exists on disk wins.
// SPIRV-Cross renames `main` to `main0` when generating MSL (`main` is reserved in Metal).
constexpr std::array<ShaderVariant, 3> kVariants{{
    {SDL_GPU_SHADERFORMAT_SPIRV, "spv", "main"},
    {SDL_GPU_SHADERFORMAT_DXIL, "dxil", "main"},
    {SDL_GPU_SHADERFORMAT_MSL, "msl", "main0"},
}};

// Tiny helper to read `"key": 123` from the shadercross reflection JSON. The file is a single
// flat object written by shadercross, so a full JSON parser is not needed here.
bool ReadJsonCount(std::string_view json, std::string_view key, u32& out)
{
    const std::string quoted = "\"" + std::string(key) + "\"";
    usize pos = json.find(quoted);
    if (pos == std::string_view::npos)
        return false;
    pos = json.find(':', pos + quoted.size());
    if (pos == std::string_view::npos)
        return false;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string_view::npos)
        return false;
    return std::from_chars(json.data() + pos, json.data() + json.size(), out).ec == std::errc{};
}

void ApplyReflection(const std::string& jsonPath, ShaderDesc& desc)
{
    usize size = 0;
    void* data = SDL_LoadFile(jsonPath.c_str(), &size);
    if (!data)
        return; // no reflection data: keep the counts from the ShaderDesc
    const std::string_view json(static_cast<const char*>(data), size);
    ReadJsonCount(json, "samplers", desc.Samplers);
    ReadJsonCount(json, "storage_textures", desc.StorageTextures);
    ReadJsonCount(json, "storage_buffers", desc.StorageBuffers);
    ReadJsonCount(json, "uniform_buffers", desc.UniformBuffers);
    SDL_free(data);
}

} // namespace

SDL_GPUShader* LoadShader(SDL_GPUDevice* device, const ShaderDesc& inDesc)
{
    SDL_GPUShaderStage stage;
    if (inDesc.Name.ends_with(".vert")) {
        stage = SDL_GPU_SHADERSTAGE_VERTEX;
    } else if (inDesc.Name.ends_with(".frag")) {
        stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    } else {
        EM_CORE_ERROR("LoadShader: cannot infer stage of '{}' (expected .vert or .frag)",
                      inDesc.Name);
        return nullptr;
    }

    // Shaders are compiled next to the executable (see cmake/Shaders.cmake).
    const std::string basePath =
        std::string(SDL_GetBasePath()) + "shaders/" + std::string(inDesc.Name);

    ShaderDesc desc = inDesc;
    ApplyReflection(basePath + ".json", desc);

    // Which bytecode formats does this device's backend understand?
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    for (const ShaderVariant& variant : kVariants) {
        if (!(supported & variant.Format))
            continue;

        const std::string path = basePath + "." + variant.Extension;
        usize codeSize = 0;
        void* code = SDL_LoadFile(path.c_str(), &codeSize);
        if (!code) {
            EM_CORE_WARN("LoadShader: {} not found, trying other formats", path);
            continue;
        }

        SDL_GPUShaderCreateInfo info{};
        info.code = static_cast<const u8*>(code);
        info.code_size = codeSize;
        info.entrypoint = variant.EntryPoint;
        info.format = variant.Format;
        info.stage = stage;
        info.num_samplers = desc.Samplers;
        info.num_storage_textures = desc.StorageTextures;
        info.num_storage_buffers = desc.StorageBuffers;
        info.num_uniform_buffers = desc.UniformBuffers;

        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
        SDL_free(code);
        if (!shader) {
            EM_CORE_ERROR("SDL_CreateGPUShader({}) failed: {}", path, SDL_GetError());
            return nullptr;
        }
        EM_CORE_INFO("Loaded shader {}.{} (uniform buffers: {}, samplers: {})", inDesc.Name,
                     variant.Extension, desc.UniformBuffers, desc.Samplers);
        return shader;
    }

    EM_CORE_ERROR("LoadShader: no usable variant of '{}' for this GPU backend", inDesc.Name);
    return nullptr;
}

} // namespace Emerald

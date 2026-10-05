#include "Emerald/Renderer/PostEffect.h"

#include <algorithm>
#include <utility>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Renderer/Pipeline.h"
#include "Emerald/Renderer/Shader.h"

namespace Emerald {

namespace {

SDL_GPUTexture* CreateColorTarget(SDL_GPUDevice* device, SDL_GPUTextureFormat format, u32 width,
                                  u32 height)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = std::max(width, 1u);
    info.height = std::max(height, 1u);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    return SDL_CreateGPUTexture(device, &info);
}

} // namespace

PostChain::~PostChain()
{
    Shutdown();
}

bool PostChain::Init(SDL_GPUDevice* device, SDL_GPUTextureFormat format)
{
    m_Device = device;
    m_Format = format;
    m_Initialized = true;
    for (usize i = 0; i < m_Effects.size(); ++i) {
        if (!m_Effects[i]->Init(device, format)) {
            EM_CORE_ERROR("PostChain: effect '{}' failed to init", m_Effects[i]->GetName());
            // Drop the failed one and everything after that was not yet init'd this pass: redo
            // by shutting them all down and marking uninitialized so Add can retry later.
            for (auto& effect : m_Effects)
                effect->Shutdown();
            m_Initialized = false;
            return false;
        }
    }
    return true;
}

void PostChain::Shutdown()
{
    ReleaseTargets();
    for (auto& effect : m_Effects)
        effect->Shutdown();
    m_Effects.clear();
    m_Device = nullptr;
    m_Initialized = false;
}

usize PostChain::Add(std::unique_ptr<PostEffect> effect)
{
    if (!effect)
        return npos;
    if (m_Initialized && !effect->Init(m_Device, m_Format)) {
        EM_CORE_ERROR("PostChain: effect '{}' failed to init", effect->GetName());
        return npos;
    }
    m_Effects.push_back(std::move(effect));
    return m_Effects.size() - 1;
}

void PostChain::Remove(usize index)
{
    if (index >= m_Effects.size())
        return;
    m_Effects[index]->Shutdown();
    m_Effects.erase(m_Effects.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_Effects.empty())
        ReleaseTargets();
}

void PostChain::Clear()
{
    for (auto& effect : m_Effects)
        effect->Shutdown();
    m_Effects.clear();
    ReleaseTargets();
}

void PostChain::Move(usize from, usize to)
{
    if (from >= m_Effects.size() || to >= m_Effects.size() || from == to)
        return;
    std::unique_ptr<PostEffect> effect = std::move(m_Effects[from]);
    m_Effects.erase(m_Effects.begin() + static_cast<std::ptrdiff_t>(from));
    m_Effects.insert(m_Effects.begin() + static_cast<std::ptrdiff_t>(to), std::move(effect));
}

PostEffect* PostChain::Get(usize index)
{
    return index < m_Effects.size() ? m_Effects[index].get() : nullptr;
}

const PostEffect* PostChain::Get(usize index) const
{
    return index < m_Effects.size() ? m_Effects[index].get() : nullptr;
}

PostEffect* PostChain::Find(std::string_view name)
{
    for (auto& effect : m_Effects)
        if (effect->GetName() == name)
            return effect.get();
    return nullptr;
}

const PostEffect* PostChain::Find(std::string_view name) const
{
    for (const auto& effect : m_Effects)
        if (effect->GetName() == name)
            return effect.get();
    return nullptr;
}

void PostChain::ReleaseTargets()
{
    for (Target* target : {&m_Scene, &m_Ping}) {
        if (target->Texture && m_Device)
            SDL_ReleaseGPUTexture(m_Device, target->Texture);
        *target = {};
    }
}

bool PostChain::EnsureTargets(u32 width, u32 height)
{
    if (!m_Device)
        return false;
    const auto make = [&](Target& target) {
        if (target.Texture && target.Width == width && target.Height == height)
            return true;
        if (target.Texture)
            SDL_ReleaseGPUTexture(m_Device, target.Texture);
        target = {.Texture = CreateColorTarget(m_Device, m_Format, width, height),
                  .Width = width,
                  .Height = height};
        return target.Texture != nullptr;
    };
    if (!make(m_Scene) || (m_Effects.size() > 1 && !make(m_Ping))) {
        EM_CORE_ERROR("PostChain: could not create {}x{} targets: {}", width, height,
                      SDL_GetError());
        ReleaseTargets();
        return false;
    }
    return true;
}

SDL_GPUTexture* PostChain::GetSceneTarget(u32 width, u32 height)
{
    if (m_Effects.empty())
        return nullptr;
    return EnsureTargets(width, height) ? m_Scene.Texture : nullptr;
}

void PostChain::Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target, f32 deltaSeconds)
{
    if (m_Effects.empty() || !m_Scene.Texture || !target)
        return;
    const u32 width = m_Scene.Width;
    const u32 height = m_Scene.Height;
    // One effect: scene -> target. Several: scene -> ping -> scene -> ... -> target.
    SDL_GPUTexture* source = m_Scene.Texture;
    for (usize i = 0; i < m_Effects.size(); ++i) {
        const bool last = i + 1 == m_Effects.size();
        SDL_GPUTexture* dest = last ? target : ((i % 2 == 0) ? m_Ping.Texture : m_Scene.Texture);
        m_Effects[i]->Apply(commandBuffer, source, dest, width, height, deltaSeconds);
        source = dest;
    }
}

// --- TintEffect ---

bool TintEffect::Init(SDL_GPUDevice* device, SDL_GPUTextureFormat format)
{
    Shutdown();
    m_Device = device;
    SDL_GPUShader* vertex = LoadShader(device, {.Name = "Fullscreen.vert"});
    SDL_GPUShader* fragment = LoadShader(device, {.Name = "Tint.frag"});
    if (vertex && fragment) {
        m_Pipeline = CreateGraphicsPipeline(device, {.VertexShader = vertex,
                                                     .FragmentShader = fragment,
                                                     .VertexBuffers = {},
                                                     .VertexAttributes = {},
                                                     .ColorFormat = format});
    }
    if (vertex)
        SDL_ReleaseGPUShader(device, vertex);
    if (fragment)
        SDL_ReleaseGPUShader(device, fragment);
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_Sampler = SDL_CreateGPUSampler(device, &sampler);
    if (!m_Pipeline || !m_Sampler) {
        EM_CORE_ERROR("TintEffect: could not create its pipeline");
        Shutdown();
        return false;
    }
    return true;
}

void TintEffect::Shutdown()
{
    if (!m_Device)
        return;
    if (m_Pipeline)
        SDL_ReleaseGPUGraphicsPipeline(m_Device, m_Pipeline);
    if (m_Sampler)
        SDL_ReleaseGPUSampler(m_Device, m_Sampler);
    m_Pipeline = nullptr;
    m_Sampler = nullptr;
    m_Device = nullptr;
}

void TintEffect::Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* source,
                       SDL_GPUTexture* target, u32, u32, f32)
{
    if (!m_Pipeline || !source || !target)
        return;
    struct Uniforms {
        f32 Color[3];
        f32 Vignette;
    } uniforms{{m_Params.Color.x, m_Params.Color.y, m_Params.Color.z}, m_Params.Vignette};
    SDL_GPUColorTargetInfo color{};
    color.texture = target;
    color.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &color, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, m_Pipeline);
    const SDL_GPUTextureSamplerBinding binding{source, m_Sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

} // namespace Emerald

#include "Emerald/Renderer/PostEffect.h"

#include <algorithm>
#include <utility>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Renderer/Pipeline.h"
#include "Emerald/Renderer/Shader.h"
#include "Platform/SdlCast.h"

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

bool PostChain::Init(GpuDevice* device, GpuTextureFormat format)
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
    SDL_GPUDevice* sdl = m_Device ? Sdl(m_Device) : nullptr;
    for (Target* target : {&m_Scene, &m_Ping}) {
        if (target->Texture && sdl)
            SDL_ReleaseGPUTexture(sdl, Sdl(target->Texture));
        *target = {};
    }
}

bool PostChain::EnsureTargets(u32 width, u32 height)
{
    if (!m_Device)
        return false;
    SDL_GPUDevice* sdl = Sdl(m_Device);
    const auto make = [&](Target& target) {
        if (target.Texture && target.Width == width && target.Height == height)
            return true;
        if (target.Texture)
            SDL_ReleaseGPUTexture(sdl, Sdl(target.Texture));
        target = {.Texture = FromSdl(CreateColorTarget(sdl, Sdl(m_Format), width, height)),
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

GpuTexture* PostChain::GetSceneTarget(u32 width, u32 height)
{
    if (m_Effects.empty())
        return nullptr;
    return EnsureTargets(width, height) ? m_Scene.Texture : nullptr;
}

void PostChain::Apply(GpuCommandBuffer* commandBuffer, GpuTexture* target, f32 deltaSeconds)
{
    if (m_Effects.empty() || !m_Scene.Texture || !target)
        return;
    SDL_GPUCommandBuffer* cmd = Sdl(commandBuffer);
    const u32 width = m_Scene.Width;
    const u32 height = m_Scene.Height;
    // One effect: scene -> target. Several: scene -> ping -> scene -> ... -> target.
    GpuTexture* source = m_Scene.Texture;
    for (usize i = 0; i < m_Effects.size(); ++i) {
        const bool last = i + 1 == m_Effects.size();
        GpuTexture* dest = last ? target : ((i % 2 == 0) ? m_Ping.Texture : m_Scene.Texture);
        if (!m_Effects[i]->Apply(commandBuffer, source, dest, width, height, deltaSeconds)) {
            // It could not run: pass the image on unchanged, so `target` is always written.
            SDL_GPUBlitInfo blit{};
            blit.source.texture = Sdl(source);
            blit.source.w = width;
            blit.source.h = height;
            blit.destination.texture = Sdl(dest);
            blit.destination.w = width;
            blit.destination.h = height;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        }
        source = dest;
    }
}

// --- TintEffect ---

bool TintEffect::Init(GpuDevice* device, GpuTextureFormat format)
{
    Shutdown();
    m_Device = device;
    SDL_GPUDevice* sdl = Sdl(device);
    GpuShader* vertex = LoadShader(device, {.Name = "Fullscreen.vert"});
    GpuShader* fragment = LoadShader(device, {.Name = "Tint.frag"});
    if (vertex && fragment) {
        m_Pipeline = CreateGraphicsPipeline(device, {.VertexShader = vertex,
                                                     .FragmentShader = fragment,
                                                     .VertexBuffers = {},
                                                     .VertexAttributes = {},
                                                     .ColorFormat = format});
    }
    if (vertex)
        SDL_ReleaseGPUShader(sdl, Sdl(vertex));
    if (fragment)
        SDL_ReleaseGPUShader(sdl, Sdl(fragment));
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_Sampler = FromSdl(SDL_CreateGPUSampler(sdl, &sampler));
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
    SDL_GPUDevice* sdl = Sdl(m_Device);
    if (m_Pipeline)
        SDL_ReleaseGPUGraphicsPipeline(sdl, Sdl(m_Pipeline));
    if (m_Sampler)
        SDL_ReleaseGPUSampler(sdl, Sdl(m_Sampler));
    m_Pipeline = nullptr;
    m_Sampler = nullptr;
    m_Device = nullptr;
}

bool TintEffect::Apply(GpuCommandBuffer* commandBuffer, GpuTexture* source, GpuTexture* target,
                       u32, u32, f32)
{
    if (!m_Pipeline || !source || !target)
        return false;
    struct Uniforms {
        f32 Color[3];
        f32 Vignette;
    } uniforms{{m_Params.Color.x, m_Params.Color.y, m_Params.Color.z}, m_Params.Vignette};
    SDL_GPUCommandBuffer* cmd = Sdl(commandBuffer);
    SDL_GPUColorTargetInfo color{};
    color.texture = Sdl(target);
    color.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, Sdl(m_Pipeline));
    const SDL_GPUTextureSamplerBinding binding{Sdl(source), Sdl(m_Sampler)};
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    return true;
}

} // namespace Emerald

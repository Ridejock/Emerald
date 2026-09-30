#include "Emerald/Renderer/CrtEffect.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Renderer/Pipeline.h"
#include "Emerald/Renderer/Shader.h"

namespace Emerald {

namespace {

// Uniforms of CrtPhosphor.frag and CrtBlur.frag (16 bytes each, like their cbuffers).
struct PhosphorUniforms {
    f32 Decay;
    f32 Padding[3];
};
struct BlurUniforms {
    f32 Step[2];
    f32 Padding[2];
};

// Near glow: sigma = 0.8% of the screen height per unit of BloomRadius; the wide one is 3x.
constexpr f32 kNearSigma = 0.008f;
constexpr f32 kWideScale = 3.0f;
constexpr f32 kTapsPerSigma = 2.0f; // CrtBlur.frag's Gaussian has sigma = 2 taps

SDL_GPUTexture* CreateTarget(SDL_GPUDevice* device, SDL_GPUTextureFormat format, u32 width,
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

SDL_GPUGraphicsPipeline* CreateFullscreenPipeline(SDL_GPUDevice* device, const char* fragmentName,
                                                  SDL_GPUTextureFormat format)
{
    SDL_GPUShader* vertex = LoadShader(device, {.Name = "Fullscreen.vert"});
    SDL_GPUShader* fragment = LoadShader(device, {.Name = fragmentName});
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    if (vertex && fragment) {
        // No vertex buffers: the vertex shader makes the triangle from SV_VertexID.
        pipeline = CreateGraphicsPipeline(device, {.VertexShader = vertex,
                                                   .FragmentShader = fragment,
                                                   .VertexBuffers = {},
                                                   .VertexAttributes = {},
                                                   .ColorFormat = format});
    }
    if (vertex)
        SDL_ReleaseGPUShader(device, vertex);
    if (fragment)
        SDL_ReleaseGPUShader(device, fragment);
    return pipeline;
}

} // namespace

CrtEffect::~CrtEffect()
{
    Shutdown();
}

bool CrtEffect::Init(SDL_GPUDevice* device, SDL_GPUTextureFormat outputFormat)
{
    Shutdown();
    m_Device = device;
    m_OutputFormat = outputFormat;

    // Float targets keep faint afterglow and glow gradients smooth; 8-bit is the fallback.
    constexpr SDL_GPUTextureUsageFlags usage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    m_WorkFormat = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                                                SDL_GPU_TEXTURETYPE_2D, usage)
                       ? SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
                       : outputFormat;

    m_PhosphorPipeline = CreateFullscreenPipeline(device, "CrtPhosphor.frag", m_WorkFormat);
    m_BlurPipeline = CreateFullscreenPipeline(device, "CrtBlur.frag", m_WorkFormat);
    m_CompositePipeline = CreateFullscreenPipeline(device, "Crt.frag", outputFormat);

    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_Sampler = SDL_CreateGPUSampler(device, &sampler);

    if (!m_PhosphorPipeline || !m_BlurPipeline || !m_CompositePipeline || !m_Sampler) {
        EM_CORE_ERROR("CrtEffect: could not create its pipelines (are the engine shaders "
                      "Fullscreen, CrtPhosphor, CrtBlur and Crt built?)");
        Shutdown();
        return false;
    }
    return true;
}

void CrtEffect::Shutdown()
{
    if (!m_Device)
        return;
    // SDL defers releasing GPU objects until in-flight frames are done with them.
    ReleaseTargets();
    for (SDL_GPUGraphicsPipeline** pipeline :
         {&m_PhosphorPipeline, &m_BlurPipeline, &m_CompositePipeline}) {
        if (*pipeline)
            SDL_ReleaseGPUGraphicsPipeline(m_Device, *pipeline);
        *pipeline = nullptr;
    }
    if (m_Sampler)
        SDL_ReleaseGPUSampler(m_Device, m_Sampler);
    m_Sampler = nullptr;
    m_Device = nullptr;
}

void CrtEffect::ReleaseTargets()
{
    Target* targets[] = {&m_Scene,    &m_History[0], &m_History[1], &m_Bloom[0],
                         &m_Bloom[1], &m_Bloom[2],   &m_Bloom[3]};
    for (Target* target : targets) {
        if (target->Texture)
            SDL_ReleaseGPUTexture(m_Device, target->Texture);
        *target = {};
    }
    m_HistoryValid = false;
}

bool CrtEffect::CreateTargets(u32 width, u32 height)
{
    ReleaseTargets();
    const auto make = [&](Target& target, SDL_GPUTextureFormat format, u32 w, u32 h) {
        target = {.Texture = CreateTarget(m_Device, format, w, h),
                  .Width = std::max(w, 1u),
                  .Height = std::max(h, 1u)};
        return target.Texture != nullptr;
    };
    bool ok = make(m_Scene, m_OutputFormat, width, height);
    ok = make(m_History[0], m_WorkFormat, width, height) && ok;
    ok = make(m_History[1], m_WorkFormat, width, height) && ok;
    ok = make(m_Bloom[0], m_WorkFormat, width / 2, height / 2) && ok;
    ok = make(m_Bloom[1], m_WorkFormat, width / 2, height / 2) && ok;
    ok = make(m_Bloom[2], m_WorkFormat, width / 4, height / 4) && ok;
    ok = make(m_Bloom[3], m_WorkFormat, width / 4, height / 4) && ok;
    if (!ok) {
        EM_CORE_ERROR("CrtEffect: could not create its {}x{} targets: {}", width, height,
                      SDL_GetError());
        ReleaseTargets();
    }
    return ok;
}

SDL_GPUTexture* CrtEffect::GetSceneTarget(u32 width, u32 height)
{
    if (!m_Device)
        return nullptr;
    if (!m_Scene.Texture || m_Scene.Width != width || m_Scene.Height != height)
        CreateTargets(width, height);
    return m_Scene.Texture;
}

void CrtEffect::RunPass(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUGraphicsPipeline* pipeline,
                        const Target& target, std::span<SDL_GPUTexture* const> sources,
                        const void* uniforms, u32 uniformSize)
{
    SDL_GPUColorTargetInfo color{};
    color.texture = target.Texture;
    color.load_op = SDL_GPU_LOADOP_DONT_CARE; // every pixel is overwritten
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commandBuffer, &color, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUTextureSamplerBinding bindings[3]{};
    const u32 count = static_cast<u32>(std::min<usize>(sources.size(), 3));
    for (u32 i = 0; i < count; ++i)
        bindings[i] = {.texture = sources[i], .sampler = m_Sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, count);
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, uniforms, uniformSize);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void CrtEffect::Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target, f32 deltaSeconds)
{
    if (!m_Device || !m_Scene.Texture)
        return;
    const u32 width = m_Scene.Width;
    const u32 height = m_Scene.Height;

    // 1. Phosphor: this frame over the faded previous one.
    const Target& history = m_History[m_Current];
    const Target& previous = m_History[1 - m_Current];
    const bool afterglow = m_Params.Afterglow > 0.0f && m_HistoryValid;
    const PhosphorUniforms phosphor{
        .Decay = afterglow ? AfterglowDecay(m_Params.Afterglow, deltaSeconds) : 0.0f,
        .Padding = {}};
    SDL_GPUTexture* const phosphorSources[] = {m_Scene.Texture,
                                               afterglow ? previous.Texture : m_Scene.Texture};
    RunPass(commandBuffer, m_PhosphorPipeline, history, phosphorSources, &phosphor,
            sizeof(phosphor));
    m_HistoryValid = m_Params.Afterglow > 0.0f;
    m_Current = 1 - m_Current;

    // 2. Bloom: horizontal then vertical blur, near glow at 1/2 size, wide glow at 1/4.
    for (u32 level = 0; level < 2; ++level) {
        const f32 step = BlurStepPixels(m_Params, height, level == 1);
        SDL_GPUTexture* const source = level == 0 ? history.Texture : m_Bloom[1].Texture;
        const BlurUniforms horizontal{.Step = {step / static_cast<f32>(width), 0.0f},
                                      .Padding = {}};
        const BlurUniforms vertical{.Step = {0.0f, step / static_cast<f32>(height)}, .Padding = {}};
        Target& across = m_Bloom[level * 2];
        Target& down = m_Bloom[level * 2 + 1];
        RunPass(commandBuffer, m_BlurPipeline, across, {&source, 1}, &horizontal,
                sizeof(horizontal));
        RunPass(commandBuffer, m_BlurPipeline, down, {&across.Texture, 1}, &vertical,
                sizeof(vertical));
    }

    // 3. Composite into the real target.
    const CrtUniforms uniforms = MakeUniforms(m_Params, width, height);
    SDL_GPUTexture* const sources[] = {history.Texture, m_Bloom[1].Texture, m_Bloom[3].Texture};
    const Target output{.Texture = target, .Width = width, .Height = height};
    RunPass(commandBuffer, m_CompositePipeline, output, sources, &uniforms, sizeof(uniforms));
}

f32 CrtEffect::AfterglowDecay(f32 halfLifeSeconds, f32 deltaSeconds)
{
    if (halfLifeSeconds <= 0.0f)
        return 0.0f;
    // Clamped, so a long hitch does not freeze the trail and a zero dt does not keep it forever.
    const f32 dt = std::clamp(deltaSeconds, 0.001f, 0.25f);
    return std::pow(0.5f, dt / halfLifeSeconds);
}

f32 CrtEffect::BlurStepPixels(const CrtParams& params, u32 height, bool wide)
{
    const f32 sigma = params.BloomRadius * kNearSigma * static_cast<f32>(height);
    return sigma * (wide ? kWideScale : 1.0f) / kTapsPerSigma;
}

CrtUniforms CrtEffect::MakeUniforms(const CrtParams& params, u32 width, u32 height)
{
    const f32 w = static_cast<f32>(std::max(width, 1u));
    const f32 h = static_cast<f32>(std::max(height, 1u));
    // ChromaticOffset is in pixels at 1080p, so the fringe looks the same at any resolution.
    const f32 chromaPixels = params.ChromaticOffset * h / 1080.0f;
    return {.Size = {w, h},
            .Chroma = {chromaPixels / w, chromaPixels / h},
            .Curvature = params.Curvature,
            .Bloom = params.Bloom,
            .Vignette = params.Vignette,
            .Brightness = params.Brightness,
            .Scanlines = std::clamp(params.Scanlines, 0.0f, 1.0f),
            .ScanlineCount = params.ScanlineCount,
            .Mask = std::clamp(params.Mask, 0.0f, 1.0f),
            .EdgeSoftness = 2.0f / h};
}

} // namespace Emerald

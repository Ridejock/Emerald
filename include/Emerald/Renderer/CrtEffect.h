#pragma once

#include <span>
#include <string_view>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Renderer/PostEffect.h"

namespace Emerald {

// The look of the CRT post-process. Every strength is 0 = off. The defaults suit a vector (XY)
// monitor: curved glass, phosphor glow and a short afterglow, but no scanlines or mask (a
// vector monitor draws lines with the beam and has neither); turn those up for a raster look.
struct CrtParams {
    f32 Curvature = 0.07f;      // barrel distortion of the glass; the corners round off
    f32 Bloom = 2.0f;           // phosphor glow added on top of the image
    f32 BloomRadius = 1.0f;     // glow spread; 1 = a near glow of ~0.8% of the screen height
    f32 Vignette = 0.35f;       // darkening towards the corners
    f32 ChromaticOffset = 1.0f; // red/blue fringe at the screen edges, in pixels at 1080p
    f32 Afterglow = 0.035f;     // seconds for the phosphor trail to fade to half (0 = off)
    f32 Scanlines = 0.0f;       // darkness of the gaps between scanlines (0..1)
    f32 ScanlineCount = 360.0f; // scanlines over the screen height
    f32 Mask = 0.0f;            // RGB aperture grille strength (0..1)
    f32 Brightness = 1.0f;      // overall gain at the end
};

// Fragment uniforms of the final pass (shaders/Crt.frag.hlsl), laid out like its cbuffer.
struct CrtUniforms {
    f32 Size[2];
    f32 Chroma[2];
    f32 Curvature;
    f32 Bloom;
    f32 Vignette;
    f32 Brightness;
    f32 Scanlines;
    f32 ScanlineCount;
    f32 Mask;
    f32 EdgeSoftness;
};

// Optional fullscreen CRT monitor post-process (one entry in Application's PostChain):
//   1. phosphor: the source over the fading previous frame (afterglow, 16-bit float history)
//   2. bloom: separable Gaussian blurs at 1/2 and 1/4 size (a near and a wide glow)
//   3. composite: curvature, bloom, chromatic offset, vignette, scanlines and mask
// Turn it on with Application::SetCrtEnabled (adds/removes this effect from the chain).
class CrtEffect final : public PostEffect {
public:
    CrtEffect() = default;
    ~CrtEffect();

    CrtEffect(const CrtEffect&) = delete;
    CrtEffect& operator=(const CrtEffect&) = delete;

    bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat outputFormat) override;
    void Shutdown() override;
    // Reads `source` (the previous chain step or the scene), writes `target`.
    void Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* source, SDL_GPUTexture* target,
               u32 width, u32 height, f32 deltaSeconds) override;
    [[nodiscard]] std::string_view GetName() const override { return "CRT"; }

    // Forgets the afterglow, e.g. after the effect was off for a while.
    void ResetAfterglow() { m_HistoryValid = false; }

    [[nodiscard]] CrtParams& GetParams() { return m_Params; }
    [[nodiscard]] const CrtParams& GetParams() const { return m_Params; }
    void SetParams(const CrtParams& params) { m_Params = params; }

    // How much of the old phosphor image is left after one frame: 0.5^(dt / half-life).
    [[nodiscard]] static f32 AfterglowDecay(f32 halfLifeSeconds, f32 deltaSeconds);
    // The final pass's uniforms for an output of `width` x `height` pixels.
    [[nodiscard]] static CrtUniforms MakeUniforms(const CrtParams& params, u32 width, u32 height);
    // Distance between blur taps in output pixels, for the near or the wide (3x) glow.
    [[nodiscard]] static f32 BlurStepPixels(const CrtParams& params, u32 height, bool wide);

private:
    // An offscreen texture with its size.
    struct Target {
        SDL_GPUTexture* Texture = nullptr;
        u32 Width = 0;
        u32 Height = 0;
    };
    bool CreateTargets(u32 width, u32 height);
    void ReleaseTargets();
    // Work targets only (history + bloom); the chain owns the scene / ping textures.
    void RunPass(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUGraphicsPipeline* pipeline,
                 const Target& target, std::span<SDL_GPUTexture* const> sources,
                 const void* uniforms, u32 uniformSize);

    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUTextureFormat m_OutputFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat m_WorkFormat = SDL_GPU_TEXTUREFORMAT_INVALID; // history and bloom
    SDL_GPUGraphicsPipeline* m_PhosphorPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_BlurPipeline = nullptr;
    SDL_GPUGraphicsPipeline* m_CompositePipeline = nullptr;
    SDL_GPUSampler* m_Sampler = nullptr; // linear, clamp to edge

    Target m_History[2]; // phosphor ping-pong: this frame's and last frame's
    Target m_Bloom[4];   // near: [0] horizontal, [1] vertical at 1/2; wide: [2], [3] at 1/4
    u32 m_Current = 0;   // index of this frame's history target
    bool m_HistoryValid = false;
    CrtParams m_Params;
};

} // namespace Emerald

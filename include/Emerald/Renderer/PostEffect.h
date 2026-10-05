#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec3.h"

namespace Emerald {

// One fullscreen pass (or several) in Application's post-effect chain. Reads `source`, writes
// `target` (outside any render pass). The chain owns the offscreen textures and the order.
class PostEffect {
public:
    virtual ~PostEffect() = default;

    virtual bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat format) = 0;
    virtual void Shutdown() = 0;
    // `width` x `height` is the size of both textures (the swapchain's for the last effect).
    virtual void Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* source,
                       SDL_GPUTexture* target, u32 width, u32 height, f32 deltaSeconds) = 0;
    [[nodiscard]] virtual std::string_view GetName() const = 0;
};

// Runs zero or more PostEffects between the scene and the swapchain. Empty: the scene draws
// straight to the swapchain (no extra passes, no offscreen texture). Effects can be added,
// removed and reordered at runtime; each is Init'd when added (and when the chain is first
// initialized).
class PostChain {
public:
    PostChain() = default;
    ~PostChain();
    PostChain(const PostChain&) = delete;
    PostChain& operator=(const PostChain&) = delete;

    // Remembers the device and format; Init's any effects already added. Returns false if an
    // effect fails (logged). Call once after the renderer exists.
    bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat format);
    void Shutdown();

    // Takes ownership. Init's the effect when the chain is already initialized. Returns its
    // index, or npos on Init failure (the effect is discarded).
    usize Add(std::unique_ptr<PostEffect> effect);
    void Remove(usize index);
    void Clear();
    // Moves the effect at `from` to `to` (other effects shift). No-op if either is out of range.
    void Move(usize from, usize to);

    [[nodiscard]] usize GetCount() const { return m_Effects.size(); }
    [[nodiscard]] PostEffect* Get(usize index);
    [[nodiscard]] const PostEffect* Get(usize index) const;
    // First effect with this name, or nullptr.
    [[nodiscard]] PostEffect* Find(std::string_view name);
    [[nodiscard]] const PostEffect* Find(std::string_view name) const;
    [[nodiscard]] bool IsEmpty() const { return m_Effects.empty(); }

    // The texture to render the scene into when the chain is not empty; nullptr when it is
    // (render to the swapchain). (Re)created at `width` x `height`.
    [[nodiscard]] SDL_GPUTexture* GetSceneTarget(u32 width, u32 height);
    // Runs every effect in order into `target`. No-op when empty.
    void Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* target, f32 deltaSeconds);

    static constexpr usize npos = static_cast<usize>(-1);

private:
    struct Target {
        SDL_GPUTexture* Texture = nullptr;
        u32 Width = 0;
        u32 Height = 0;
    };
    bool EnsureTargets(u32 width, u32 height);
    void ReleaseTargets();

    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUTextureFormat m_Format = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool m_Initialized = false;
    std::vector<std::unique_ptr<PostEffect>> m_Effects;
    Target m_Scene; // the scene draws here
    Target m_Ping;  // between effects when there are two or more
};

// A cheap grade: multiply RGB and optional vignette. One fullscreen pass; useful as a second
// chain entry next to the CRT (the sandbox's lighting scene uses it).
struct TintParams {
    Vec3 Color{1.0f, 1.0f, 1.0f};
    f32 Vignette = 0.0f; // 0 = none, 1 = black corners
};

class TintEffect final : public PostEffect {
public:
    [[nodiscard]] TintParams& GetParams() { return m_Params; }
    [[nodiscard]] const TintParams& GetParams() const { return m_Params; }
    void SetParams(const TintParams& params) { m_Params = params; }

    bool Init(SDL_GPUDevice* device, SDL_GPUTextureFormat format) override;
    void Shutdown() override;
    void Apply(SDL_GPUCommandBuffer* commandBuffer, SDL_GPUTexture* source, SDL_GPUTexture* target,
               u32 width, u32 height, f32 deltaSeconds) override;
    [[nodiscard]] std::string_view GetName() const override { return "Tint"; }

private:
    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;
    SDL_GPUSampler* m_Sampler = nullptr;
    TintParams m_Params;
};

} // namespace Emerald

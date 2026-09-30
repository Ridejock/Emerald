#pragma once

#include <filesystem>
#include <optional>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

struct Image;

enum class TextureFilter : u8 {
    Nearest, // crisp, blocky pixels when scaled: pixel art
    Linear,  // smooth blending between pixels: photos, painted art
};

enum class TextureWrap : u8 {
    Clamp,  // outside 0..1 the edge pixels repeat (no bleeding from the opposite side)
    Repeat, // the image tiles
};

struct TextureOptions {
    TextureFilter Filter = TextureFilter::Nearest;
    TextureWrap Wrap = TextureWrap::Clamp;
};

// An RGBA8 image on the GPU plus the sampler that reads it. Owns both and releases them when it
// is destroyed (RAII); it can be moved but not copied.
//
//   std::optional<Texture> ship = Texture::Load(device, "assets/ship.png");
//   renderer2D.DrawSprite(*ship, {100, 100});
//
// Pixels are straight (not premultiplied) alpha, matching Renderer2D's blending. A texture must
// stay alive until the frames that draw it have been rendered (Renderer2D keeps no reference
// after Render); SDL defers the actual GPU release until the GPU is done with it.
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Uploads `image` right away (own command buffer + copy pass, submitted before any later
    // frame). Returns nullopt on failure (logged).
    [[nodiscard]] static std::optional<Texture> Create(SDL_GPUDevice* device, const Image& image,
                                                       const TextureOptions& options = {});
    // Loads a PNG/JPG/... with stb_image (see Image) and uploads it.
    [[nodiscard]] static std::optional<Texture> Load(SDL_GPUDevice* device,
                                                     const std::filesystem::path& path,
                                                     const TextureOptions& options = {});
    // A texture with a size but no GPU resources: sprite batching works (UVs, commands), drawing
    // is skipped. For tests and headless tools.
    [[nodiscard]] static Texture CreateWithoutGpu(u32 width, u32 height);

    [[nodiscard]] u32 GetWidth() const { return m_Width; }
    [[nodiscard]] u32 GetHeight() const { return m_Height; }
    [[nodiscard]] Vec2 GetSize() const
    {
        return {static_cast<f32>(m_Width), static_cast<f32>(m_Height)};
    }
    // Unique per texture (0 = none), used to batch sprites that share a texture.
    [[nodiscard]] u32 GetId() const { return m_Id; }
    [[nodiscard]] bool HasGpuTexture() const { return m_Texture != nullptr; }
    [[nodiscard]] SDL_GPUTexture* GetGpuTexture() const { return m_Texture; }
    [[nodiscard]] SDL_GPUSampler* GetSampler() const { return m_Sampler; }

private:
    void Release();

    SDL_GPUDevice* m_Device = nullptr;
    SDL_GPUTexture* m_Texture = nullptr;
    SDL_GPUSampler* m_Sampler = nullptr;
    u32 m_Width = 0;
    u32 m_Height = 0;
    u32 m_Id = 0;
};

} // namespace Emerald

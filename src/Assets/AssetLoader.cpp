#include "Emerald/Assets/AssetLoader.h"

#include <vector>

#include "Emerald/Audio/Sound.h"

namespace Emerald {

Sound AssetLoader::MakePlaceholderSound()
{
    const usize frames = static_cast<usize>(kMixSpec.freq) / 10;
    return Sound(std::vector<f32>(frames * static_cast<usize>(kMixSpec.channels), 0.0f));
}

Image MakeCheckerImage()
{
    Image image;
    image.Width = 64;
    image.Height = 64;
    image.Pixels.resize(static_cast<usize>(image.Width * image.Height) * 4);
    for (i32 y = 0; y < image.Height; ++y) {
        for (i32 x = 0; x < image.Width; ++x) {
            const bool magenta = ((x / 8) + (y / 8)) % 2 == 0;
            u8* p = &image.Pixels[static_cast<usize>(y * image.Width + x) * 4];
            p[0] = magenta ? 255 : 0;
            p[1] = 0;
            p[2] = magenta ? 255 : 0;
            p[3] = 255;
        }
    }
    return image;
}

std::optional<Texture> GpuAssetLoader::LoadTexture(const std::filesystem::path& file,
                                                   const TextureOptions& options)
{
    return Texture::Load(m_Device, file, options);
}

std::optional<TextureAtlas> GpuAssetLoader::LoadAtlas(const std::filesystem::path& image,
                                                      const std::filesystem::path& json,
                                                      const TextureOptions& options)
{
    return TextureAtlas::Load(m_Device, image, json, options);
}

std::optional<Font> GpuAssetLoader::LoadFont(const std::filesystem::path& file,
                                             const FontOptions& options)
{
    return Font::Load(m_Device, file, options);
}

std::optional<Sound> GpuAssetLoader::LoadSound(const std::filesystem::path& file)
{
    return Emerald::LoadSound(file);
}

std::optional<Tilemap> GpuAssetLoader::LoadTilemap(const std::filesystem::path& file,
                                                   const TextureOptions& options)
{
    return Tilemap::Load(m_Device, file, options);
}

Texture GpuAssetLoader::MakePlaceholderTexture()
{
    // Nearest keeps the squares sharp when the placeholder is drawn large.
    if (!m_Device)
        return Texture::CreateWithoutGpu(64, 64); // tests and tools: right size, nothing to draw
    if (std::optional<Texture> texture =
            Texture::Create(m_Device, MakeCheckerImage(), {.Filter = TextureFilter::Nearest}))
        return std::move(*texture);
    return Texture::CreateWithoutGpu(64, 64); // the upload failed (logged)
}

TextureAtlas GpuAssetLoader::MakePlaceholderAtlas()
{
    return TextureAtlas::CreatePlaceholder(MakePlaceholderTexture());
}

Font GpuAssetLoader::MakePlaceholderFont(const FontOptions& options)
{
    return Font::CreatePlaceholder(m_Device, options);
}

} // namespace Emerald

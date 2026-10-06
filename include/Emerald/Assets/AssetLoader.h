#pragma once

#include <filesystem>
#include <optional>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Audio/Sound.h"
#include "Emerald/Dialogue/Dialogue.h"
#include "Emerald/Particles/ParticleEffect.h"
#include "Emerald/Renderer/Font.h"
#include "Emerald/Renderer/Texture.h"
#include "Emerald/Renderer/TextureAtlas.h"
#include "Emerald/Tilemap/Tilemap.h"

namespace Emerald {

// How the asset manager (Assets.h) turns files into objects. The manager itself only does the
// bookkeeping - which files are loaded, who uses them, when they changed - and asks a loader
// for the actual work. That split keeps the bookkeeping testable without a GPU: tests give the
// manager a loader that makes GPU-less stand-ins (see tests/AssetTests.cpp).
//
// Load functions return nullopt when a file is missing or broken (and log why). The Make...
// functions build placeholders for that case; they must always succeed.
class AssetLoader {
public:
    virtual ~AssetLoader() = default;

    [[nodiscard]] virtual std::optional<Texture> LoadTexture(const std::filesystem::path& file,
                                                             const TextureOptions& options) = 0;
    [[nodiscard]] virtual std::optional<TextureAtlas> LoadAtlas(const std::filesystem::path& image,
                                                                const std::filesystem::path& json,
                                                                const TextureOptions& options) = 0;
    [[nodiscard]] virtual std::optional<Font> LoadFont(const std::filesystem::path& file,
                                                       const FontOptions& options) = 0;
    [[nodiscard]] virtual std::optional<Sound> LoadSound(const std::filesystem::path& file) = 0;
    [[nodiscard]] virtual std::optional<Tilemap> LoadTilemap(const std::filesystem::path& file,
                                                             const TextureOptions& options) = 0;

    [[nodiscard]] virtual Texture MakePlaceholderTexture() = 0;
    [[nodiscard]] virtual TextureAtlas MakePlaceholderAtlas() = 0;
    [[nodiscard]] virtual Font MakePlaceholderFont(const FontOptions& options) = 0;
    // A tenth of a second of silence (the same for every loader).
    [[nodiscard]] virtual Sound MakePlaceholderSound();
    // An empty map (no layers, no size), the same for every loader.
    [[nodiscard]] virtual Tilemap MakePlaceholderTilemap() { return {}; }
    // Dialogue decks are plain data (no GPU): every loader reads them the same way.
    [[nodiscard]] virtual std::optional<DialogueDeck>
    LoadDialogueDeck(const std::filesystem::path& file)
    {
        return LoadDialogue(file);
    }
    // A deck without cards: starting it logs "no card" and ends at once.
    [[nodiscard]] virtual DialogueDeck MakePlaceholderDialogue() { return {}; }
    // Particle effects too; the placeholder is the default config (white dots, 1 s).
    [[nodiscard]] virtual std::optional<ParticleEffect>
    LoadParticleEffectFile(const std::filesystem::path& file)
    {
        return LoadParticleEffect(file);
    }
    [[nodiscard]] virtual ParticleEffect MakePlaceholderParticleEffect() { return {}; }
};

// The real loader: files from disk, textures uploaded to `device`.
//
// Placeholders: a magenta/black checkerboard texture (hard to miss, and the classic "texture
// missing" look), an atlas made of that texture (every sprite and animation shows the whole
// checkerboard), a font that draws every character as a hollow box, silence, and an empty map.
class GpuAssetLoader final : public AssetLoader {
public:
    explicit GpuAssetLoader(SDL_GPUDevice* device) : m_Device(device) {}

    [[nodiscard]] std::optional<Texture> LoadTexture(const std::filesystem::path& file,
                                                     const TextureOptions& options) override;
    [[nodiscard]] std::optional<TextureAtlas> LoadAtlas(const std::filesystem::path& image,
                                                        const std::filesystem::path& json,
                                                        const TextureOptions& options) override;
    [[nodiscard]] std::optional<Font> LoadFont(const std::filesystem::path& file,
                                               const FontOptions& options) override;
    [[nodiscard]] std::optional<Sound> LoadSound(const std::filesystem::path& file) override;
    [[nodiscard]] std::optional<Tilemap> LoadTilemap(const std::filesystem::path& file,
                                                     const TextureOptions& options) override;

    [[nodiscard]] Texture MakePlaceholderTexture() override;
    [[nodiscard]] TextureAtlas MakePlaceholderAtlas() override;
    [[nodiscard]] Font MakePlaceholderFont(const FontOptions& options) override;

private:
    SDL_GPUDevice* m_Device = nullptr;
};

// The 64 x 64 magenta/black checkerboard (8 x 8 pixel squares) used for missing textures.
[[nodiscard]] Image MakeCheckerImage();

} // namespace Emerald

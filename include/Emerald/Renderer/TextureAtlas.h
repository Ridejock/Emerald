#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/Texture.h"

namespace Emerald {

// One texture with many named sprites in it, described by a JSON file:
//
//   { "ship":  { "x": 1,  "y": 1, "w": 38, "h": 80 },
//     "enemy": { "x": 40, "y": 1, "w": 80, "h": 46 } }
//
// (TexturePacker's "JSON (Hash)" export, { "frames": { "ship": { "frame": {x, y, w, h} } } },
// works too.)
//
//   std::optional<TextureAtlas> atlas = TextureAtlas::Load(device, "atlas.png", "atlas.json");
//   const Sprite ship = atlas->Get("ship");
//   r.DrawSprite(ship, position, {.Rotation = angle});
//
// The texture lives on the heap, so sprites stay valid when the atlas is moved; they must not
// outlive the atlas.
class TextureAtlas {
public:
    using RegionMap = std::map<std::string, TextureRegion, std::less<>>;

    // Loads the image and the JSON. Regions outside the image are dropped (with a warning).
    [[nodiscard]] static std::optional<TextureAtlas> Load(SDL_GPUDevice* device,
                                                          const std::filesystem::path& image,
                                                          const std::filesystem::path& json,
                                                          const TextureOptions& options = {});
    // An atlas from an existing texture and regions (e.g. a Texture::CreateWithoutGpu in tests).
    [[nodiscard]] static TextureAtlas Create(Texture texture, RegionMap regions);
    // Just the JSON part. Returns nullopt if the text is not valid JSON or has no usable region
    // (entries without numeric x, y, w, h are skipped with a warning).
    [[nodiscard]] static std::optional<RegionMap> ParseRegions(std::string_view json);

    [[nodiscard]] std::optional<Sprite> Find(std::string_view name) const;
    // Like Find, but logs an error for unknown names and returns the whole texture instead, so a
    // typo shows up on screen rather than crashing.
    [[nodiscard]] Sprite Get(std::string_view name) const;
    [[nodiscard]] bool Contains(std::string_view name) const { return m_Regions.contains(name); }

    [[nodiscard]] const Texture& GetTexture() const { return *m_Texture; }
    [[nodiscard]] const RegionMap& GetRegions() const { return m_Regions; }

private:
    std::unique_ptr<Texture> m_Texture;
    RegionMap m_Regions;
};

} // namespace Emerald

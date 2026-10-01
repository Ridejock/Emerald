#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Renderer/Animation.h"
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
// An optional "animations" object names frame sequences, by a list of sprite names or a name
// pattern ("{}" = 0, 1, 2 ... while those sprites exist, or from "from" to "to"):
//
//   "animations": {
//     "idle": { "frames": ["hero_0", "hero_1"], "durations": [1.2, 0.1] },
//     "walk": { "pattern": "hero_walk_{}", "duration": 0.12 },
//     "jump": { "pattern": "hero_jump_{}", "from": 0, "to": 2, "mode": "once" },
//     "coin": { "pattern": "coin_{}", "duration": 0.08, "mode": "pingpong" } }
//
// "duration" (seconds, default 0.1) applies to every frame unless "durations" has one per frame;
// "mode" is "loop" (default), "once" or "pingpong". A plain array of names is a looping
// animation. Frames that name no sprite are reported (GetMissingFrames, logged) and left out.
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
    // An "animations" entry as written in the JSON, before its frames are looked up.
    struct AnimationDef {
        std::vector<std::string> Frames; // explicit names, or empty when Pattern is used
        std::string Pattern;             // "{}" stands for the frame number
        i32 From = 0;
        i32 To = -1; // -1: up to the last existing number
        f32 Duration = 0.1f;
        std::vector<f32> Durations; // per frame; ignored unless one per frame
        AnimationMode Mode = AnimationMode::Loop;
    };
    using AnimationDefMap = std::map<std::string, AnimationDef, std::less<>>;
    using AnimationMap = std::map<std::string, Animation, std::less<>>;

    // Loads the image and the JSON. Regions outside the image are dropped (with a warning).
    [[nodiscard]] static std::optional<TextureAtlas> Load(SDL_GPUDevice* device,
                                                          const std::filesystem::path& image,
                                                          const std::filesystem::path& json,
                                                          const TextureOptions& options = {});
    // An atlas from an existing texture and regions (e.g. a Texture::CreateWithoutGpu in tests).
    [[nodiscard]] static TextureAtlas Create(Texture texture, RegionMap regions,
                                             const AnimationDefMap& animations = {});
    // Just the JSON part. Returns nullopt if the text is not valid JSON or has no usable region
    // (entries without numeric x, y, w, h are skipped with a warning).
    [[nodiscard]] static std::optional<RegionMap> ParseRegions(std::string_view json);
    // The "animations" object (empty if there is none; malformed entries are skipped, logged).
    [[nodiscard]] static AnimationDefMap ParseAnimations(std::string_view json);

    [[nodiscard]] std::optional<Sprite> Find(std::string_view name) const;
    // Like Find, but logs an error for unknown names and returns the whole texture instead, so a
    // typo shows up on screen rather than crashing.
    [[nodiscard]] Sprite Get(std::string_view name) const;
    [[nodiscard]] bool Contains(std::string_view name) const { return m_Regions.contains(name); }

    [[nodiscard]] const Animation* FindAnimation(std::string_view name) const;
    // Like FindAnimation, but logs unknown names and returns an empty animation (draws nothing).
    [[nodiscard]] const Animation& GetAnimation(std::string_view name) const;
    [[nodiscard]] const AnimationMap& GetAnimations() const { return m_Animations; }
    // "animation: frame" for every animation frame that names no sprite.
    [[nodiscard]] const std::vector<std::string>& GetMissingFrames() const
    {
        return m_MissingFrames;
    }

    [[nodiscard]] const Texture& GetTexture() const { return *m_Texture; }
    [[nodiscard]] const RegionMap& GetRegions() const { return m_Regions; }

private:
    void ResolveAnimations(const AnimationDefMap& defs);

    std::unique_ptr<Texture> m_Texture;
    RegionMap m_Regions;
    AnimationMap m_Animations;
    std::vector<std::string> m_MissingFrames;
};

} // namespace Emerald

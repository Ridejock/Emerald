#include "Emerald/Renderer/TextureAtlas.h"

#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "Emerald/Core/Log.h"

namespace Emerald {

std::optional<TextureAtlas::RegionMap> TextureAtlas::ParseRegions(std::string_view json)
{
    // No exceptions: parse errors give a "discarded" value instead.
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        EM_CORE_ERROR("TextureAtlas: not a JSON object");
        return std::nullopt;
    }
    // TexturePacker puts the entries under "frames" and each rectangle under "frame".
    const nlohmann::json& entries =
        root.contains("frames") && root["frames"].is_object() ? root["frames"] : root;

    RegionMap regions;
    for (const auto& [name, entry] : entries.items()) {
        if (&entries == &root && (name == "animations" || name == "meta"))
            continue; // not a sprite
        const nlohmann::json& rect =
            entry.is_object() && entry.contains("frame") ? entry["frame"] : entry;
        const auto number = [&](const char* key) {
            return rect.is_object() && rect.contains(key) && rect[key].is_number();
        };
        if (!number("x") || !number("y") || !number("w") || !number("h")) {
            EM_CORE_WARN("TextureAtlas: '{}' needs numeric x, y, w, h; skipped", name);
            continue;
        }
        regions[name] = {{rect["x"].get<f32>(), rect["y"].get<f32>()},
                         {rect["w"].get<f32>(), rect["h"].get<f32>()}};
    }
    if (regions.empty()) {
        EM_CORE_ERROR("TextureAtlas: no regions found");
        return std::nullopt;
    }
    return regions;
}

TextureAtlas::AnimationDefMap TextureAtlas::ParseAnimations(std::string_view json)
{
    AnimationDefMap defs;
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("animations"))
        return defs;
    const nlohmann::json& list = root["animations"];
    if (!list.is_object()) {
        EM_CORE_WARN("TextureAtlas: \"animations\" must be an object; ignored");
        return defs;
    }
    const auto names = [](const nlohmann::json& array, std::vector<std::string>& out) {
        for (const nlohmann::json& n : array)
            if (n.is_string())
                out.push_back(n.get<std::string>());
        return !out.empty();
    };
    for (const auto& [name, entry] : list.items()) {
        AnimationDef def;
        bool ok = false;
        if (entry.is_array()) {
            ok = names(entry, def.Frames); // a plain list of names: looping, default duration
        } else if (entry.is_object()) {
            if (entry.contains("frames") && entry["frames"].is_array())
                ok = names(entry["frames"], def.Frames);
            else if (entry.contains("pattern") && entry["pattern"].is_string()) {
                def.Pattern = entry["pattern"].get<std::string>();
                ok = def.Pattern.find("{}") != std::string::npos;
            }
            def.From = entry.value("from", 0);
            def.To = entry.value("to", -1);
            def.Duration = entry.value("duration", def.Duration);
            if (entry.contains("durations") && entry["durations"].is_array())
                for (const nlohmann::json& d : entry["durations"])
                    def.Durations.push_back(d.is_number() ? d.get<f32>() : def.Duration);
            const std::string mode = entry.value("mode", std::string("loop"));
            if (mode == "once")
                def.Mode = AnimationMode::Once;
            else if (mode == "pingpong" || mode == "ping-pong")
                def.Mode = AnimationMode::PingPong;
            else if (mode != "loop")
                EM_CORE_WARN("TextureAtlas: animation '{}' has unknown mode '{}'; looping", name,
                             mode);
        }
        if (!ok) {
            EM_CORE_WARN("TextureAtlas: animation '{}' needs \"frames\" or a \"pattern\" with "
                         "{{}}; skipped",
                         name);
            continue;
        }
        defs[name] = std::move(def);
    }
    return defs;
}

void TextureAtlas::ResolveAnimations(const AnimationDefMap& defs)
{
    for (const auto& [name, def] : defs) {
        std::vector<std::string> frames = def.Frames;
        if (!def.Pattern.empty()) {
            const auto numbered = [&](i32 i) {
                std::string n = def.Pattern;
                n.replace(n.find("{}"), 2, std::to_string(i));
                return n;
            };
            if (def.To >= 0) {
                for (i32 i = def.From; i <= def.To; ++i)
                    frames.push_back(numbered(i));
            } else {
                for (i32 i = def.From; m_Regions.contains(numbered(i)); ++i)
                    frames.push_back(numbered(i));
                if (frames.empty())
                    frames.push_back(numbered(def.From)); // reported as missing below
            }
        }
        if (!def.Durations.empty() && def.Durations.size() != frames.size())
            EM_CORE_WARN("TextureAtlas: animation '{}' has {} durations for {} frames; using {}s",
                         name, def.Durations.size(), frames.size(), def.Duration);
        const bool perFrame = def.Durations.size() == frames.size();

        Animation animation{.Name = name, .Frames = {}, .Mode = def.Mode};
        for (usize i = 0; i < frames.size(); ++i) {
            const auto region = m_Regions.find(frames[i]);
            if (region == m_Regions.end()) {
                m_MissingFrames.push_back(name + ": " + frames[i]);
                EM_CORE_WARN("TextureAtlas: animation '{}' uses missing sprite '{}'; left out",
                             name, frames[i]);
                continue;
            }
            animation.Frames.push_back({.Image = {m_Texture.get(), region->second},
                                        .Duration = perFrame ? def.Durations[i] : def.Duration});
        }
        if (animation.Frames.empty()) {
            EM_CORE_WARN("TextureAtlas: animation '{}' has no frames; skipped", name);
            continue;
        }
        m_Animations[name] = std::move(animation);
    }
}

const Animation* TextureAtlas::FindAnimation(std::string_view name) const
{
    const auto it = m_Animations.find(name);
    return it == m_Animations.end() ? nullptr : &it->second;
}

const Animation& TextureAtlas::GetAnimation(std::string_view name) const
{
    if (const Animation* animation = FindAnimation(name))
        return *animation;
    EM_CORE_ERROR("TextureAtlas: no animation named '{}'", name);
    static const Animation empty;
    return empty;
}

TextureAtlas TextureAtlas::Create(Texture texture, RegionMap regions,
                                  const AnimationDefMap& animations)
{
    TextureAtlas atlas;
    atlas.m_Texture = std::make_unique<Texture>(std::move(texture));
    // Drop regions that reach outside the texture (they would sample the clamped edge).
    const Vec2 size = atlas.m_Texture->GetSize();
    for (auto it = regions.begin(); it != regions.end();) {
        const TextureRegion& r = it->second;
        const bool inside = r.Position.x >= 0.0f && r.Position.y >= 0.0f && r.Size.x > 0.0f &&
                            r.Size.y > 0.0f && r.Position.x + r.Size.x <= size.x &&
                            r.Position.y + r.Size.y <= size.y;
        if (inside) {
            ++it;
        } else {
            EM_CORE_WARN("TextureAtlas: '{}' is outside the {}x{} texture; skipped", it->first,
                         atlas.m_Texture->GetWidth(), atlas.m_Texture->GetHeight());
            it = regions.erase(it);
        }
    }
    atlas.m_Regions = std::move(regions);
    atlas.ResolveAnimations(animations);
    return atlas;
}

std::optional<TextureAtlas> TextureAtlas::Load(SDL_GPUDevice* device,
                                               const std::filesystem::path& image,
                                               const std::filesystem::path& json,
                                               const TextureOptions& options)
{
    std::ifstream file(json, std::ios::binary);
    if (!file) {
        EM_CORE_ERROR("TextureAtlas: cannot open {}", json.string());
        return std::nullopt;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::optional<RegionMap> regions = ParseRegions(text.str());
    if (!regions) {
        EM_CORE_ERROR("TextureAtlas: {} is not a usable atlas description", json.string());
        return std::nullopt;
    }
    std::optional<Texture> texture = Texture::Load(device, image, options);
    if (!texture)
        return std::nullopt;
    TextureAtlas atlas =
        Create(std::move(*texture), std::move(*regions), ParseAnimations(text.str()));
    EM_CORE_INFO("Loaded atlas {} with {} sprites and {} animations", json.string(),
                 atlas.m_Regions.size(), atlas.m_Animations.size());
    return atlas;
}

std::optional<Sprite> TextureAtlas::Find(std::string_view name) const
{
    const auto it = m_Regions.find(name);
    if (it == m_Regions.end() || !m_Texture)
        return std::nullopt;
    return Sprite{m_Texture.get(), it->second};
}

Sprite TextureAtlas::Get(std::string_view name) const
{
    if (std::optional<Sprite> sprite = Find(name))
        return *sprite;
    EM_CORE_ERROR("TextureAtlas: no sprite named '{}'", name);
    return m_Texture ? Sprite::FromTexture(*m_Texture) : Sprite{};
}

} // namespace Emerald

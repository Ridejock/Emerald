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

TextureAtlas TextureAtlas::Create(Texture texture, RegionMap regions)
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
    TextureAtlas atlas = Create(std::move(*texture), std::move(*regions));
    EM_CORE_INFO("Loaded atlas {} with {} sprites", json.string(), atlas.m_Regions.size());
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

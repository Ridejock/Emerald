#include "Emerald/Tilemap/Tilemap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

using Json = nlohmann::json;
namespace fs = std::filesystem;

// --- Properties -------------------------------------------------------------------------------

void Properties::Set(std::string name, Value value)
{
    for (auto& [n, v] : m_Items) {
        if (n == name) {
            v = std::move(value);
            return;
        }
    }
    m_Items.emplace_back(std::move(name), std::move(value));
}

const Properties::Value* Properties::Find(std::string_view name) const
{
    for (const auto& [n, v] : m_Items)
        if (n == name)
            return &v;
    return nullptr;
}

bool Properties::GetBool(std::string_view name, bool fallback) const
{
    const Value* v = Find(name);
    return v && std::holds_alternative<bool>(*v) ? std::get<bool>(*v) : fallback;
}

i64 Properties::GetInt(std::string_view name, i64 fallback) const
{
    const Value* v = Find(name);
    if (v && std::holds_alternative<i64>(*v))
        return std::get<i64>(*v);
    if (v && std::holds_alternative<f64>(*v))
        return static_cast<i64>(std::get<f64>(*v));
    return fallback;
}

f64 Properties::GetFloat(std::string_view name, f64 fallback) const
{
    const Value* v = Find(name);
    if (v && std::holds_alternative<f64>(*v))
        return std::get<f64>(*v);
    if (v && std::holds_alternative<i64>(*v))
        return static_cast<f64>(std::get<i64>(*v));
    return fallback;
}

std::string_view Properties::GetString(std::string_view name, std::string_view fallback) const
{
    const Value* v = Find(name);
    return v && std::holds_alternative<std::string>(*v)
               ? std::string_view(std::get<std::string>(*v))
               : fallback;
}

// --- Tiles ------------------------------------------------------------------------------------

TileTransform GetTileTransform(u32 cell)
{
    const bool x = (cell & kTileFlipX) != 0;
    const bool y = (cell & kTileFlipY) != 0;
    if ((cell & kTileFlipDiag) == 0)
        return {.FlipX = x, .FlipY = y, .Rotation = 0.0f};
    // Diagonal = flip Y, then turn clockwise. The X / Y flips that Tiled applies afterwards
    // swap axes when moved before the turn: X becomes a Y flip and Y an X flip.
    return {.FlipX = y, .FlipY = !x, .Rotation = HalfPi};
}

Sprite Tileset::GetSprite(u32 index) const
{
    const u32 columns = Max(Columns, 1u);
    const Vec2 size(TileSize);
    const Vec2 cell(static_cast<f32>(index % columns), static_cast<f32>(index / columns));
    const Vec2 position =
        Vec2(static_cast<f32>(Margin)) + cell * (size + Vec2(static_cast<f32>(Spacing)));
    return {Sheet.get(), {position, size}};
}

// --- Loading ----------------------------------------------------------------------------------

namespace {

// Typed reads that never throw: a missing key or another type gives the fallback.
i64 GetInt(const Json& j, const char* key, i64 fallback = 0)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<i64>() : fallback;
}

f32 GetFloat(const Json& j, const char* key, f32 fallback = 0.0f)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<f32>() : fallback;
}

bool GetBool(const Json& j, const char* key, bool fallback = false)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string GetString(const Json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Tiled 1.9 wrote "class" where other versions write "type".
std::string GetClass(const Json& j)
{
    std::string type = GetString(j, "type");
    return type.empty() ? GetString(j, "class") : type;
}

// Which collision wins when layers overlap: solid, then slope, then one-way.
i32 Priority(TileCollision c)
{
    switch (c) {
    case TileCollision::Solid:
        return 3;
    case TileCollision::Slope:
        return 2;
    case TileCollision::OneWay:
        return 1;
    case TileCollision::None:
        break;
    }
    return 0;
}

// The file as a JSON object, or null (logged) if it is missing or not valid JSON.
std::optional<Json> ReadJson(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        EM_CORE_ERROR("Tilemap: cannot open {}", file.string());
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    const std::string s = text.str();
    Json json = Json::parse(s.begin(), s.end(), nullptr, false); // no exceptions
    if (json.is_discarded() || !json.is_object()) {
        EM_CORE_ERROR("Tilemap: {} is not a JSON object", file.string());
        return std::nullopt;
    }
    return json;
}

} // namespace

// Fills a Tilemap from Tiled's JSON. Every function logs what is wrong and returns false.
struct TilemapLoader {
    Tilemap& Map;
    fs::path File; // the .tmj, for messages
    SDL_GPUDevice* Device = nullptr;
    TextureOptions Options;

    template <typename... Args> bool Fail(spdlog::format_string_t<Args...> format, Args&&... args)
    {
        EM_CORE_ERROR("Tilemap: {}: {}", File.string(),
                      spdlog::fmt_lib::format(format, std::forward<Args>(args)...));
        return false;
    }

    // Tiled's [{"name", "type", "value"}] list.
    void ReadProperties(const Json& owner, Properties& out)
    {
        const auto it = owner.find("properties");
        if (it == owner.end() || !it->is_array())
            return;
        for (const Json& p : *it) {
            const std::string name = GetString(p, "name");
            const auto value = p.find("value");
            if (name.empty() || value == p.end())
                continue;
            if (value->is_boolean())
                out.Set(name, value->get<bool>());
            else if (value->is_number_integer())
                out.Set(name, value->get<i64>());
            else if (value->is_number())
                out.Set(name, value->get<f64>());
            else if (value->is_string())
                out.Set(name, value->get<std::string>());
            else
                EM_CORE_WARN("Tilemap: {}: property '{}' ({}) is not supported; skipped",
                             File.string(), name, GetString(p, "type"));
        }
    }

    // A tileset's fields (the map's entry, or the .tsj it points to); `dir` is where its image
    // path is relative to.
    bool ReadTileset(const Json& json, const fs::path& dir, Tileset& set)
    {
        set.Name = GetString(json, "name");
        set.TileSize = {static_cast<i32>(GetInt(json, "tilewidth")),
                        static_cast<i32>(GetInt(json, "tileheight"))};
        set.Margin = static_cast<i32>(GetInt(json, "margin"));
        set.Spacing = static_cast<i32>(GetInt(json, "spacing"));
        if (const auto offset = json.find("tileoffset"); offset != json.end())
            set.DrawOffset = {GetFloat(*offset, "x"), GetFloat(*offset, "y")};
        ReadProperties(json, set.Props);
        if (set.TileSize.x <= 0 || set.TileSize.y <= 0)
            return Fail("tileset '{}' has no tile size", set.Name);
        const std::string image = GetString(json, "image");
        if (image.empty())
            return Fail("tileset '{}' has no single image (image collections are not supported)",
                        set.Name);
        set.Image = (dir / image).lexically_normal();

        // The image: a texture, or with no device just its size.
        if (Device) {
            std::optional<Texture> texture = Texture::Load(Device, set.Image, Options);
            if (!texture)
                return Fail("cannot load tileset image {}", set.Image.string());
            set.Sheet = std::make_unique<Texture>(std::move(*texture));
        } else {
            const std::optional<Image> pixels = Image::LoadFromFile(set.Image);
            if (!pixels)
                return Fail("cannot load tileset image {}", set.Image.string());
            set.Sheet = std::make_unique<Texture>(Texture::CreateWithoutGpu(
                static_cast<u32>(pixels->Width), static_cast<u32>(pixels->Height)));
        }

        // Columns and count from the image if the file leaves them out.
        const i32 stepX = set.TileSize.x + set.Spacing;
        const i32 stepY = set.TileSize.y + set.Spacing;
        const i32 fitX =
            (static_cast<i32>(set.Sheet->GetWidth()) - 2 * set.Margin + set.Spacing) / stepX;
        const i32 fitY =
            (static_cast<i32>(set.Sheet->GetHeight()) - 2 * set.Margin + set.Spacing) / stepY;
        set.Columns = static_cast<u32>(GetInt(json, "columns", fitX));
        set.TileCount = static_cast<u32>(GetInt(json, "tilecount", fitX * fitY));
        if (set.Columns == 0 || set.TileCount == 0 || fitX <= 0 || fitY <= 0)
            return Fail("tileset '{}': image {} holds no {}x{} tiles", set.Name, set.Image.string(),
                        set.TileSize.x, set.TileSize.y);

        // Per-tile class and properties, and the collision they mean.
        set.Tiles.assign(set.TileCount, {});
        if (const auto tiles = json.find("tiles"); tiles != json.end() && tiles->is_array()) {
            for (const Json& t : *tiles) {
                const i64 id = GetInt(t, "id", -1);
                if (id < 0 || id >= static_cast<i64>(set.TileCount)) {
                    EM_CORE_WARN("Tilemap: {}: tileset '{}' has data for tile {} (of {}); skipped",
                                 File.string(), set.Name, id, set.TileCount);
                    continue;
                }
                TileInfo& info = set.Tiles[static_cast<usize>(id)];
                info.Type = GetClass(t);
                ReadProperties(t, info.Props);
                const bool slope = info.Props.Has("slopeLeft") || info.Props.Has("slopeRight") ||
                                   info.Type == "slope";
                if (info.Props.GetBool("solid") || info.Type == "solid")
                    info.Collision = TileCollision::Solid;
                else if (slope)
                    info.Collision = TileCollision::Slope;
                else if (info.Props.GetBool("oneway") || info.Type == "oneway")
                    info.Collision = TileCollision::OneWay;
                // Heights clamped to the tile, so a typo cannot make a floor outside it.
                const f64 height = static_cast<f64>(set.TileSize.y);
                info.Slope = {
                    .Left = static_cast<f32>(Clamp(info.Props.GetFloat("slopeLeft"), 0.0, height)),
                    .Right =
                        static_cast<f32>(Clamp(info.Props.GetFloat("slopeRight"), 0.0, height))};
            }
        }
        return true;
    }

    bool ReadTilesets(const Json& root)
    {
        const auto list = root.find("tilesets");
        if (list == root.end())
            return true; // a map without tiles
        if (!list->is_array())
            return Fail("\"tilesets\" is not an array");
        const fs::path mapDir = File.parent_path();
        for (const Json& entry : *list) {
            Tileset set;
            set.FirstGid = static_cast<u32>(GetInt(entry, "firstgid", 0));
            if (set.FirstGid == 0)
                return Fail("a tileset has no firstgid");
            const std::string source = GetString(entry, "source");
            if (source.empty()) {
                if (!ReadTileset(entry, mapDir, set)) // embedded
                    return false;
            } else {
                set.Source = (mapDir / source).lexically_normal();
                if (set.Source.extension() != ".tsj" && set.Source.extension() != ".json")
                    return Fail("tileset {} is not JSON; in Tiled, export or save it as .tsj",
                                set.Source.string());
                const std::optional<Json> external = ReadJson(set.Source);
                if (!external || !ReadTileset(*external, set.Source.parent_path(), set))
                    return Fail("cannot use tileset {}", set.Source.string());
                Map.m_Files.push_back(set.Source);
            }
            Map.m_Files.push_back(set.Image);
            Map.m_Tilesets.push_back(std::move(set));
        }
        std::sort(Map.m_Tilesets.begin(), Map.m_Tilesets.end(),
                  [](const Tileset& a, const Tileset& b) { return a.FirstGid < b.FirstGid; });
        return true;
    }

    MapObject ReadObject(const Json& j)
    {
        MapObject o;
        o.Id = static_cast<u32>(GetInt(j, "id"));
        o.Name = GetString(j, "name");
        o.Type = GetClass(j);
        o.Position = {GetFloat(j, "x"), GetFloat(j, "y")};
        o.Size = {GetFloat(j, "width"), GetFloat(j, "height")};
        o.Rotation = ToRadians(GetFloat(j, "rotation"));
        o.Visible = GetBool(j, "visible", true);
        ReadProperties(j, o.Props);
        if (const i64 gid = GetInt(j, "gid"); gid > 0) {
            o.Shape = ObjectShape::Tile;
            o.Gid = static_cast<u32>(gid);
            o.Position.y -= o.Size.y; // Tiled anchors tile objects at the bottom-left
        } else if (GetBool(j, "point")) {
            o.Shape = ObjectShape::Point;
        } else if (GetBool(j, "ellipse")) {
            o.Shape = ObjectShape::Ellipse;
        } else if (j.contains("text")) {
            o.Shape = ObjectShape::Text;
        }
        for (const char* key : {"polygon", "polyline"}) {
            const auto points = j.find(key);
            if (points == j.end() || !points->is_array())
                continue;
            o.Shape = key[4] == 'g' ? ObjectShape::Polygon : ObjectShape::Polyline;
            for (const Json& p : *points)
                o.Points.push_back({GetFloat(p, "x"), GetFloat(p, "y")});
        }
        return o;
    }

    // Layers in order; groups add their children with the group's offset, opacity and
    // visibility folded in.
    bool ReadLayers(const Json& list, Vec2 offset, f32 opacity, bool visible)
    {
        if (!list.is_array())
            return Fail("\"layers\" is not an array");
        for (const Json& j : list) {
            MapLayer layer;
            layer.Name = GetString(j, "name");
            layer.Id = static_cast<u32>(GetInt(j, "id"));
            layer.Visible = visible && GetBool(j, "visible", true);
            layer.Opacity = opacity * GetFloat(j, "opacity", 1.0f);
            layer.Offset = offset + Vec2(GetFloat(j, "offsetx"), GetFloat(j, "offsety"));
            ReadProperties(j, layer.Props);
            const std::string type = GetString(j, "type");
            if (type == "group") {
                const auto children = j.find("layers");
                if (children != j.end() &&
                    !ReadLayers(*children, layer.Offset, layer.Opacity, layer.Visible))
                    return false;
                continue;
            }
            if (type == "objectgroup") {
                layer.Kind = LayerKind::Objects;
                if (const auto objects = j.find("objects");
                    objects != j.end() && objects->is_array())
                    for (const Json& o : *objects)
                        layer.Objects.push_back(ReadObject(o));
            } else if (type == "tilelayer") {
                if (!ReadCells(j, layer))
                    return false;
                layer.Collides = layer.Props.GetBool("collision", true);
            } else {
                EM_CORE_WARN("Tilemap: {}: layer '{}' ({}) is not supported; skipped",
                             File.string(), layer.Name, type);
                continue;
            }
            Map.m_Layers.push_back(std::move(layer));
        }
        return true;
    }

    bool ReadCells(const Json& j, MapLayer& layer)
    {
        if (j.contains("chunks"))
            return Fail("layer '{}' uses chunks (infinite map)", layer.Name);
        if (GetString(j, "encoding") == "base64")
            return Fail("layer '{}' is base64; in Tiled, set Map Properties > Tile Layer Format "
                        "to CSV",
                        layer.Name);
        if (GetInt(j, "width") != Map.m_Size.x || GetInt(j, "height") != Map.m_Size.y)
            return Fail("layer '{}' is not the size of the map", layer.Name);
        const auto data = j.find("data");
        const usize count = static_cast<usize>(Map.m_Size.x) * static_cast<usize>(Map.m_Size.y);
        if (data == j.end() || !data->is_array() || data->size() != count)
            return Fail("layer '{}' needs {} cells in \"data\"", layer.Name, count);
        layer.Cells.reserve(count);
        for (const Json& cell : *data) {
            if (!cell.is_number_unsigned() || cell.get<u64>() > 0xffffffffull)
                return Fail("layer '{}' has a cell that is not a tile id", layer.Name);
            layer.Cells.push_back(cell.get<u32>());
        }
        return true;
    }

    // GID -> tileset table and the combined collision grid.
    bool Finish()
    {
        u32 maxGid = 0;
        for (const Tileset& set : Map.m_Tilesets) {
            maxGid = Max(maxGid, set.FirstGid + set.TileCount - 1);
            Map.m_MaxTileSize = {Max(Map.m_MaxTileSize.x, set.TileSize.x),
                                 Max(Map.m_MaxTileSize.y, set.TileSize.y)};
        }
        if (maxGid > (1u << 24))
            return Fail("tile ids go up to {}; too many", maxGid);
        Map.m_GidToTileset.assign(static_cast<usize>(maxGid) + 1, 0);
        for (usize i = 0; i < Map.m_Tilesets.size(); ++i) {
            const Tileset& set = Map.m_Tilesets[i];
            std::fill_n(Map.m_GidToTileset.begin() + set.FirstGid, set.TileCount,
                        static_cast<u16>(i + 1));
        }

        Map.m_Collision.assign(static_cast<usize>(Map.m_Size.x * Map.m_Size.y),
                               TileCollision::None);
        for (const MapLayer& layer : Map.m_Layers) {
            if (layer.Kind != LayerKind::Tiles)
                continue;
            u32 unknown = 0;
            for (usize i = 0; i < layer.Cells.size(); ++i) {
                if (layer.Cells[i] == 0)
                    continue;
                const TileInfo* tile = Map.FindTile(layer.Cells[i]);
                if (!tile) {
                    ++unknown;
                    continue;
                }
                if (!layer.Collides || Priority(tile->Collision) <= Priority(Map.m_Collision[i]))
                    continue;
                Map.m_Collision[i] = tile->Collision;
                if (tile->Collision != TileCollision::Slope)
                    continue;
                // Slopes keep their heights per cell: a flipped cell mirrors them.
                if (Map.m_Slopes.empty())
                    Map.m_Slopes.assign(Map.m_Collision.size(), {});
                const bool flip = (layer.Cells[i] & kTileFlipX) != 0;
                Map.m_Slopes[i] =
                    flip ? TileSlope{.Left = tile->Slope.Right, .Right = tile->Slope.Left}
                         : tile->Slope;
            }
            if (unknown > 0)
                EM_CORE_WARN("Tilemap: {}: layer '{}' has {} cell(s) with unknown tile ids; they "
                             "are left empty",
                             File.string(), layer.Name, unknown);
        }
        return true;
    }
};

std::optional<Tilemap> Tilemap::Load(SDL_GPUDevice* device, const fs::path& file,
                                     const TextureOptions& options)
{
    const std::optional<Json> root = ReadJson(file);
    if (!root)
        return std::nullopt;
    Tilemap map;
    map.m_Files.push_back(file);
    TilemapLoader loader{.Map = map, .File = file, .Device = device, .Options = options};

    const std::string orientation = GetString(*root, "orientation");
    if (!orientation.empty() && orientation != "orthogonal") {
        loader.Fail("{} maps are not supported (only orthogonal)", orientation);
        return std::nullopt;
    }
    if (GetBool(*root, "infinite")) {
        loader.Fail("infinite maps are not supported; in Tiled, untick Map Properties > Infinite");
        return std::nullopt;
    }
    map.m_Size = {static_cast<i32>(GetInt(*root, "width")),
                  static_cast<i32>(GetInt(*root, "height"))};
    map.m_TileSize = {static_cast<i32>(GetInt(*root, "tilewidth")),
                      static_cast<i32>(GetInt(*root, "tileheight"))};
    if (map.m_Size.x <= 0 || map.m_Size.y <= 0 || map.m_TileSize.x <= 0 || map.m_TileSize.y <= 0 ||
        map.m_Size.x > 1 << 14 || map.m_Size.y > 1 << 14) {
        loader.Fail("needs a size (width, height) and tile size (tilewidth, tileheight); got "
                    "{}x{} tiles of {}x{}",
                    map.m_Size.x, map.m_Size.y, map.m_TileSize.x, map.m_TileSize.y);
        return std::nullopt;
    }
    loader.ReadProperties(*root, map.m_Props);
    const auto layers = root->find("layers");
    if (!loader.ReadTilesets(*root) ||
        (layers != root->end() && !loader.ReadLayers(*layers, {}, 1.0f, true)) || !loader.Finish())
        return std::nullopt;
    return map;
}

// --- Queries ----------------------------------------------------------------------------------

const MapLayer* Tilemap::FindLayer(std::string_view name) const
{
    for (const MapLayer& layer : m_Layers)
        if (layer.Name == name)
            return &layer;
    return nullptr;
}

const MapObject* Tilemap::FindObject(std::string_view name) const
{
    for (const MapLayer& layer : m_Layers)
        for (const MapObject& object : layer.Objects)
            if (object.Name == name)
                return &object;
    return nullptr;
}

u32 Tilemap::GetCell(usize layer, Vec2i tile) const
{
    if (layer >= m_Layers.size() || tile.x < 0 || tile.y < 0 || tile.x >= m_Size.x ||
        tile.y >= m_Size.y)
        return 0;
    const std::vector<u32>& cells = m_Layers[layer].Cells;
    const usize i = static_cast<usize>(tile.y * m_Size.x + tile.x);
    return i < cells.size() ? cells[i] : 0;
}

const Tileset* Tilemap::FindTileset(u32 cell) const
{
    const u32 gid = GetTileGid(cell);
    if (gid == 0 || gid >= m_GidToTileset.size() || m_GidToTileset[gid] == 0)
        return nullptr;
    return &m_Tilesets[m_GidToTileset[gid] - 1u];
}

const TileInfo* Tilemap::FindTile(u32 cell) const
{
    const Tileset* set = FindTileset(cell);
    return set ? &set->Tiles[GetTileGid(cell) - set->FirstGid] : nullptr;
}

TileCollision Tilemap::GetCollision(Vec2i tile) const
{
    if (tile.x < 0 || tile.y < 0 || tile.x >= m_Size.x || tile.y >= m_Size.y)
        return TileCollision::None;
    return m_Collision[static_cast<usize>(tile.y * m_Size.x + tile.x)];
}

Vec2i Tilemap::WorldToTile(Vec2 position) const
{
    return {static_cast<i32>(std::floor(position.x / static_cast<f32>(m_TileSize.x))),
            static_cast<i32>(std::floor(position.y / static_cast<f32>(m_TileSize.y)))};
}

Aabb Tilemap::GetTileBounds(Vec2i tile) const
{
    const Vec2 size(m_TileSize);
    const Vec2 min = Vec2(tile) * size;
    return {min, min + size};
}

Tilemap::TileRange Tilemap::GetTileRange(const Aabb& area) const
{
    if (m_TileSize.x <= 0 || m_TileSize.y <= 0)
        return {};
    // Clamped as floats first, so huge areas cannot overflow the int conversion.
    const auto index = [](f32 v, f32 tile, i32 count, bool up) {
        const f32 t = v / tile;
        return static_cast<i32>(
            Clamp(up ? std::ceil(t) : std::floor(t), 0.0f, static_cast<f32>(count)));
    };
    const f32 tw = static_cast<f32>(m_TileSize.x);
    const f32 th = static_cast<f32>(m_TileSize.y);
    return {.Min = {index(area.Min.x, tw, m_Size.x, false), index(area.Min.y, th, m_Size.y, false)},
            .Max = {index(area.Max.x, tw, m_Size.x, true), index(area.Max.y, th, m_Size.y, true)}};
}

bool Tilemap::OverlapsSolid(const Aabb& area) const
{
    bool hit = false;
    ForEachCollidingTile(area, [&](Vec2i, TileCollision c) { hit |= c == TileCollision::Solid; });
    return hit;
}

TileSlope Tilemap::GetSlope(Vec2i tile) const
{
    if (m_Slopes.empty() || GetCollision(tile) != TileCollision::Slope)
        return {};
    return m_Slopes[static_cast<usize>(tile.y * m_Size.x + tile.x)];
}

f32 Tilemap::GetSlopeFloorY(Vec2i tile, f32 x) const
{
    const Aabb t = GetTileBounds(tile);
    if (GetCollision(tile) != TileCollision::Slope)
        return t.Min.y;
    const TileSlope slope = GetSlope(tile);
    const f32 along = Clamp((x - t.Min.x) / (t.Max.x - t.Min.x), 0.0f, 1.0f);
    return t.Max.y - (slope.Left + (slope.Right - slope.Left) * along);
}

std::optional<f32> Tilemap::FindSlopeFloor(f32 x, f32 from, f32 to) const
{
    if (m_Slopes.empty())
        return std::nullopt;
    const TileRange rows = GetTileRange({{x, from}, {x, to}});
    const i32 column = WorldToTile({x, from}).x;
    for (i32 y = rows.Min.y; y < rows.Max.y; ++y) { // top to bottom: the first one is the highest
        if (GetCollision({column, y}) != TileCollision::Slope)
            continue;
        const f32 floor = GetSlopeFloorY({column, y}, x);
        if (floor >= from && floor <= to)
            return floor;
    }
    return std::nullopt;
}

TileMove Tilemap::MoveAndCollide(const Aabb& box, Vec2 delta, const TileMoveOptions& options) const
{
    // A hair of slack: a box resting exactly on a floor (after float rounding) is not stopped
    // by that floor when it walks sideways, and a box touching a wall counts as touching it.
    constexpr f32 kSkin = 0.01f;
    TileMove move;
    Aabb b = box;
    const auto centerX = [&b] { return (b.Min.x + b.Max.x) * 0.5f; };
    // Standing on a slope (bottom center on its surface) at the start of the move.
    const bool onSlope = FindSlopeFloor(centerX(), b.Max.y - 0.05f, b.Max.y + 0.05f).has_value();

    // Along x: every solid tile in the swept area that is ahead of the box limits the move.
    if (delta.x != 0.0f) {
        f32 dx = delta.x;
        // Standing on a slope, the box's lower corner may dip into the ground at the slope's top
        // (slopes are at most 45 degrees: up to half the box's width), so tiles that low are
        // not walls.
        const f32 lift = onSlope ? (b.Max.x - b.Min.x) * 0.5f + std::abs(dx) : kSkin;
        const Aabb sweep{{Min(b.Min.x, b.Min.x + dx), b.Min.y + kSkin},
                         {Max(b.Max.x, b.Max.x + dx), b.Max.y - lift}};
        ForEachCollidingTile(sweep, [&](Vec2i tile, TileCollision c) {
            if (c != TileCollision::Solid)
                return;
            const Aabb t = GetTileBounds(tile);
            if (delta.x > 0.0f && t.Min.x >= b.Max.x - kSkin && t.Min.x - b.Max.x <= dx) {
                dx = Max(t.Min.x - b.Max.x, 0.0f);
                move.HitRight = true;
            } else if (delta.x < 0.0f && t.Max.x <= b.Min.x + kSkin && t.Max.x - b.Min.x >= dx) {
                dx = Min(t.Max.x - b.Min.x, 0.0f);
                move.HitLeft = true;
            }
        });
        b.Min.x += dx;
        b.Max.x += dx;
        move.Delta.x = dx;
    }

    // Up: solid ceilings only (one-way tiles and slopes let the box through from below).
    if (delta.y < 0.0f) {
        f32 dy = delta.y;
        const Aabb sweep{{b.Min.x + kSkin, b.Min.y + dy}, {b.Max.x - kSkin, b.Max.y}};
        ForEachCollidingTile(sweep, [&](Vec2i tile, TileCollision c) {
            const Aabb t = GetTileBounds(tile);
            if (c == TileCollision::Solid && t.Max.y <= b.Min.y + kSkin &&
                t.Max.y - b.Min.y >= dy) {
                dy = Min(t.Max.y - b.Min.y, 0.0f);
                move.HitTop = true;
            }
        });
        // Jumping up along a slope, the floor can rise faster than the box: keep its bottom
        // center on the surface (so it lands on the slope, not inside it).
        const f32 bottom = b.Max.y + dy;
        const f32 climb = std::abs(move.Delta.x) + kSkin;
        if (const std::optional<f32> floor = FindSlopeFloor(centerX(), bottom - climb, bottom))
            dy += *floor - bottom;
        move.Delta.y = dy;
        return move;
    }
    if (delta.y == 0.0f && options.SnapDown <= 0.0f)
        return move;

    // Down (or snapping): floors up to `reach` below the box's bottom.
    const f32 bottom = b.Max.y;
    const f32 reach = delta.y + options.SnapDown;
    // 1. A slope under the bottom center is the floor, even a little above the bottom: walking
    //    up a 45 degree slope raises the floor by at most the distance walked.
    const f32 climb = std::abs(move.Delta.x) + kSkin;
    if (const std::optional<f32> floor =
            FindSlopeFloor(centerX(), bottom - climb, bottom + reach)) {
        move.Delta.y = *floor - bottom;
        move.HitBottom = move.OnSlope = true;
        return move;
    }
    // 2. The nearest tile top below the box: one-way tiles the box starts above, and solid
    //    tiles; coming off a slope, those may also be up to `climb` above the bottom (stepping
    //    off the top of the slope onto the flat ground next to it).
    f32 best = reach;
    bool found = false;
    bool solid = false;
    const Aabb sweep{{b.Min.x + kSkin, b.Min.y}, {b.Max.x - kSkin, bottom + reach}};
    ForEachCollidingTile(sweep, [&](Vec2i tile, TileCollision c) {
        if (c == TileCollision::Slope || (c == TileCollision::OneWay && options.IgnoreOneWay))
            return;
        const f32 top = GetTileBounds(tile).Min.y;
        const f32 above = c == TileCollision::Solid && onSlope ? climb : kSkin;
        if (top < bottom - above || top - bottom > reach) // overlapping it already, or too far
            return;
        const f32 distance = c == TileCollision::Solid ? top - bottom : Max(top - bottom, 0.0f);
        if (!found || distance < best) {
            best = distance;
            solid = c == TileCollision::Solid;
        } else if (distance == best) {
            solid |= c == TileCollision::Solid; // one-way and solid side by side: not droppable
        }
        found = true;
    });
    if (found) {
        move.Delta.y = best;
        move.HitBottom = true;
        move.OnOneWay = !solid;
    } else {
        move.Delta.y = delta.y; // nothing within the snap distance: fall freely
    }
    return move;
}

// --- Drawing ----------------------------------------------------------------------------------

u32 Tilemap::DrawLayer(Renderer2D& r, usize index, const Rect2D& view, const Vec4& tint) const
{
    if (index >= m_Layers.size() || m_Layers[index].Kind != LayerKind::Tiles)
        return 0;
    const MapLayer& layer = m_Layers[index];
    // Tiles larger than the grid stick out up and to the right of their cell (Tiled anchors
    // them at the bottom-left), so look a little further left and down.
    const Vec2 overhang(static_cast<f32>(Max(m_MaxTileSize.x - m_TileSize.x, 0)),
                        static_cast<f32>(Max(m_MaxTileSize.y - m_TileSize.y, 0)));
    const TileRange range = GetTileRange({view.Min - layer.Offset - Vec2(overhang.x, 0.0f),
                                          view.Max - layer.Offset + Vec2(0.0f, overhang.y)});
    const Vec4 color{tint.x, tint.y, tint.z, tint.w * layer.Opacity};
    u32 drawn = 0;
    for (i32 y = range.Min.y; y < range.Max.y; ++y) {
        for (i32 x = range.Min.x; x < range.Max.x; ++x) {
            const u32 cell = layer.Cells[static_cast<usize>(y * m_Size.x + x)];
            const Tileset* set = FindTileset(cell);
            if (!set || !set->Sheet)
                continue;
            const Sprite sprite = set->GetSprite(GetTileGid(cell) - set->FirstGid);
            const Vec2 size(set->TileSize);
            const Vec2 topLeft = Vec2(static_cast<f32>(x * m_TileSize.x),
                                      static_cast<f32>((y + 1) * m_TileSize.y) - size.y) +
                                 layer.Offset + set->DrawOffset;
            const TileTransform t = GetTileTransform(cell);
            if (t.Rotation == 0.0f)
                r.DrawSprite(sprite, topLeft,
                             {.Origin = {0.0f, 0.0f},
                              .Tint = color,
                              .FlipX = t.FlipX,
                              .FlipY = t.FlipY,
                              .PixelSnap = true});
            else
                r.DrawSprite(sprite, topLeft + size * 0.5f,
                             {.Rotation = t.Rotation,
                              .Tint = color,
                              .FlipX = t.FlipX,
                              .FlipY = t.FlipY,
                              .PixelSnap = true});
            ++drawn;
        }
    }
    return drawn;
}

u32 Tilemap::Draw(Renderer2D& r, const Rect2D& view) const
{
    u32 drawn = 0;
    for (usize i = 0; i < m_Layers.size(); ++i)
        if (m_Layers[i].Visible)
            drawn += DrawLayer(r, i, view);
    return drawn;
}

u32 Tilemap::DrawCollision(Renderer2D& r, const Rect2D& view) const
{
    u32 drawn = 0;
    const Vec2 size(m_TileSize);
    ForEachCollidingTile({view.Min, view.Max}, [&](Vec2i tile, TileCollision c) {
        const Vec2 at = Vec2(tile) * size;
        if (c == TileCollision::Solid)
            r.FillRect(at, size, {1.0f, 0.15f, 0.15f, 0.35f});
        else if (c == TileCollision::OneWay)
            r.FillRect(at, {size.x, 3.0f}, {1.0f, 0.85f, 0.1f, 0.8f});
        else
            r.DrawLine({at.x, GetSlopeFloorY(tile, at.x)},
                       {at.x + size.x, GetSlopeFloorY(tile, at.x + size.x)},
                       {0.2f, 0.9f, 1.0f, 1.0f});
        ++drawn;
    });
    return drawn;
}

} // namespace Emerald

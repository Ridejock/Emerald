#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Physics/Collision.h"
#include "Emerald/Renderer/Camera2D.h"
#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/Texture.h"

namespace Emerald {

class Renderer2D;

// Custom properties as Tiled stores them on maps, layers, tiles and objects. Tiled's types map
// to: bool -> bool, int and object -> i64, float -> f64, string, color ("#aarrggbb") and file ->
// std::string. The getters convert between int and float and return `fallback` for a missing
// name or another type.
class Properties {
public:
    using Value = std::variant<bool, i64, f64, std::string>;

    void Set(std::string name, Value value);
    [[nodiscard]] const Value* Find(std::string_view name) const;
    [[nodiscard]] bool Has(std::string_view name) const { return Find(name) != nullptr; }
    [[nodiscard]] bool GetBool(std::string_view name, bool fallback = false) const;
    [[nodiscard]] i64 GetInt(std::string_view name, i64 fallback = 0) const;
    [[nodiscard]] f64 GetFloat(std::string_view name, f64 fallback = 0.0) const;
    [[nodiscard]] std::string_view GetString(std::string_view name,
                                             std::string_view fallback = {}) const;
    [[nodiscard]] const std::vector<std::pair<std::string, Value>>& GetAll() const
    {
        return m_Items;
    }

private:
    std::vector<std::pair<std::string, Value>> m_Items; // a handful per thing: a plain list
};

// --- Tiles ------------------------------------------------------------------------------------

// A cell of a tile layer holds a GID: 0 = empty, otherwise a tile of one of the map's tilesets
// (tileset.FirstGid + the tile's index). Tiled keeps flips in the top bits.
inline constexpr u32 kTileFlipX = 0x80000000u;    // mirrored left <-> right
inline constexpr u32 kTileFlipY = 0x40000000u;    // mirrored top <-> bottom
inline constexpr u32 kTileFlipDiag = 0x20000000u; // mirrored along the top-left diagonal
inline constexpr u32 kTileGidMask = 0x0fffffffu;  // the GID (hex maps' 0x10000000 is dropped too)
[[nodiscard]] constexpr u32 GetTileGid(u32 cell)
{
    return cell & kTileGidMask;
}

// A cell's flip bits as SpriteOptions: Tiled applies the diagonal flip first, then X, then Y;
// a diagonal flip is the same as a vertical flip followed by a quarter turn clockwise.
// (Rotation assumes square tiles, like Tiled's rotate buttons.)
struct TileTransform {
    bool FlipX = false;
    bool FlipY = false;
    f32 Rotation = 0.0f; // radians, clockwise
};
[[nodiscard]] TileTransform GetTileTransform(u32 cell);

// How a tile collides, from its tileset's custom properties (see README "Tilemaps"): a bool
// property "solid" or "oneway", a tile class (type) of "solid", "oneway" or "slope", or the
// slope heights below. Solid wins over slope, slope over one-way.
enum class TileCollision : u8 {
    None,
    Solid,  // blocks from every side
    OneWay, // a platform top: only blocks things moving down onto it from above
    Slope,  // a sloped floor (TileSlope): only the bottom center of a box stands on it
};

// A slope's floor height at the tile's left and right edge, in pixels up from the tile's bottom
// (Tiled float or int properties "slopeLeft" and "slopeRight"). 0 -> 16 on a 16 px tile is a 45
// degree slope rising to the right; 0 -> 8 and 8 -> 16 on two tiles make a 22.5 degree one. A
// horizontally flipped cell swaps the two (vertical and diagonal flips are ignored).
struct TileSlope {
    f32 Left = 0.0f;
    f32 Right = 0.0f;
};

// What a tileset knows about one of its tiles (Tiled lists only tiles with data; the rest are
// default: no class, no properties, no collision).
struct TileInfo {
    std::string Type; // Tiled's "Class" field
    Properties Props;
    TileCollision Collision = TileCollision::None;
    TileSlope Slope{}; // Collision == Slope
};

// A grid of same-size tiles in one image, embedded in the map or from an external .tsj file.
struct Tileset {
    std::string Name;
    u32 FirstGid = 1;
    u32 TileCount = 0;
    u32 Columns = 0;
    Vec2i TileSize{};
    i32 Margin = 0;                 // pixels around the grid
    i32 Spacing = 0;                // pixels between tiles
    Vec2 DrawOffset{};              // Tiled's "Drawing Offset"
    std::filesystem::path Source;   // the .tsj (empty if embedded)
    std::filesystem::path Image;    // the image, resolved to a full path
    std::unique_ptr<Texture> Sheet; // on the heap so its address survives moves
    std::vector<TileInfo> Tiles;    // TileCount entries, by tile index
    Properties Props;

    // The tile's region of the image; `index` is the GID minus FirstGid.
    [[nodiscard]] Sprite GetSprite(u32 index) const;
};

// --- Layers and objects -----------------------------------------------------------------------

enum class ObjectShape : u8 { Rectangle, Point, Ellipse, Polygon, Polyline, Tile, Text };

// An object from an object layer: spawn points, triggers, items... Position and Size are in
// map pixels with the position at the top-left corner (Tiled puts tile objects' position at
// the bottom-left; the loader moves it to the top-left like every other shape).
struct MapObject {
    u32 Id = 0;
    std::string Name;
    std::string Type; // Tiled's "Class" field
    ObjectShape Shape = ObjectShape::Rectangle;
    Vec2 Position{};
    Vec2 Size{};
    f32 Rotation = 0.0f;      // radians, clockwise around Position (as in Tiled)
    u32 Gid = 0;              // tile objects: the tile, with flip bits
    std::vector<Vec2> Points; // polygons and polylines, relative to Position
    bool Visible = true;
    Properties Props;

    [[nodiscard]] Aabb GetBounds() const { return {Position, Position + Size}; }
};

enum class LayerKind : u8 { Tiles, Objects };

// One layer, in Tiled's order (the first is drawn first, at the bottom). Group layers are
// flattened into their children (offsets add up, opacity multiplies, hidden hides).
struct MapLayer {
    LayerKind Kind = LayerKind::Tiles;
    std::string Name;
    u32 Id = 0;
    bool Visible = true;
    f32 Opacity = 1.0f;
    Vec2 Offset{}; // pixels
    Properties Props;
    bool Collides = true;           // tile layers: false if the layer property "collision" is false
    std::vector<u32> Cells;         // tile layers: Width * Height cells, row by row
    std::vector<MapObject> Objects; // object layers
};

// Where MoveAndCollide stopped: the delta actually moved and which sides hit a tile.
struct TileMove {
    Vec2 Delta{};
    bool HitLeft = false;
    bool HitRight = false;
    bool HitTop = false;    // moving up into a ceiling
    bool HitBottom = false; // moving down onto a floor or one-way platform: "on the ground"
    bool OnSlope = false;   // HitBottom, and the floor is a slope under the box's bottom center
    bool OnOneWay = false;  // HitBottom, and the floor is only one-way tiles (can drop through)
};

// Extras for MoveAndCollide, for platformer characters (Physics/Platformer.h).
struct TileMoveOptions {
    bool IgnoreOneWay = false; // fall through one-way platforms (dropping down)
    // A box not moving up keeps to a floor up to this far below the end of its move (walking
    // down a slope stays on it instead of falling down in small steps). 0: off.
    f32 SnapDown = 0.0f;
};

// --- The map ----------------------------------------------------------------------------------

// A Tiled map (.tmj, Tiled's JSON format; orthogonal, fixed size, CSV layer data), its tilesets
// and their images, with tile collision and culled drawing.
//
//   AssetHandle<Tilemap> map = assets.Load<Tilemap>("maps/room.tmj"); // hot reloads in debug
//   r.Begin(camera);
//   map->Draw(r, camera.GetVisibleBounds()); // only the tiles on screen
//   const TileMove move = map->MoveAndCollide(heroBox, velocity * dt);
//   heroBox.Min += move.Delta; heroBox.Max += move.Delta;
//
// World units are map pixels, with (0, 0) at the map's top-left. Outside the map is empty.
class Tilemap {
public:
    // Loads the map, its external tilesets and their images (textures on `device`; with no
    // device, textures without GPU resources, for tests and tools). Returns nullopt and logs why
    // if a file is missing or the data is unusable.
    [[nodiscard]] static std::optional<Tilemap> Load(SDL_GPUDevice* device,
                                                     const std::filesystem::path& file,
                                                     const TextureOptions& options = {});

    [[nodiscard]] Vec2i GetSize() const { return m_Size; } // in tiles
    [[nodiscard]] Vec2i GetTileSize() const { return m_TileSize; }
    [[nodiscard]] Rect2D GetBounds() const // in pixels: e.g. Camera2D::SetBounds
    {
        return {.Min = {},
                .Max = Vec2(static_cast<f32>(m_Size.x * m_TileSize.x),
                            static_cast<f32>(m_Size.y * m_TileSize.y))};
    }
    [[nodiscard]] const Properties& GetProperties() const { return m_Props; }
    [[nodiscard]] const std::vector<MapLayer>& GetLayers() const { return m_Layers; }
    [[nodiscard]] const std::vector<Tileset>& GetTilesets() const { return m_Tilesets; }
    [[nodiscard]] const MapLayer* FindLayer(std::string_view name) const;
    // The first object with this name in any object layer.
    [[nodiscard]] const MapObject* FindObject(std::string_view name) const;
    // Every file the map was loaded from (the .tmj first): what hot reload watches.
    [[nodiscard]] const std::vector<std::filesystem::path>& GetFiles() const { return m_Files; }

    // The cell (GID + flip bits) of a tile layer at a tile; 0 outside the map.
    [[nodiscard]] u32 GetCell(usize layer, Vec2i tile) const;
    // The tileset and tile data for a cell; null for empty cells and unknown GIDs.
    [[nodiscard]] const Tileset* FindTileset(u32 cell) const;
    [[nodiscard]] const TileInfo* FindTile(u32 cell) const;

    // --- Collision: every tile layer with Collides, combined into one grid at load ---
    [[nodiscard]] TileCollision GetCollision(Vec2i tile) const; // None outside the map
    [[nodiscard]] Vec2i WorldToTile(Vec2 position) const;
    [[nodiscard]] Aabb GetTileBounds(Vec2i tile) const;
    // The range of tiles an area overlaps (touching an edge is not overlapping), clamped to the
    // map: tiles Min.x <= x < Max.x, Min.y <= y < Max.y. Empty if the area is outside.
    struct TileRange {
        Vec2i Min{};
        Vec2i Max{};
    };
    [[nodiscard]] TileRange GetTileRange(const Aabb& area) const;
    // Calls fn(Vec2i tile, TileCollision) for every colliding tile the area overlaps.
    template <typename Fn> void ForEachCollidingTile(const Aabb& area, Fn&& fn) const;
    // Whether the area overlaps a solid tile (one-way tiles never count as overlapping).
    [[nodiscard]] bool OverlapsSolid(const Aabb& area) const;
    // A slope cell's heights (flip applied); zeros for other cells.
    [[nodiscard]] TileSlope GetSlope(Vec2i tile) const;
    // The y of a slope cell's floor at world x (clamped to the tile); for other cells, its top.
    [[nodiscard]] f32 GetSlopeFloorY(Vec2i tile, f32 x) const;
    // Moves `box` by `delta`, stopping at tiles: first along x, then along y, each as a sweep,
    // so fast moves do not tunnel. Solid tiles block every side; one-way tiles only block a
    // downward move whose bottom starts at or above the tile's top. A box that already overlaps
    // a tile is not pushed out, only kept from going further in.
    // Slopes (see README "Slopes"): a box moving down (or snapping) whose bottom center is over
    // a slope stands on the slope's surface, also when that means moving up a little (walking
    // up it); slopes never block sideways or from below. A box standing on a slope may overlap
    // the solid tile at the slope's top a little, so it can walk up onto it.
    [[nodiscard]] TileMove MoveAndCollide(const Aabb& box, Vec2 delta,
                                          const TileMoveOptions& options = {}) const;

    // --- Drawing (between Renderer2D::Begin and End) ---
    // Draws one tile layer's tiles that overlap `view` (e.g. Camera2D::GetVisibleBounds()),
    // tinted with the layer's opacity times `tint`. Returns how many tiles it drew. Hidden layers
    // are drawn too: the caller decides (Draw below skips them).
    u32 DrawLayer(Renderer2D& r, usize layer, const Rect2D& view,
                  const Vec4& tint = {1.0f, 1.0f, 1.0f, 1.0f}) const;
    // Every visible tile layer in order; returns the tile count.
    u32 Draw(Renderer2D& r, const Rect2D& view) const;
    // Debug view: solid tiles filled red, one-way tiles as a yellow bar along their top, slopes
    // as a cyan line along their surface.
    u32 DrawCollision(Renderer2D& r, const Rect2D& view) const;

private:
    friend struct TilemapLoader; // fills in the members (Tilemap.cpp)

    Vec2i m_Size{};
    Vec2i m_TileSize{};
    Properties m_Props;
    std::vector<Tileset> m_Tilesets; // by FirstGid
    std::vector<MapLayer> m_Layers;
    std::vector<TileCollision> m_Collision; // m_Size.x * m_Size.y
    std::vector<TileSlope> m_Slopes;        // like m_Collision; empty if the map has no slopes
    std::vector<u16> m_GidToTileset;        // GID -> index into m_Tilesets + 1 (0: none)
    std::vector<std::filesystem::path> m_Files;
    Vec2i m_MaxTileSize{}; // the largest tileset tile (for culling)

    // The y of the highest slope surface under world x between y `from` and `to` (from < to).
    [[nodiscard]] std::optional<f32> FindSlopeFloor(f32 x, f32 from, f32 to) const;
};

template <typename Fn> void Tilemap::ForEachCollidingTile(const Aabb& area, Fn&& fn) const
{
    const TileRange range = GetTileRange(area);
    for (i32 y = range.Min.y; y < range.Max.y; ++y)
        for (i32 x = range.Min.x; x < range.Max.x; ++x)
            if (const TileCollision c = m_Collision[static_cast<usize>(y * m_Size.x + x)];
                c != TileCollision::None)
                fn(Vec2i(x, y), c);
}

} // namespace Emerald

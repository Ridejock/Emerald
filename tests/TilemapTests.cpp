// Tilemaps (Tiled JSON): loading the sandbox's sample room (layers, both kinds of tileset,
// objects and properties), flip bits, collision queries and MoveAndCollide, errors for missing
// files and bad data, and hot reload through the asset manager. No GPU needed.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <Emerald/Assets/Assets.h>
#include <Emerald/Core/Log.h>
#include <Emerald/Tilemap/Tilemap.h>

#include "Test.h"

using namespace Emerald;
namespace fs = std::filesystem;

namespace {

const fs::path kSample = EMERALD_TEST_TILEMAPS; // sandbox/assets/tilemaps

std::optional<Tilemap> LoadRoom()
{
    Log::Init({});
    return Tilemap::Load(nullptr, kSample / "room.tmj");
}

// Reading or writing a test file failing is reported (not just an empty string or a lost edit).
std::string ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        ::Test::Fail(__FILE__, __LINE__, "cannot read " + path.string());
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes a file and moves its modification time forward (coarse timestamps: see AssetTests).
void WriteText(const fs::path& path, const std::string& text)
{
    const bool existed = fs::exists(path);
    const fs::file_time_type before = existed ? fs::last_write_time(path) : fs::file_time_type{};
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    if (!out)
        ::Test::Fail(__FILE__, __LINE__, "cannot write " + path.string());
    if (existed)
        fs::last_write_time(path, before + std::chrono::seconds(2));
}

// Finds `pattern` in `text`, where whitespace in the pattern matches any run of whitespace (or
// none) in the text, so the edits below work whatever the file's formatting: CRLF line ends
// (Git's core.autocrlf on Windows) or a map saved again by Tiled ("height":30). Returns the
// position and the length matched.
std::optional<std::pair<usize, usize>> FindLoose(const std::string& text,
                                                 const std::string& pattern)
{
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (usize start = 0; start < text.size(); ++start) {
        usize t = start;
        usize p = 0;
        while (p < pattern.size()) {
            if (space(pattern[p])) {
                while (p < pattern.size() && space(pattern[p]))
                    ++p;
                while (t < text.size() && space(text[t]))
                    ++t;
            } else if (t < text.size() && text[t] == pattern[p]) {
                ++t;
                ++p;
            } else {
                break;
            }
        }
        if (p == pattern.size())
            return std::pair(start, t - start);
    }
    return std::nullopt;
}

// A copy of the sample folder to break things in.
struct TempMaps {
    fs::path Dir = fs::temp_directory_path() / "emerald_tilemap_tests";
    TempMaps()
    {
        Log::Init({});
        fs::remove_all(Dir);
        fs::create_directories(Dir);
        for (const char* name : {"room.tmj", "dungeon.tsj", "dungeon.png", "props.png"})
            fs::copy_file(kSample / name, Dir / name);
    }
    ~TempMaps() { fs::remove_all(Dir); }
    TempMaps(const TempMaps&) = delete;
    TempMaps& operator=(const TempMaps&) = delete;

    // room.tmj with the first `from` (see FindLoose) replaced by `to`.
    void EditRoom(const std::string& from, const std::string& to) const
    {
        std::string text = ReadText(Dir / "room.tmj");
        const std::optional<std::pair<usize, usize>> at = FindLoose(text, from);
        if (!at) {
            // Say what the file looked like, so a failure elsewhere can be told apart.
            ::Test::Fail(__FILE__, __LINE__,
                         "room.tmj (" + std::to_string(text.size()) + " bytes, starting '" +
                             text.substr(0, 24) + "') has no '" + from + "'");
            return;
        }
        text.replace(at->first, at->second, to);
        WriteText(Dir / "room.tmj", text);
    }
};

Aabb Box(f32 x, f32 y, f32 w, f32 h)
{
    return {{x, y}, {x + w, y + h}};
}

} // namespace

TEST(TilemapLoadsLayersInOrder)
{
    const std::optional<Tilemap> map = LoadRoom();
    CHECK(map.has_value());
    if (!map)
        return;
    CHECK(map->GetSize() == Vec2i(48, 30) && map->GetTileSize() == Vec2i(16, 16));
    CHECK(map->GetBounds().Max == Vec2(768.0f, 480.0f));
    CHECK(map->GetProperties().GetString("title") == "Sample room");

    const auto& layers = map->GetLayers();
    CHECK(layers.size() == 5);
    const char* names[] = {"Ground", "Walls", "Decor", "Objects", "Overhead"};
    for (usize i = 0; i < layers.size() && i < 5; ++i)
        CHECK(layers[i].Name == names[i]);
    CHECK(layers[0].Kind == LayerKind::Tiles && layers[0].Cells.size() == 48 * 30);
    CHECK(layers[3].Kind == LayerKind::Objects && layers[3].Cells.empty());
    CHECK(layers[0].Collides && !layers[4].Collides); // Overhead: property collision = false
    CHECK(map->FindLayer("Decor") == &layers[2] && map->FindLayer("Nope") == nullptr);
}

TEST(TilemapTilesetsExternalAndEmbedded)
{
    const std::optional<Tilemap> map = LoadRoom();
    if (!map) {
        CHECK(false);
        return;
    }
    const auto& sets = map->GetTilesets();
    CHECK(sets.size() == 2);
    if (sets.size() != 2)
        return;
    // dungeon.tsj (external) and props (embedded in the map).
    CHECK(sets[0].Name == "dungeon" && sets[0].FirstGid == 1 && sets[0].TileCount == 16);
    CHECK(sets[0].Source.filename() == "dungeon.tsj" && sets[0].Columns == 8);
    CHECK(sets[1].Name == "props" && sets[1].FirstGid == 17 && sets[1].Source.empty());
    CHECK(sets[0].Sheet && sets[0].Sheet->GetWidth() == 128 && sets[0].Sheet->GetHeight() == 32);

    // Tile 9 (wall face) is the second tile of the second row.
    const Sprite face = sets[0].GetSprite(9);
    CHECK(face.Source == sets[0].Sheet.get());
    CHECK(face.Region.Position == Vec2(16.0f, 16.0f) && face.Region.Size == Vec2(16.0f, 16.0f));

    // Lookup by GID, flip bits or not; unknown GIDs find nothing.
    CHECK(map->FindTileset(17) == &sets[1] && map->FindTileset(16 | kTileFlipX) == &sets[0]);
    CHECK(map->FindTileset(0) == nullptr && map->FindTileset(999) == nullptr);
    CHECK(map->FindTile(1 + 10)->Collision == TileCollision::Solid);  // water: "solid"
    CHECK(map->FindTile(1 + 11)->Collision == TileCollision::OneWay); // railing: "oneway"
    CHECK(map->FindTile(17 + 1)->Type == "solid" &&
          map->FindTile(17 + 1)->Collision == TileCollision::Solid); // barrel: by class
    CHECK(map->FindTile(1)->Collision == TileCollision::None);

    // Hot reload watches every file.
    const auto& files = map->GetFiles();
    CHECK(files.size() == 4 && files[0].filename() == "room.tmj");
}

TEST(TilemapObjects)
{
    const std::optional<Tilemap> map = LoadRoom();
    if (!map) {
        CHECK(false);
        return;
    }
    const MapObject* spawn = map->FindObject("spawn");
    CHECK(spawn && spawn->Type == "spawn" && spawn->Shape == ObjectShape::Point);
    CHECK(spawn && spawn->Position == Vec2(22.0f * 16.0f + 8.0f, 14.0f * 16.0f + 8.0f));

    const MapObject* chest = map->FindObject("chest");
    CHECK(chest && chest->Type == "chest" && chest->Shape == ObjectShape::Rectangle);
    CHECK(chest && chest->Size == Vec2(48.0f, 48.0f));
    CHECK(chest && chest->Props.GetInt("gold") == 25 &&
          chest->Props.GetString("contents") == "a rusty key");
    CHECK(chest && chest->Props.GetFloat("gold") == 25.0 && !chest->Props.Has("weight"));
    CHECK(chest && chest->Props.GetInt("weight", 7) == 7); // missing: the fallback

    const MapObject* pond = map->FindObject("pond");
    CHECK(pond && pond->Shape == ObjectShape::Ellipse && pond->Props.GetFloat("depth") == 1.5);
    CHECK(pond && pond->GetBounds().Max == Vec2(27.0f * 16.0f, 12.0f * 16.0f));
    CHECK(map->FindObject("nobody") == nullptr);
}

TEST(TilemapCollisionGrid)
{
    const std::optional<Tilemap> map = LoadRoom();
    if (!map) {
        CHECK(false);
        return;
    }
    CHECK(map->GetCollision({0, 0}) == TileCollision::Solid);   // wall
    CHECK(map->GetCollision({1, 1}) == TileCollision::None);    // floor
    CHECK(map->GetCollision({30, 5}) == TileCollision::OneWay); // railing
    CHECK(map->GetCollision({4, 4}) == TileCollision::Solid);   // barrel (Decor layer)
    CHECK(map->GetCollision({15, 0}) == TileCollision::Solid);  // pillar base, not its top
    CHECK(map->GetCollision({-1, 3}) == TileCollision::None &&  // outside the map
          map->GetCollision({48, 3}) == TileCollision::None);

    CHECK(map->WorldToTile({-0.5f, 17.0f}) == Vec2i(-1, 1));
    CHECK(map->GetTileBounds({2, 3}).Min == Vec2(32.0f, 48.0f));
    // Touching a tile's edge is not overlapping it.
    const Tilemap::TileRange range = map->GetTileRange(Box(16.0f, 16.0f, 16.0f, 16.0f));
    CHECK(range.Min == Vec2i(1, 1) && range.Max == Vec2i(2, 2));
    const Tilemap::TileRange clamped = map->GetTileRange(Box(-1e9f, -5.0f, 2e9f, 1e9f));
    CHECK(clamped.Min == Vec2i(0, 0) && clamped.Max == Vec2i(48, 30));

    CHECK(map->OverlapsSolid(Box(10.0f, 10.0f, 10.0f, 10.0f)));  // into the corner walls
    CHECK(!map->OverlapsSolid(Box(16.0f, 16.0f, 16.0f, 16.0f))); // touching only
    CHECK(!map->OverlapsSolid(Box(30.0f * 16.0f, 5.0f * 16.0f + 2.0f, 8.0f, 8.0f))); // one-way
    u32 count = 0;
    map->ForEachCollidingTile(Box(0.0f, 0.0f, 32.0f, 32.0f),
                              [&](Vec2i, TileCollision) { ++count; });
    CHECK(count == 3); // (0, 0), (1, 0), (0, 1): (1, 1) is floor
}

TEST(TilemapMoveAndCollide)
{
    const std::optional<Tilemap> map = LoadRoom();
    if (!map) {
        CHECK(false);
        return;
    }
    // A 10 x 12 box on the floor at x = 20 ... 30, y = 20 ... 32 (walls at x < 16, y < 16).
    const Aabb box = Box(20.0f, 20.0f, 10.0f, 12.0f);

    // Into the left wall: stops flush at x = 16, even from far away (no tunneling).
    TileMove m = map->MoveAndCollide(box, {-500.0f, 0.0f});
    CHECK(m.HitLeft && !m.HitRight && NearlyEqual(m.Delta.x, -4.0f));
    m = map->MoveAndCollide(box, {0.0f, -3.0f}); // up, not far enough to hit
    CHECK(!m.HitTop && NearlyEqual(m.Delta.y, -3.0f));
    m = map->MoveAndCollide(box, {-2.0f, -10.0f}); // diagonal into the corner
    CHECK(m.HitTop && NearlyEqual(m.Delta.x, -2.0f) && NearlyEqual(m.Delta.y, -4.0f));

    // Sliding along a wall the box touches: not stopped by it.
    const Aabb onWall = Box(16.0f, 20.0f, 10.0f, 12.0f);
    m = map->MoveAndCollide(onWall, {0.0f, 6.0f});
    CHECK(!m.HitBottom && NearlyEqual(m.Delta.y, 6.0f));

    // The railing row at y = 80 ... 96, x = 480 ... 672. From above it is a floor...
    const Aabb above = Box(500.0f, 60.0f, 10.0f, 12.0f); // bottom at 72
    m = map->MoveAndCollide(above, {0.0f, 50.0f});
    CHECK(m.HitBottom && NearlyEqual(m.Delta.y, 8.0f));
    // ... standing on it, walking sideways is free ...
    const Aabb standing = Box(500.0f, 68.0f, 10.0f, 12.0f); // bottom exactly at 80
    m = map->MoveAndCollide(standing, {5.0f, 0.0f});
    CHECK(!m.HitRight && NearlyEqual(m.Delta.x, 5.0f));
    // ... from below it lets the box through going up ...
    const Aabb below = Box(500.0f, 100.0f, 10.0f, 12.0f);
    m = map->MoveAndCollide(below, {0.0f, -30.0f});
    CHECK(!m.HitTop && NearlyEqual(m.Delta.y, -30.0f));
    // ... and a box already inside it (bottom below the top) falls on through.
    const Aabb inside = Box(500.0f, 75.0f, 10.0f, 12.0f);
    m = map->MoveAndCollide(inside, {0.0f, 4.0f});
    CHECK(!m.HitBottom && NearlyEqual(m.Delta.y, 4.0f));
}

// Tiled: diagonal flip (swap x and y), then X, then Y. The sprite: flip X / Y, then turn
// clockwise. Both must send every point of the tile to the same place.
TEST(TilemapFlipBits)
{
    CHECK(GetTileGid(5 | kTileFlipX | kTileFlipY | kTileFlipDiag) == 5);
    CHECK(GetTileGid(0x10000000u | 7) == 7); // the hex rotation bit is dropped too
    for (u32 bits = 0; bits < 8; ++bits) {
        const u32 cell = 1 | (bits & 1 ? kTileFlipX : 0) | (bits & 2 ? kTileFlipY : 0) |
                         (bits & 4 ? kTileFlipDiag : 0);
        const Vec2 p(1.0f, 2.0f); // a point relative to the tile's center
        Vec2 tiled = p;
        if (bits & 4)
            tiled = {tiled.y, tiled.x};
        if (bits & 1)
            tiled.x = -tiled.x;
        if (bits & 2)
            tiled.y = -tiled.y;

        const TileTransform t = GetTileTransform(cell);
        Vec2 sprite(t.FlipX ? -p.x : p.x, t.FlipY ? -p.y : p.y);
        const f32 c = std::cos(t.Rotation);
        const f32 s = std::sin(t.Rotation);
        sprite = {c * sprite.x - s * sprite.y, s * sprite.x + c * sprite.y}; // clockwise, y-down
        CHECK(NearlyEqual(sprite, tiled, 1e-5f));
    }

    // The sample room's arrow row carries all eight combinations.
    const std::optional<Tilemap> map = LoadRoom();
    if (!map) {
        CHECK(false);
        return;
    }
    for (u32 i = 0; i < 8; ++i) {
        const u32 cell = map->GetCell(2, {3 + static_cast<i32>(i), 14});
        CHECK(GetTileGid(cell) == 15);
        CHECK(((cell & kTileFlipX) != 0) == ((i & 1) != 0));
        CHECK(((cell & kTileFlipDiag) != 0) == ((i & 4) != 0));
    }
}

TEST(TilemapErrorsAreLoggedNotFatal)
{
    TempMaps t;
    CHECK(Tilemap::Load(nullptr, t.Dir / "room.tmj").has_value()); // the copy works
    CHECK(!Tilemap::Load(nullptr, t.Dir / "missing.tmj"));         // no such file

    // Each edit starts from the sample room (kept in memory, so one failed write cannot spoil
    // the edits after it).
    const std::string room = ReadText(kSample / "room.tmj");
    const auto broken = [&](const std::string& from, const std::string& to) {
        t.EditRoom(from, to);
        const bool failed = !Tilemap::Load(nullptr, t.Dir / "room.tmj").has_value();
        WriteText(t.Dir / "room.tmj", room);
        return failed;
    };
    CHECK(broken("{", "["));                                    // not JSON
    CHECK(broken("\"height\": 30,", "\"height\": 31,"));        // layers do not fit
    CHECK(broken("\"infinite\": false", "\"infinite\": true")); // unsupported
    CHECK(broken("\"orientation\": \"orthogonal\"", "\"orientation\": \"isometric\""));
    CHECK(broken("\"source\": \"dungeon.tsj\"", "\"source\": \"gone.tsj\""));    // missing tileset
    CHECK(broken("\"source\": \"dungeon.tsj\"", "\"source\": \"dungeon.tsx\"")); // XML tileset
    CHECK(broken("\"image\": \"props.png\"", "\"image\": \"gone.png\""));        // missing image
    CHECK(broken("\"data\": [", "\"encoding\": \"base64\", \"data\": ["));       // not CSV
    CHECK(broken("\"data\": [\n    ", "\"data\": [\n    -"));                    // bad cell

    // Unknown tile ids load (with a warning) and stay empty.
    t.EditRoom("\"data\": [\n    ", "\"data\": [\n    999, ");
    std::string text = ReadText(t.Dir / "room.tmj"); // and drop one cell to keep the count
    const usize end = text.find("]", text.find("\"data\""));
    text.erase(text.rfind(",", end), end - text.rfind(",", end));
    WriteText(t.Dir / "room.tmj", text);
    const std::optional<Tilemap> odd = Tilemap::Load(nullptr, t.Dir / "room.tmj");
    CHECK(odd && odd->GetCell(0, {0, 0}) == 999 && odd->FindTileset(999) == nullptr);

    // A broken external tileset fails the map too.
    WriteText(t.Dir / "dungeon.tsj", "{\"name\": \"dungeon\"}");
    CHECK(!Tilemap::Load(nullptr, t.Dir / "room.tmj"));
}

// Through the asset manager: a handle, a placeholder for a missing map, and (debug builds) hot
// reload when the map or its external tileset changes.
TEST(TilemapAssetHotReload)
{
    TempMaps t;
    {
        Assets assets(std::make_unique<GpuAssetLoader>(nullptr), t.Dir);
        AssetHandle<Tilemap> map = assets.Load<Tilemap>("room.tmj");
        CHECK(!assets.GetInfo(map.GetId())->Placeholder && map->GetSize() == Vec2i(48, 30));
        AssetHandle<Tilemap> missing = assets.Load<Tilemap>("nope.tmj");
        CHECK(assets.GetInfo(missing.GetId())->Placeholder && missing->GetLayers().empty());
        CHECK(missing->GetCollision({0, 0}) == TileCollision::None); // empty but safe to use

        if (Assets::kHotReload) {
            // The map: rename a layer.
            t.EditRoom("\"Decor\"", "\"Props\"");
            assets.CheckForChanges();
            CHECK(assets.CheckForChanges() == 1);
            CHECK(map->FindLayer("Props") && !map->FindLayer("Decor"));
            // The external tileset: floor tile 0 becomes solid.
            std::string tsj = ReadText(t.Dir / "dungeon.tsj");
            tsj.replace(tsj.find("\"id\": 8"), 7, "\"id\": 0");
            CHECK(map->GetCollision({1, 1}) == TileCollision::None);
            WriteText(t.Dir / "dungeon.tsj", tsj);
            assets.CheckForChanges();
            CHECK(assets.CheckForChanges() == 1);
            CHECK(map->GetCollision({1, 1}) == TileCollision::Solid);
            // A broken edit keeps the last good version.
            WriteText(t.Dir / "room.tmj", "{ oops");
            assets.CheckForChanges();
            CHECK(assets.CheckForChanges() == 0 && map->GetSize() == Vec2i(48, 30));
        }
    }
}

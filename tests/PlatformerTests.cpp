// Platformer physics (Physics/Platformer.h) on small maps written from text: walking up and down
// 45 and 22.5 degree slopes, one-way platforms (landing, dropping through), coyote time, the jump
// buffer and variable jump height, turning (TurnAccel), and the same scripted input giving the same
// path twice.

#include <bit>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Emerald/Core/Log.h>
#include <Emerald/Physics/Platformer.h>
#include <Emerald/Tilemap/Tilemap.h>

#include "Test.h"

using namespace Emerald;
namespace fs = std::filesystem;

namespace {

const fs::path kSample = EMERALD_TEST_TILEMAPS; // sandbox/assets/tilemaps
constexpr f32 kDt = 1.0f / 120.0f;              // the sandbox's fixed step

// A map from rows of text, using the sandbox's platformer tileset: '#' ground, '/' 45 degree
// slope up, '\' down (the same tile flipped), 'a' 'b' 22.5 degrees up, 'B' 'A' down, '=' one-way.
std::optional<Tilemap> MakeMap(const std::vector<std::string>& rows)
{
    Log::Init({});
    // GIDs in platformer.tsj order: ground 1, slopes 3 to 5, one-way 6.
    constexpr std::string_view kTiles = ".#./ab=";
    constexpr std::string_view kFlipped = "...\\AB"; // the same tiles mirrored
    std::string data;
    for (const std::string& row : rows) {
        for (const char c : row) {
            u32 cell = 0;
            if (const usize i = kTiles.find(c); c != '.' && i != std::string_view::npos)
                cell = static_cast<u32>(i);
            else if (const usize f = kFlipped.find(c); c != '.' && f != std::string_view::npos)
                cell = static_cast<u32>(f) | kTileFlipX;
            data += (data.empty() ? "" : ",") + std::to_string(cell);
        }
    }
    const std::string tileset = (kSample / "platformer.tsj").generic_string();
    const std::string json = R"({"width":)" + std::to_string(rows[0].size()) + R"(,"height":)" +
                             std::to_string(rows.size()) +
                             R"(,"tilewidth":16,"tileheight":16,"orientation":"orthogonal",)" +
                             R"("tilesets":[{"firstgid":1,"source":")" + tileset + R"("}],)" +
                             R"("layers":[{"type":"tilelayer","name":"Ground","width":)" +
                             std::to_string(rows[0].size()) + R"(,"height":)" +
                             std::to_string(rows.size()) + R"(,"data":[)" + data + "]}]}";
    const fs::path file = fs::temp_directory_path() / "emerald_platformer_test.tmj";
    std::ofstream(file, std::ios::binary) << json;
    std::optional<Tilemap> map = Tilemap::Load(nullptr, file);
    fs::remove(file);
    return map;
}

// The floor under world x: the first tile from the top in that column (a slope's surface, or a
// tile's top).
f32 FloorUnder(const Tilemap& map, f32 x)
{
    for (i32 y = 0; y < map.GetSize().y; ++y) {
        const Vec2i tile(map.WorldToTile({x, 0.0f}).x, y);
        if (map.GetCollision(tile) != TileCollision::None)
            return map.GetSlopeFloorY(tile, x);
    }
    return 1e9f;
}

// A body standing on the ground with its feet at (x, feetY).
PlatformerBody StandingAt(f32 x, f32 feetY)
{
    PlatformerBody body;
    body.Position = {x, feetY - body.HalfSize.y};
    body.Grounded = true;
    return body;
}

void Step(PlatformerBody& body, const Tilemap& map, PlatformerInput input = {},
          const PlatformerTunables& tunables = {})
{
    StepPlatformer(body, input, tunables, map, kDt);
}

// 45 degrees up and down, flat between, then 22.5 degrees up and down; ground at y = 128.
const std::vector<std::string> kSlopes = {
    "....................................",  "....................................",
    "....................................",  "....................................",
    "....................................",  "....................................",
    "........./##\\.......................", "......../####\\......ab####BA........",
    "####################################",  "####################################",
};

} // namespace

TEST(PlatformerSlopeTiles)
{
    const std::optional<Tilemap> map = MakeMap(kSlopes);
    if (!map) {
        CHECK(false);
        return;
    }
    // The tileset's heights, and the flipped cell mirrored.
    CHECK(map->GetCollision({8, 7}) == TileCollision::Slope);
    CHECK(map->GetSlope({8, 7}).Left == 0.0f && map->GetSlope({8, 7}).Right == 16.0f);
    CHECK(map->GetSlope({13, 7}).Left == 16.0f && map->GetSlope({13, 7}).Right == 0.0f);
    CHECK(map->GetSlope({20, 7}).Right == 8.0f && map->GetSlope({21, 7}).Left == 8.0f);
    CHECK(map->GetSlope({0, 8}).Left == 0.0f); // not a slope: zeros
    // Floor heights along the tiles.
    CHECK_NEAR(map->GetSlopeFloorY({8, 7}, 128.0f), 128.0f);
    CHECK_NEAR(map->GetSlopeFloorY({8, 7}, 136.0f), 120.0f);
    CHECK_NEAR(map->GetSlopeFloorY({13, 7}, 212.0f), 116.0f);
    CHECK_NEAR(map->GetSlopeFloorY({20, 7}, 328.0f), 124.0f);
    CHECK_NEAR(map->GetSlopeFloorY({0, 8}, 5.0f), 128.0f); // other tiles: their top
    // Slopes never block sideways.
    const Aabb box{{120.0f, 90.0f}, {130.0f, 104.0f}}; // next to the slope at (9, 6)
    CHECK(!map->MoveAndCollide(box, {20.0f, 0.0f}).HitRight);
}

TEST(PlatformerWalksSlopesWithoutBouncing)
{
    const std::optional<Tilemap> map = MakeMap(kSlopes);
    if (!map) {
        CHECK(false);
        return;
    }
    // Right over both hills and back: on the ground every step, feet on the floor under the
    // center (so no hops going down, no sinking going up), always moving.
    for (const f32 direction : {1.0f, -1.0f}) {
        PlatformerBody body = StandingAt(direction > 0.0f ? 40.0f : 540.0f, 128.0f);
        Step(body, *map); // settle
        u32 slopeSteps = 0;
        for (i32 i = 0; i < 600; ++i) {
            const f32 before = body.Position.x;
            Step(body, *map, {.Move = direction});
            CHECK(body.Grounded);
            CHECK(!body.HitWall);
            CHECK((body.Position.x - before) * direction > 0.1f); // from the first step on
            CHECK_NEAR_EPS(body.GetFeet().y, FloorUnder(*map, body.Position.x), 0.02f);
            slopeSteps += body.OnSlope ? 1u : 0u;
            if (body.Position.x > 540.0f || body.Position.x < 40.0f)
                break;
        }
        CHECK(slopeSteps > 100); // it really walked on them
        CHECK(direction > 0.0f ? body.Position.x > 540.0f : body.Position.x < 40.0f);
    }
}

TEST(PlatformerSlopeLandingAndJumping)
{
    const std::optional<Tilemap> map = MakeMap(kSlopes);
    if (!map) {
        CHECK(false);
        return;
    }
    // Dropped above the 45 degree slope: lands on its surface, not on the ground inside it.
    PlatformerBody body;
    body.Position = {136.0f, 40.0f};
    for (i32 i = 0; i < 120 && !body.Grounded; ++i)
        Step(body, *map);
    CHECK(body.Grounded && body.OnSlope);
    CHECK_NEAR_EPS(body.GetFeet().y, 120.0f, 0.02f);
    // A jump up the slope ends on it, not inside it.
    body.Position.x = 132.0f;
    Step(body, *map); // settle on the slope at the new spot
    Step(body, *map, {.Move = 1.0f, .JumpPressed = true, .JumpHeld = true});
    CHECK(body.Jumped);
    i32 steps = 0;
    while (!body.Grounded && steps++ < 240)
        Step(body, *map, {.Move = 1.0f, .JumpHeld = true});
    CHECK(body.Grounded);
    CHECK_NEAR_EPS(body.GetFeet().y, FloorUnder(*map, body.Position.x), 0.02f);
}

TEST(PlatformerOneWayPlatforms)
{
    const std::optional<Tilemap> map = MakeMap({
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "......====......",
        "................",
        "................",
        "################",
        "################",
    });
    if (!map) {
        CHECK(false);
        return;
    }
    // Jump up through the platform (top at y = 96) and land on it.
    PlatformerBody body = StandingAt(128.0f, 144.0f);
    Step(body, *map);
    CHECK(body.Grounded && !body.OnOneWay);
    Step(body, *map, {.JumpPressed = true, .JumpHeld = true});
    CHECK(body.Jumped);
    f32 highest = body.GetFeet().y;
    for (i32 i = 0; i < 240 && !(body.Grounded && body.Velocity.y == 0.0f && i > 2); ++i) {
        Step(body, *map, {.JumpHeld = true});
        highest = Min(highest, body.GetFeet().y);
    }
    CHECK(highest < 96.0f);
    CHECK(body.Grounded && body.OnOneWay);
    CHECK_NEAR(body.GetFeet().y, 96.0f);

    // Down + jump drops through it to the ground; it is not a jump.
    Step(body, *map, {.JumpPressed = true, .JumpHeld = true, .Down = true});
    CHECK(body.DroppedThrough && !body.Jumped && !body.Grounded);
    for (i32 i = 0; i < 240 && !body.Grounded; ++i)
        Step(body, *map, {.Down = true});
    CHECK(body.Grounded && !body.OnOneWay);
    CHECK_NEAR(body.GetFeet().y, 144.0f);

    // On solid ground, down + jump is an ordinary jump.
    Step(body, *map, {.JumpPressed = true, .JumpHeld = true, .Down = true});
    CHECK(body.Jumped && !body.DroppedThrough);
}

TEST(PlatformerCoyoteTime)
{
    // A ledge ending at x = 128 over a pit.
    const std::optional<Tilemap> map =
        MakeMap({"................", "................", "................", "................",
                 "................", "................", "................", "########........",
                 "########........"});
    if (!map) {
        CHECK(false);
        return;
    }
    // Walks off the ledge and presses jump `late` steps after leaving it. Returns whether it
    // jumped.
    const auto jumpAfterLeaving = [&](i32 late, const PlatformerTunables& tunables) {
        PlatformerBody body = StandingAt(100.0f, 112.0f);
        Step(body, *map, {}, tunables);
        i32 steps = 0;
        while (body.Grounded && steps++ < 120)
            Step(body, *map, {.Move = 1.0f}, tunables);
        for (i32 i = 1; i < late; ++i)
            Step(body, *map, {.Move = 1.0f}, tunables);
        Step(body, *map, {.Move = 1.0f, .JumpPressed = true, .JumpHeld = true}, tunables);
        return body.Jumped;
    };
    const PlatformerTunables tunables; // 0.1 s = 12 steps
    CHECK(jumpAfterLeaving(1, tunables));
    CHECK(jumpAfterLeaving(10, tunables));
    CHECK(!jumpAfterLeaving(14, tunables));
    // Configurable: none at all, or much longer.
    CHECK(!jumpAfterLeaving(1, PlatformerTunables{.CoyoteTime = 0.0f}));
    CHECK(jumpAfterLeaving(30, PlatformerTunables{.CoyoteTime = 0.3f}));
}

TEST(PlatformerJumpBuffer)
{
    const std::optional<Tilemap> map =
        MakeMap({"........", "........", "........", "........", "........", "########"});
    if (!map) {
        CHECK(false);
        return;
    }
    // Falls from the top; jump is pressed `early` steps before the step that lands. Returns
    // whether the body jumped right after landing.
    const auto pressEarly = [&](i32 early, const PlatformerTunables& tunables) {
        PlatformerBody body;
        body.Position = {64.0f, 10.0f};
        i32 landing = 0; // how many steps the fall takes, without input
        {
            PlatformerBody probe = body;
            while (!probe.Grounded && landing < 600) {
                Step(probe, *map, {}, tunables);
                ++landing;
            }
        }
        for (i32 i = 1; i <= landing; ++i)
            Step(body, *map, {.JumpPressed = i == landing - early, .JumpHeld = true}, tunables);
        CHECK(body.Grounded || body.Jumped);
        if (body.Jumped) // pressed so late that it was already the landing step's jump
            return true;
        Step(body, *map, {.JumpHeld = true}, tunables);
        return body.Jumped;
    };
    const PlatformerTunables tunables; // 0.12 s = about 14 steps
    CHECK(pressEarly(1, tunables));
    CHECK(pressEarly(10, tunables));
    CHECK(!pressEarly(20, tunables));
    CHECK(!pressEarly(5, PlatformerTunables{.JumpBuffer = 0.0f}));
    CHECK(pressEarly(30, PlatformerTunables{.JumpBuffer = 0.4f}));
}

TEST(PlatformerVariableJumpHeight)
{
    const std::optional<Tilemap> map =
        MakeMap({"........", "........", "........", "........", "........", "........", "........",
                 "........", "........", "........", "########"});
    if (!map) {
        CHECK(false);
        return;
    }
    // Holding jump: about v^2 / 2g high. Letting go after 3 steps: much lower.
    const auto apex = [&](i32 held) {
        PlatformerBody body = StandingAt(64.0f, 160.0f);
        Step(body, *map);
        f32 top = body.GetFeet().y;
        for (i32 i = 0; i < 240; ++i) {
            Step(body, *map, {.JumpPressed = i == 0, .JumpHeld = i < held});
            top = Min(top, body.GetFeet().y);
        }
        CHECK(body.Grounded);
        return 160.0f - top;
    };
    const PlatformerTunables t;
    const f32 full = apex(1000);
    CHECK(full > 0.9f * t.JumpVelocity * t.JumpVelocity / (2.0f * t.Gravity));
    CHECK(full < 1.05f * t.JumpVelocity * t.JumpVelocity / (2.0f * t.Gravity));
    CHECK(apex(3) < 0.4f * full);
}

TEST(PlatformerWallsAndCeilings)
{
    const std::optional<Tilemap> map =
        MakeMap({"##########", "#........#", "#........#", "#........#", "##########"});
    if (!map) {
        CHECK(false);
        return;
    }
    PlatformerBody body = StandingAt(80.0f, 64.0f);
    for (i32 i = 0; i < 120; ++i)
        Step(body, *map, {.Move = 1.0f});
    CHECK(body.HitWall && body.Grounded);
    CHECK_NEAR(body.GetBox().Max.x, 144.0f);
    Step(body, *map, {.JumpPressed = true, .JumpHeld = true});
    for (i32 i = 0; i < 30 && !body.HitCeiling; ++i)
        Step(body, *map, {.JumpHeld = true});
    CHECK(body.HitCeiling);
    CHECK_NEAR(body.GetBox().Min.y, 16.0f);
}

TEST(PlatformerTurnAccel)
{
    const std::optional<Tilemap> map = MakeMap({"..........", "..........", "##########"});
    if (!map) {
        CHECK(false);
        return;
    }
    // Running right at full speed, then pushing left: TurnAccel applies until the body has
    // turned, GroundAccel after that.
    const PlatformerTunables tunables{.GroundAccel = 1000.0f, .TurnAccel = 3000.0f};
    PlatformerBody body = StandingAt(80.0f, 32.0f);
    body.Velocity.x = tunables.RunSpeed;
    Step(body, *map, {.Move = -1.0f}, tunables);
    CHECK_NEAR(body.Velocity.x, tunables.RunSpeed - tunables.TurnAccel * kDt);
    body.Velocity.x = -10.0f; // already moving left: no longer turning
    Step(body, *map, {.Move = -1.0f}, tunables);
    CHECK_NEAR(body.Velocity.x, -10.0f - tunables.GroundAccel * kDt);
    body.Velocity.x = 50.0f; // no input: GroundDecel
    Step(body, *map, {}, tunables);
    CHECK_NEAR(body.Velocity.x, 50.0f - tunables.GroundDecel * kDt);
}

TEST(PlatformerDeterministicReplay)
{
    Log::Init({});
    const std::optional<Tilemap> map = Tilemap::Load(nullptr, kSample / "platformer.tmj");
    if (!map) {
        CHECK(false);
        return;
    }
    const MapObject* spawn = map->FindObject("spawn");
    CHECK(spawn != nullptr);
    const Vec2 start = spawn ? spawn->Position : Vec2(64.0f, 200.0f);

    // A scripted run: 6000 steps (50 s) of input changing every few steps, from a fixed seed.
    struct Frame {
        PlatformerInput Input;
    };
    std::vector<Frame> script;
    u32 seed = 12345u;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    };
    PlatformerInput held;
    for (i32 i = 0; i < 6000; ++i) {
        if (i % 20 == 0) {
            const u32 r = next();
            held.Move = (r % 5 < 3) ? 1.0f : ((r % 5 == 3) ? -1.0f : 0.0f); // mostly right
            held.JumpHeld = (r & 0x30u) != 0;
            held.Down = (r & 0x700u) == 0x700u;
        }
        PlatformerInput in = held;
        in.JumpPressed = held.JumpHeld && (i % 20 == 0);
        script.push_back({in});
    }

    // Plays the script, respawning when the body falls out of the map; every position.
    const auto play = [&] {
        std::vector<Vec2> path;
        PlatformerBody body;
        body.Position = start - Vec2(0.0f, body.HalfSize.y);
        const PlatformerTunables tunables;
        u32 jumps = 0;
        for (const Frame& f : script) {
            StepPlatformer(body, f.Input, tunables, *map, kDt);
            jumps += body.Jumped ? 1u : 0u;
            if (body.Position.y > map->GetBounds().Max.y + 64.0f)
                body = PlatformerBody{.Position = start - Vec2(0.0f, 7.0f)};
            path.push_back(body.Position);
        }
        CHECK(jumps > 20);
        return path;
    };
    const std::vector<Vec2> a = play();
    const std::vector<Vec2> b = play();
    CHECK(a.size() == b.size());
    bool same = a.size() == b.size();
    f32 maxX = 0.0f;
    for (usize i = 0; same && i < a.size(); ++i) {
        same = std::bit_cast<u32>(a[i].x) == std::bit_cast<u32>(b[i].x) &&
               std::bit_cast<u32>(a[i].y) == std::bit_cast<u32>(b[i].y);
        maxX = Max(maxX, a[i].x);
    }
    CHECK(same);
    CHECK(maxX > 400.0f); // it got past the slopes
}

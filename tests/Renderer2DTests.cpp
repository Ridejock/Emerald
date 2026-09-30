// Tests for the CPU side of Renderer2D (batching, shape and sprite quad generation) and atlas
// parsing. No GPU is needed: recording only fills vectors, textures are CreateWithoutGpu, and
// Upload/Render are never called here.

#include <optional>
#include <utility>

#include <Emerald/Core/Log.h>
#include <Emerald/Renderer/Renderer2D.h>
#include <Emerald/Renderer/Texture.h>
#include <Emerald/Renderer/TextureAtlas.h>

#include "Test.h"

using namespace Emerald;

namespace {
const Vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};
}

TEST(Renderer2DPackColor)
{
    CHECK(Renderer2D::PackColor({1.0f, 0.0f, 0.0f, 1.0f}) == 0xFF0000FFu); // red: lowest byte
    CHECK(Renderer2D::PackColor({0.0f, 0.0f, 1.0f, 0.0f}) == 0x00FF0000u);
    CHECK(Renderer2D::PackColor({2.0f, -1.0f, 0.5f, 1.0f}) == 0xFF8000FFu); // clamped, rounded
}

TEST(Renderer2DTransform)
{
    const Transform2D t{.Position = {10.0f, 20.0f}, .Rotation = HalfPi, .Scale = {2.0f, 2.0f}};
    // (1, 0) -> scaled (2, 0) -> rotated a quarter turn (0, 2) -> moved (10, 22).
    CHECK_NEAR(t.Apply({1.0f, 0.0f}), Vec2(10.0f, 22.0f));
    CHECK_NEAR(Transform2D{}.Apply({3.0f, 4.0f}), Vec2(3.0f, 4.0f));
}

TEST(Renderer2DBatches)
{
    Renderer2D r;
    r.Begin(Mat4::Identity());
    r.DrawLine({0.0f, 0.0f}, {1.0f, 1.0f}, kWhite);
    const Vec2 triangle[] = {{0.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    r.DrawPolygon(triangle, kWhite, {.Position = {5.0f, 5.0f}});
    r.End();

    r.Begin(Mat4::OrthoPixelSpace(100.0f, 100.0f), {10, 20, 30, 40});
    r.DrawCircle({0.0f, 0.0f}, 10.0f, kWhite, 16);
    r.DrawPolyline(triangle, kWhite); // open: 2 segments
    r.DrawRect({0.0f, 0.0f}, {4.0f, 2.0f}, kWhite);
    r.End();

    const auto batches = r.GetBatches();
    CHECK(batches.size() == 2);
    CHECK(batches[0].FirstVertex == 0 && batches[0].VertexCount == 2 + 6);
    CHECK(batches[1].FirstVertex == 8 && batches[1].VertexCount == 32 + 4 + 8);
    CHECK(batches[0].Clip.w == 0); // no clip rectangle by default
    CHECK(batches[1].Clip.x == 10 && batches[1].Clip.y == 20 && batches[1].Clip.w == 30 &&
          batches[1].Clip.h == 40);
    CHECK(r.GetLineCount() == 4 + 22);

    // The polygon is closed and moved: its last segment ends at the (moved) first point.
    const auto v = r.GetVertices();
    CHECK_NEAR(v[2].Position, Vec2(5.0f, 4.0f));
    CHECK_NEAR(v[7].Position, Vec2(5.0f, 4.0f));
    // The circle starts and ends at (radius, 0).
    CHECK_NEAR(v[8].Position, Vec2(10.0f, 0.0f));
    CHECK_NEAR_EPS(v[8 + 31].Position, Vec2(10.0f, 0.0f), 1e-4f);

    r.Clear();
    CHECK(r.GetVertices().empty() && r.GetBatches().empty());
}

TEST(Renderer2DCommandsKeepCallOrder)
{
    const Texture a = Texture::CreateWithoutGpu(64, 64);
    const Texture b = Texture::CreateWithoutGpu(32, 32);
    CHECK(a.GetId() != 0 && a.GetId() != b.GetId());

    Renderer2D r;
    r.Begin(Mat4::Identity());
    r.DrawLine({0.0f, 0.0f}, {1.0f, 0.0f}, kWhite);
    r.DrawSprite(a, {10.0f, 10.0f});
    r.DrawSprite(a, {20.0f, 10.0f}); // same texture: same command
    r.DrawSprite(b, {30.0f, 10.0f}); // texture switch: new command
    r.DrawLine({0.0f, 1.0f}, {1.0f, 1.0f}, kWhite);
    r.DrawLine({0.0f, 2.0f}, {1.0f, 2.0f}, kWhite); // joins the previous lines
    r.DrawSprite(a, {40.0f, 10.0f});                // back to a: new command (order matters)
    r.End();

    r.Begin(Mat4::Identity()); // a new batch always starts new commands
    r.DrawSprite(a, {0.0f, 0.0f});
    r.End();

    using Type = Renderer2D::CommandType;
    const auto c = r.GetCommands();
    CHECK(c.size() == 6);
    CHECK(c[0].Type == Type::Lines && c[0].FirstVertex == 0 && c[0].VertexCount == 2);
    CHECK(c[1].Type == Type::Sprites && c[1].TextureId == a.GetId() && c[1].FirstVertex == 0 &&
          c[1].VertexCount == 12);
    CHECK(c[2].Type == Type::Sprites && c[2].TextureId == b.GetId() && c[2].FirstVertex == 12 &&
          c[2].VertexCount == 6);
    CHECK(c[3].Type == Type::Lines && c[3].FirstVertex == 2 && c[3].VertexCount == 4);
    CHECK(c[4].Type == Type::Sprites && c[4].TextureId == a.GetId() && c[4].FirstVertex == 18);
    CHECK(c[5].Type == Type::Sprites && c[5].FirstVertex == 24 && c[5].VertexCount == 6);
    CHECK(c[1].GpuTexture == nullptr); // no GPU: Render would skip it

    const auto batches = r.GetBatches();
    CHECK(batches.size() == 2);
    CHECK(batches[0].FirstCommand == 0 && batches[0].CommandCount == 5);
    CHECK(batches[1].FirstCommand == 5 && batches[1].CommandCount == 1);
    CHECK(batches[0].VertexCount == 6); // the batch's line vertices
    CHECK(r.GetSpriteCount() == 5 && r.GetLineCount() == 3);

    r.Clear();
    CHECK(r.GetCommands().empty() && r.GetSpriteVertices().empty());
}

TEST(Renderer2DSpriteQuad)
{
    const Texture texture = Texture::CreateWithoutGpu(100, 50);
    Renderer2D r;
    r.Begin(Mat4::Identity());
    // Region (10, 5) 20 x 10 of a 100 x 50 texture, centered on (50, 60), twice the size.
    const Sprite sprite{&texture, {{10.0f, 5.0f}, {20.0f, 10.0f}}};
    r.DrawSprite(sprite, {50.0f, 60.0f}, {.Scale = {2.0f, 2.0f}, .Tint = {1.0f, 0.0f, 0.0f, 1.0f}});
    r.End();

    const auto v = r.GetSpriteVertices();
    CHECK(v.size() == 6);
    // Triangles: top-left, top-right, bottom-right, top-left, bottom-right, bottom-left.
    CHECK_NEAR(v[0].Position, Vec2(30.0f, 50.0f));
    CHECK_NEAR(v[1].Position, Vec2(70.0f, 50.0f));
    CHECK_NEAR(v[2].Position, Vec2(70.0f, 70.0f));
    CHECK_NEAR(v[5].Position, Vec2(30.0f, 70.0f));
    CHECK(v[3].Position == v[0].Position && v[4].Position == v[2].Position);
    // UVs: the region divided by the texture size.
    CHECK_NEAR(v[0].TexCoord, Vec2(0.1f, 0.1f));
    CHECK_NEAR(v[2].TexCoord, Vec2(0.3f, 0.3f));
    CHECK(v[0].Color == Renderer2D::PackColor({1.0f, 0.0f, 0.0f, 1.0f}));
}

TEST(Renderer2DSpriteOptions)
{
    const Texture texture = Texture::CreateWithoutGpu(10, 10);
    Renderer2D r;
    r.Begin(Mat4::Identity());
    // 1) Explicit size, origin at the top-left: the quad starts at the position.
    r.DrawSprite(texture, {5.0f, 5.0f}, {.Size = {4.0f, 2.0f}, .Origin = {0.0f, 0.0f}});
    // 2) A quarter turn around the center: the top-left corner ends up top-right.
    r.DrawSprite(texture, {0.0f, 0.0f}, {.Rotation = HalfPi});
    // 3) Flipped both ways: UVs swapped.
    r.DrawSprite(texture, {0.0f, 0.0f}, {.FlipX = true, .FlipY = true});
    // 4) Pixel snap: the top-left corner (10.3 - 5, 7.6 - 5) is rounded to (5, 3).
    r.DrawSprite(texture, {10.3f, 7.6f}, {.PixelSnap = true});
    r.End();

    const auto v = r.GetSpriteVertices();
    CHECK(v.size() == 24);
    CHECK_NEAR(v[0].Position, Vec2(5.0f, 5.0f));
    CHECK_NEAR(v[2].Position, Vec2(9.0f, 7.0f));
    // (-5, -5) turned clockwise on screen (+x towards +y) is (5, -5).
    CHECK_NEAR_EPS(v[6].Position, Vec2(5.0f, -5.0f), 1e-4f);
    CHECK_NEAR_EPS(v[8].Position, Vec2(-5.0f, 5.0f), 1e-4f); // bottom-right -> bottom-left
    CHECK_NEAR(v[12].TexCoord, Vec2(1.0f, 1.0f));            // top-left shows bottom-right
    CHECK_NEAR(v[14].TexCoord, Vec2(0.0f, 0.0f));
    CHECK_NEAR(v[18].Position, Vec2(5.0f, 3.0f));
    CHECK_NEAR(v[20].Position, Vec2(15.0f, 13.0f));

    // Nothing is recorded for a sprite without a texture.
    r.Begin(Mat4::Identity());
    r.DrawSprite(Sprite{}, {0.0f, 0.0f});
    r.End();
    CHECK(r.GetSpriteVertices().size() == 24);
}

TEST(TextureAtlasParse)
{
    Emerald::Log::Init({}); // console only; the atlas logs skipped entries and failed lookups

    const auto flat = TextureAtlas::ParseRegions(R"({
        "ship":  {"x": 1, "y": 2, "w": 38, "h": 80},
        "enemy": {"x": 40, "y": 1, "w": 80, "h": 46},
        "broken": {"x": 1, "y": 1},
        "text": "not a region"
    })");
    CHECK(flat.has_value() && flat->size() == 2); // incomplete entries are skipped
    if (flat) {
        const TextureRegion& ship = flat->at("ship");
        CHECK(ship.Position == Vec2(1.0f, 2.0f) && ship.Size == Vec2(38.0f, 80.0f));
    }

    // TexturePacker's "JSON (Hash)" layout.
    const auto packer = TextureAtlas::ParseRegions(
        R"({"frames": {"rock": {"frame": {"x": 0, "y": 0, "w": 16, "h": 16}}}, "meta": {}})");
    CHECK(packer.has_value() && packer->size() == 1 && packer->contains("rock"));

    CHECK(!TextureAtlas::ParseRegions("{ not json").has_value());
    CHECK(!TextureAtlas::ParseRegions("[1, 2, 3]").has_value());
    CHECK(!TextureAtlas::ParseRegions("{}").has_value());

    // Lookup; regions outside the texture are dropped.
    TextureAtlas::RegionMap regions = *flat;
    regions["outside"] = {{100.0f, 100.0f}, {50.0f, 50.0f}};
    const TextureAtlas atlas =
        TextureAtlas::Create(Texture::CreateWithoutGpu(128, 128), std::move(regions));
    CHECK(atlas.Contains("ship") && atlas.Contains("enemy") && !atlas.Contains("outside"));
    const std::optional<Sprite> ship = atlas.Find("ship");
    CHECK(ship.has_value() && ship->Source == &atlas.GetTexture() &&
          ship->Region.Size == Vec2(38.0f, 80.0f));
    CHECK(!atlas.Find("nope").has_value());
    const Sprite fallback = atlas.Get("nope"); // logged, whole texture
    CHECK(fallback.Source == &atlas.GetTexture() && fallback.Region.Size == Vec2(128.0f, 128.0f));

    // Crop: part of a region (e.g. an animation frame).
    const Sprite top = ship->Crop({0.0f, 0.0f}, {38.0f, 60.0f});
    CHECK(top.Region.Position == Vec2(1.0f, 2.0f) && top.Region.Size == Vec2(38.0f, 60.0f));

    Emerald::Log::Shutdown();
}

TEST(Renderer2DBlendModes)
{
    // A blend change starts a new command; Begin goes back to Alpha.
    Renderer2D r;
    const Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    r.Begin(Mat4::Identity());
    r.DrawLine({0.0f, 0.0f}, {1.0f, 0.0f}, white);
    r.SetBlendMode(BlendMode::Additive);
    r.DrawLine({0.0f, 1.0f}, {1.0f, 1.0f}, white);
    r.DrawLine({0.0f, 2.0f}, {1.0f, 2.0f}, white); // same blend: joins the previous line
    r.End();
    r.Begin(Mat4::Identity());
    CHECK(r.GetBlendMode() == BlendMode::Alpha);
    r.DrawLine({0.0f, 0.0f}, {1.0f, 0.0f}, white);
    r.End();

    const auto c = r.GetCommands();
    CHECK(c.size() == 3);
    CHECK(c[0].Blend == BlendMode::Alpha && c[0].VertexCount == 2);
    CHECK(c[1].Blend == BlendMode::Additive && c[1].VertexCount == 4);
    CHECK(c[2].Blend == BlendMode::Alpha);
}

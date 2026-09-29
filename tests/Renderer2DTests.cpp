// Tests for the CPU side of Renderer2D (batching and shape generation). No GPU is needed:
// recording only fills vectors; Upload/Render are never called here.

#include <Emerald/Renderer/Renderer2D.h>

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

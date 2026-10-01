// Camera2D: view-projection, screen <-> world conversion with zoom, rotation and letterboxing,
// follow, bounds and shake. Pure math, no GPU.

#include <cmath>

#include <Emerald/Renderer/Camera2D.h>
#include <Emerald/Renderer/Renderer2D.h>

#include "Test.h"

using namespace Emerald;

namespace {
// Clip space (-1..1, +Y up) -> target pixels (top-left origin, +Y down).
Vec2 ToPixels(const Camera2D& camera, const Vec2& world)
{
    const Vec4 clip = camera.GetViewProjection() * Vec4(world.x, world.y, 0.0f, 1.0f);
    const Vec2 target = camera.GetTargetSize();
    return {(clip.x + 1.0f) * 0.5f * target.x, (1.0f - clip.y) * 0.5f * target.y};
}
} // namespace

TEST(CameraDefaultIsPixelSpace)
{
    // A default camera over its own view size is exactly OrthoPixelSpace, and does not clip.
    const Camera2D camera({1280.0f, 720.0f});
    CHECK_NEAR(camera.GetViewProjection(), Mat4::OrthoPixelSpace(1280.0f, 720.0f));
    CHECK_NEAR(camera.WorldToScreen({100.0f, 50.0f}), Vec2(100.0f, 50.0f));
    const SDL_Rect clip = camera.GetClip();
    CHECK(clip.x == 0 && clip.y == 0 && clip.w == 1280 && clip.h == 720);
}

TEST(CameraLetterbox)
{
    // A 16:9 view in a 4:3 target: scaled to the width, bars top and bottom.
    Camera2D camera({1280.0f, 720.0f});
    camera.SetTargetSize({640.0f, 480.0f});
    const Viewport vp = camera.GetViewport();
    CHECK_NEAR(vp.Position, Vec2(0.0f, 60.0f));
    CHECK_NEAR(vp.Size, Vec2(640.0f, 360.0f));
    CHECK_NEAR(camera.GetPixelsPerUnit(), 0.5f);
    const SDL_Rect clip = camera.GetClip();
    CHECK(clip.x == 0 && clip.y == 60 && clip.w == 640 && clip.h == 360);
    // The view's corners land on the viewport's corners.
    CHECK_NEAR(camera.WorldToScreen({0.0f, 0.0f}), Vec2(0.0f, 60.0f));
    CHECK_NEAR(camera.WorldToScreen({1280.0f, 720.0f}), Vec2(640.0f, 420.0f));
    // Odd bar sizes round outwards.
    camera.SetTargetSize({101.0f, 100.0f});
    const SDL_Rect odd = camera.GetClip();
    CHECK(odd.y <= 21 && odd.y + odd.h >= 79);
}

TEST(CameraZoomAndRotation)
{
    Camera2D camera({200.0f, 100.0f});
    camera.SetPosition({0.0f, 0.0f});
    camera.SetZoom(2.0f);
    // The eye is the center; 10 units right are 20 pixels right at zoom 2.
    CHECK_NEAR(camera.WorldToScreen({0.0f, 0.0f}), Vec2(100.0f, 50.0f));
    CHECK_NEAR(camera.WorldToScreen({10.0f, 0.0f}), Vec2(120.0f, 50.0f));
    CHECK_NEAR(camera.GetVisibleBounds().Max, Vec2(50.0f, 25.0f));

    // Turning the camera a quarter turn makes the world turn the other way on screen.
    camera.SetRotation(HalfPi);
    CHECK_NEAR_EPS(camera.WorldToScreen({10.0f, 0.0f}), Vec2(100.0f, 30.0f), 1e-4f);
    CHECK_NEAR_EPS(camera.GetVisibleBounds().Max, Vec2(25.0f, 50.0f), 1e-4f);
}

TEST(CameraRoundTrip)
{
    // ScreenToWorld undoes WorldToScreen, and both agree with the GPU matrix.
    Camera2D camera({320.0f, 180.0f});
    camera.SetTargetSize({1000.0f, 1000.0f}); // letterboxed
    camera.SetPosition({-40.0f, 75.0f});
    camera.SetZoom(1.7f);
    camera.SetRotation(0.6f);
    const Vec2 points[] = {{0.0f, 0.0f}, {-40.0f, 75.0f}, {123.0f, -45.0f}, {-300.0f, 260.0f}};
    for (const Vec2& p : points) {
        const Vec2 screen = camera.WorldToScreen(p);
        CHECK_NEAR_EPS(camera.ScreenToWorld(screen), p, 1e-3f);
        CHECK_NEAR_EPS(ToPixels(camera, p), screen, 1e-2f);
    }
    CHECK_NEAR_EPS(camera.ScreenToWorld({500.0f, 500.0f}), Vec2(-40.0f, 75.0f), 1e-3f);
}

TEST(CameraBoundsClamp)
{
    Camera2D camera({100.0f, 100.0f});
    camera.SetBounds({.Min = {0.0f, 0.0f}, .Max = {1000.0f, 300.0f}});
    camera.SetPosition({-500.0f, 900.0f});
    CHECK_NEAR(camera.GetPosition(), Vec2(50.0f, 250.0f)); // the view's edge on the bounds
    // Zooming out past the bounds' height centers that axis.
    camera.SetZoom(0.25f); // 400 x 400 visible
    camera.Update(0.0f);
    CHECK_NEAR(camera.GetPosition(), Vec2(200.0f, 150.0f));
    // Rotated, the view's bounding box still stays inside.
    camera.SetZoom(1.0f);
    camera.SetRotation(0.5f);
    camera.SetPosition({0.0f, 0.0f});
    const Rect2D visible = camera.GetVisibleBounds();
    CHECK(visible.Min.x >= -1e-3f && visible.Min.y >= -1e-3f);
}

TEST(CameraFollowDeadZoneAndDamping)
{
    Camera2D camera({100.0f, 100.0f});
    camera.SetPosition({0.0f, 0.0f});
    camera.GetFollowParams() = {.DeadZone = {10.0f, 10.0f}, .Damping = 0.0f};
    camera.Follow({5.0f, -8.0f}, 0.01f); // inside the dead zone: no movement
    CHECK_NEAR(camera.GetPosition(), Vec2(0.0f, 0.0f));
    camera.Follow({30.0f, 0.0f}, 0.01f); // snaps until the target is on the dead zone's edge
    CHECK_NEAR(camera.GetPosition(), Vec2(20.0f, 0.0f));

    // Damped follow covers the same distance in one second at any step size.
    const auto run = [](f32 dt) {
        Camera2D c({100.0f, 100.0f});
        c.SetPosition({0.0f, 0.0f});
        c.GetFollowParams().Damping = 3.0f;
        for (f32 t = 0.0f; t < 1.0f - 0.5f * dt; t += dt)
            c.Follow({100.0f, 0.0f}, dt);
        return c.GetPosition().x;
    };
    CHECK_NEAR_EPS(run(1.0f / 30.0f), run(1.0f / 240.0f), 1e-2f);
    CHECK_NEAR_EPS(run(1.0f / 120.0f), 100.0f * (1.0f - std::exp(-3.0f)), 1e-2f);
}

TEST(CameraShake)
{
    Camera2D camera({100.0f, 100.0f});
    camera.SetPosition({0.0f, 0.0f});
    camera.GetShakeParams() = {.MaxOffset = 10.0f, .MaxAngle = 0.1f, .Decay = 2.0f};
    CHECK_NEAR(camera.GetEyePosition(), Vec2(0.0f, 0.0f)); // no trauma: no shake
    camera.AddTrauma(0.7f);
    camera.AddTrauma(0.7f);
    CHECK_NEAR(camera.GetTrauma(), 1.0f); // capped
    camera.Update(0.13f);
    const Vec2 eye = camera.GetEyePosition();
    CHECK(Length(eye) > 0.0f && Length(eye) <= 10.0f * std::sqrt(2.0f));
    CHECK(std::abs(camera.GetEyeRotation()) <= 0.1f);
    // Decays linearly with time: the same after 4 x 0.1 s as after 0.4 s.
    Camera2D other = camera;
    for (i32 i = 0; i < 4; ++i)
        camera.Update(0.1f);
    other.Update(0.4f);
    CHECK_NEAR(camera.GetTrauma(), other.GetTrauma());
    CHECK_NEAR(camera.GetEyePosition(), other.GetEyePosition());
    camera.Update(1.0f);
    CHECK_NEAR(camera.GetTrauma(), 0.0f);
    CHECK_NEAR(camera.GetEyePosition(), Vec2(0.0f, 0.0f));
    // Disabled: nothing moves.
    camera.AddTrauma(1.0f);
    camera.GetShakeParams().Enabled = false;
    CHECK_NEAR(camera.GetEyePosition(), Vec2(0.0f, 0.0f));
}

TEST(CameraRenderer2DBatch)
{
    Camera2D camera({1280.0f, 720.0f});
    camera.SetTargetSize({1920.0f, 1200.0f});
    Renderer2D r;
    r.Begin(camera);
    r.End();
    const Renderer2D::Batch& batch = r.GetBatches()[0];
    CHECK_NEAR(batch.ViewProjection, camera.GetViewProjection());
    CHECK(batch.Clip.x == 0 && batch.Clip.y == 60 && batch.Clip.w == 1920 && batch.Clip.h == 1080);
}

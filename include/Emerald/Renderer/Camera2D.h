#pragma once

#include <optional>

#include <SDL3/SDL_rect.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// Axis-aligned rectangle (world units).
struct Rect2D {
    Vec2 Min{};
    Vec2 Max{};
};

// Where the camera's view lands on the render target, in pixels with (0, 0) at the top-left.
struct Viewport {
    Vec2 Position{};
    Vec2 Size{};
};

// A 2D camera: which part of the world shows where on the render target.
//
// It looks at Position (the center of the view) and shows ViewSize world units at zoom 1,
// rotated by Rotation (radians, the same sense as Transform2D). The view is fitted into the
// render target with a uniform scale and centered, with bars if the aspect ratios differ
// (letterboxing). For a plain 1:1 camera, set the view size to the window size.
//
//   void OnFixedUpdate(f32 dt) override { m_Camera.Follow(player, dt); m_Camera.Update(dt); }
//   void OnRender2D(Renderer2D& r) override
//   {
//       m_Camera.SetTargetSize({frameWidth, frameHeight}); // render target pixels
//       r.Begin(m_Camera);                                 // world, clipped to the viewport
//       ... r.End();
//       r.Begin(Mat4::OrthoPixelSpace(w, h)); ... r.End(); // HUD / text: screen space
//   }
//
// Shake is trauma based: AddTrauma() bumps a 0..1 value that decays linearly; the shake is
// trauma^2 times the maximum, driven by smooth noise over time, so it looks the same at any
// frame rate. Results (matrices, conversions) include the shake and respect the bounds.
class Camera2D {
public:
    struct FollowParams {
        Vec2 DeadZone{};    // half size (world units) of the box the target moves freely in
        f32 Damping = 8.0f; // per second: higher catches up faster; 0 snaps
    };
    struct ShakeParams {
        f32 MaxOffset = 12.0f; // view units at trauma 1 (the same size on screen at any zoom)
        f32 MaxAngle = 0.04f;  // radians at trauma 1
        f32 Frequency = 18.0f; // noise speed, in changes per second
        f32 Decay = 1.5f;      // trauma lost per second
        bool Enabled = true;   // false: trauma still decays, but nothing moves
    };

    explicit Camera2D(const Vec2& viewSize = {1280.0f, 720.0f});

    void SetPosition(const Vec2& position);
    [[nodiscard]] Vec2 GetPosition() const { return m_Position; }
    void SetZoom(f32 zoom); // > 1 magnifies; clamped to [0.01, 100]
    [[nodiscard]] f32 GetZoom() const { return m_Zoom; }
    void SetRotation(f32 radians) { m_Rotation = radians; }
    [[nodiscard]] f32 GetRotation() const { return m_Rotation; }
    void SetViewSize(const Vec2& size); // world units visible at zoom 1
    [[nodiscard]] Vec2 GetViewSize() const { return m_ViewSize; }
    void SetTargetSize(const Vec2& pixels); // the render target, in pixels
    [[nodiscard]] Vec2 GetTargetSize() const { return m_TargetSize; }

    // The camera never shows anything outside these (a smaller area is centered).
    void SetBounds(const Rect2D& bounds);
    void ClearBounds() { m_Bounds.reset(); }
    [[nodiscard]] const std::optional<Rect2D>& GetBounds() const { return m_Bounds; }

    [[nodiscard]] FollowParams& GetFollowParams() { return m_Follow; }
    [[nodiscard]] ShakeParams& GetShakeParams() { return m_Shake; }

    // Moves towards `target` (call once per fixed step): only once it leaves the dead zone, with
    // exponential damping, so the result does not depend on the step size.
    void Follow(const Vec2& target, f32 dt);
    void AddTrauma(f32 amount); // trauma stays in [0, 1]
    [[nodiscard]] f32 GetTrauma() const { return m_Trauma; }
    // Advances the shake and decays trauma.
    void Update(f32 dt);

    // --- Results (with shake, inside the bounds) ---
    [[nodiscard]] Vec2 GetEyePosition() const; // where the camera really looks
    [[nodiscard]] f32 GetEyeRotation() const;
    [[nodiscard]] Viewport GetViewport() const;
    // The viewport rounded outwards, for Renderer2D::Begin's clip rectangle.
    [[nodiscard]] SDL_Rect GetClip() const;
    // Target pixels per world unit (letterbox scale * zoom).
    [[nodiscard]] f32 GetPixelsPerUnit() const;
    [[nodiscard]] Mat4 GetView() const; // world -> target pixels
    [[nodiscard]] Mat4 GetViewProjection() const;
    // Target pixels (top-left origin) <-> world.
    [[nodiscard]] Vec2 ScreenToWorld(const Vec2& pixel) const;
    [[nodiscard]] Vec2 WorldToScreen(const Vec2& world) const;
    // World area inside the viewport (bounding box when rotated), e.g. for culling.
    [[nodiscard]] Rect2D GetVisibleBounds() const;

private:
    // Half size of the visible world box for a rotation.
    [[nodiscard]] Vec2 GetHalfExtents(f32 rotation) const;
    [[nodiscard]] Vec2 ClampToBounds(const Vec2& position, f32 rotation) const;

    Vec2 m_Position;
    f32 m_Zoom = 1.0f;
    f32 m_Rotation = 0.0f;
    Vec2 m_ViewSize;
    Vec2 m_TargetSize;
    std::optional<Rect2D> m_Bounds;
    FollowParams m_Follow;
    ShakeParams m_Shake;
    f32 m_Trauma = 0.0f;
    f32 m_ShakeTime = 0.0f;
};

} // namespace Emerald

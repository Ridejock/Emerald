#include "Emerald/Renderer/Camera2D.h"

#include <cmath>

#include "Emerald/Math/Common.h"

namespace Emerald {

namespace {

// Hash of a lattice point to [-1, 1].
f32 Hash(i32 n, u32 seed)
{
    u32 x = static_cast<u32>(n) * 0x9E3779B1u ^ seed * 0x85EBCA77u;
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return static_cast<f32>(x >> 8) * (2.0f / 16777215.0f) - 1.0f;
}

// Smooth 1D value noise in [-1, 1]: random values at whole t, eased in between.
f32 Noise(f32 t, u32 seed)
{
    const f32 cell = std::floor(t);
    const f32 f = t - cell;
    const i32 i = static_cast<i32>(cell);
    return Lerp(Hash(i, seed), Hash(i + 1, seed), f * f * (3.0f - 2.0f * f));
}

Vec2 Rotate(const Vec2& v, f32 radians)
{
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

} // namespace

Camera2D::Camera2D(const Vec2& viewSize)
    : m_Position(viewSize * 0.5f), m_ViewSize(viewSize), m_TargetSize(viewSize)
{
}

void Camera2D::SetPosition(const Vec2& position)
{
    m_Position = ClampToBounds(position, m_Rotation);
}

void Camera2D::SetZoom(f32 zoom)
{
    m_Zoom = Clamp(zoom, 0.01f, 100.0f);
}

void Camera2D::SetViewSize(const Vec2& size)
{
    m_ViewSize = Max(size, Vec2(1.0f));
}

void Camera2D::SetTargetSize(const Vec2& pixels)
{
    m_TargetSize = Max(pixels, Vec2(1.0f));
}

void Camera2D::SetBounds(const Rect2D& bounds)
{
    m_Bounds = bounds;
    m_Position = ClampToBounds(m_Position, m_Rotation);
}

void Camera2D::Follow(const Vec2& target, f32 dt)
{
    // The closest position that has the target inside the dead zone.
    const auto axis = [](f32 position, f32 target, f32 deadZone) {
        const f32 d = target - position;
        if (d > deadZone)
            return target - deadZone;
        if (d < -deadZone)
            return target + deadZone;
        return position;
    };
    const Vec2 desired(axis(m_Position.x, target.x, m_Follow.DeadZone.x),
                       axis(m_Position.y, target.y, m_Follow.DeadZone.y));
    const f32 t = m_Follow.Damping > 0.0f ? 1.0f - std::exp(-m_Follow.Damping * dt) : 1.0f;
    m_Position = ClampToBounds(Lerp(m_Position, desired, t), m_Rotation);
}

void Camera2D::AddTrauma(f32 amount)
{
    m_Trauma = Clamp(m_Trauma + amount, 0.0f, 1.0f);
}

void Camera2D::Update(f32 dt)
{
    m_ShakeTime += dt;
    m_Trauma = Max(m_Trauma - m_Shake.Decay * dt, 0.0f);
    m_Position = ClampToBounds(m_Position, m_Rotation); // zoom or rotation may have changed
}

f32 Camera2D::GetEyeRotation() const
{
    if (!m_Shake.Enabled || m_Trauma <= 0.0f)
        return m_Rotation;
    const f32 shake = m_Trauma * m_Trauma;
    return m_Rotation + m_Shake.MaxAngle * shake * Noise(m_ShakeTime * m_Shake.Frequency, 3);
}

Vec2 Camera2D::GetEyePosition() const
{
    Vec2 eye = m_Position;
    if (m_Shake.Enabled && m_Trauma > 0.0f) {
        const f32 t = m_ShakeTime * m_Shake.Frequency;
        const f32 amount = m_Shake.MaxOffset * m_Trauma * m_Trauma / m_Zoom;
        eye += Vec2(Noise(t, 1), Noise(t, 2)) * amount;
    }
    return ClampToBounds(eye, GetEyeRotation());
}

Viewport Camera2D::GetViewport() const
{
    const f32 scale = Min(m_TargetSize.x / m_ViewSize.x, m_TargetSize.y / m_ViewSize.y);
    const Vec2 size = m_ViewSize * scale;
    return {.Position = (m_TargetSize - size) * 0.5f, .Size = size};
}

SDL_Rect Camera2D::GetClip() const
{
    const Viewport vp = GetViewport();
    const i32 left = static_cast<i32>(std::floor(vp.Position.x));
    const i32 top = static_cast<i32>(std::floor(vp.Position.y));
    return {left, top, static_cast<i32>(std::ceil(vp.Position.x + vp.Size.x)) - left,
            static_cast<i32>(std::ceil(vp.Position.y + vp.Size.y)) - top};
}

f32 Camera2D::GetPixelsPerUnit() const
{
    return GetViewport().Size.x / m_ViewSize.x * m_Zoom;
}

Mat4 Camera2D::GetView() const
{
    // Read right to left: eye to the origin, undo the camera's rotation, scale to pixels, move
    // to the viewport's center.
    const Viewport vp = GetViewport();
    return Mat4::Translate(vp.Position + vp.Size * 0.5f) * Mat4::Scale(Vec2(GetPixelsPerUnit())) *
           Mat4::RotateZ(-GetEyeRotation()) * Mat4::Translate(-GetEyePosition());
}

Mat4 Camera2D::GetViewProjection() const
{
    return Mat4::OrthoPixelSpace(m_TargetSize.x, m_TargetSize.y) * GetView();
}

Vec2 Camera2D::ScreenToWorld(const Vec2& pixel) const
{
    const Viewport vp = GetViewport();
    const Vec2 local = (pixel - (vp.Position + vp.Size * 0.5f)) / GetPixelsPerUnit();
    return GetEyePosition() + Rotate(local, GetEyeRotation());
}

Vec2 Camera2D::WorldToScreen(const Vec2& world) const
{
    const Viewport vp = GetViewport();
    const Vec2 local = Rotate(world - GetEyePosition(), -GetEyeRotation());
    return vp.Position + vp.Size * 0.5f + local * GetPixelsPerUnit();
}

Rect2D Camera2D::GetVisibleBounds() const
{
    const Vec2 eye = GetEyePosition();
    const Vec2 half = GetHalfExtents(GetEyeRotation());
    return {.Min = eye - half, .Max = eye + half};
}

Vec2 Camera2D::GetHalfExtents(f32 rotation) const
{
    const Vec2 half = m_ViewSize * (0.5f / m_Zoom);
    const f32 c = std::abs(std::cos(rotation));
    const f32 s = std::abs(std::sin(rotation));
    return {c * half.x + s * half.y, s * half.x + c * half.y};
}

Vec2 Camera2D::ClampToBounds(const Vec2& position, f32 rotation) const
{
    if (!m_Bounds)
        return position;
    const Vec2 half = GetHalfExtents(rotation);
    // Per axis: centered if the bounds are smaller than the view, else kept inside.
    const auto axis = [](f32 p, f32 lo, f32 hi, f32 h) {
        return hi - lo <= 2.0f * h ? 0.5f * (lo + hi) : Clamp(p, lo + h, hi - h);
    };
    return {axis(position.x, m_Bounds->Min.x, m_Bounds->Max.x, half.x),
            axis(position.y, m_Bounds->Min.y, m_Bounds->Max.y, half.y)};
}

} // namespace Emerald

#pragma once

#include <cmath>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// From angle a to angle b (radians) the short way round: from 350 degrees to 10 degrees passes 0,
// not 180. t = 0 and 1 give exactly a and b.
[[nodiscard]] inline f32 LerpAngle(f32 a, f32 b, f32 t)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    f32 delta = std::fmod(b - a, TwoPi);
    if (delta > Pi)
        delta -= TwoPi;
    else if (delta < -Pi)
        delta += TwoPi;
    return a + delta * t;
}

// A value as it was after the last two fixed steps, for drawing in between (render
// interpolation). The simulation reads and writes Current; drawing uses Get(alpha) with the
// frame's alpha (Application::GetRenderAlpha, Scene::GetRenderAlpha):
//
//   Interpolated<Vec2> m_Position;
//   void OnFixedUpdate(f32 dt) { m_Position.BeginStep(); m_Position.Set(m_Position.Current + v *
//   dt); } void OnRender2D(Renderer2D& r) { r.DrawSprite(ship, m_Position.Get(GetRenderAlpha())); }
//   void Respawn() { m_Position.Snap(spawn); } // a jump: no slide across the screen
//
// T needs Lerp(T, T, f32) (f32, Vec2, Vec3, ...). For angles use InterpolatedAngle.
template <typename T> struct Interpolated {
    T Previous{};
    T Current{};

    Interpolated() = default;
    explicit Interpolated(const T& value) : Previous(value), Current(value) {}

    // At the start of each fixed step, before the step changes Current.
    void BeginStep() { Previous = Current; }
    void Set(const T& value) { Current = value; }
    // Teleports, spawns, respawns: drawn at `value` right away.
    void Snap(const T& value) { Previous = Current = value; }
    void Snap() { Previous = Current; }

    // alpha 0: the previous step, 1: the current one.
    [[nodiscard]] T Get(f32 alpha) const { return Lerp(Previous, Current, alpha); }
};

// An angle (radians), blended the short way round.
struct InterpolatedAngle : Interpolated<f32> {
    using Interpolated<f32>::Interpolated;
    [[nodiscard]] f32 Get(f32 alpha) const { return LerpAngle(Previous, Current, alpha); }
};

} // namespace Emerald

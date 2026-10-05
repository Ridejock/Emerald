#pragma once

#include <cmath>
#include <span>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Common.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec3.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

// How many lights Renderer2D passes to the lit sprite shader at once. More than this are ignored
// (AddLight returns false). Kept small: they are uniforms, not a deferred light volume.
inline constexpr u32 kMaxLights = 8;

enum class LightKind : u8 { Point, Spot };

// A 2D light. The scene sits on z = 0; Z is how far above it the light hangs (so normals facing
// the camera still catch light from the side, like a desk lamp over a table). Spot is a point
// light with a cone: Direction is where it points, InnerAngle / OuterAngle (radians from that
// axis) go from full brightness to zero.
struct Light {
    LightKind Kind = LightKind::Point;
    Vec2 Position{};
    f32 Z = 40.0f;
    Vec3 Color{1.0f, 1.0f, 1.0f};
    f32 Intensity = 1.0f;
    f32 Radius = 200.0f;
    f32 Falloff = 1.0f;         // 1 = linear edge; higher = sharper falloff near the edge
    Vec2 Direction{0.0f, 1.0f}; // Spot: world-space aim (length ignored)
    f32 InnerAngle = 0.3f;      // Spot: full brightness inside this angle from Direction
    f32 OuterAngle = 0.7f;      // Spot: zero outside this (smooth between the two)
};

// CPU-side lighting helpers (also the formulas the lit sprite shader uses). Unit-tested without
// a GPU; the renderer packs the same numbers into LightingUniforms for the GPU.
namespace LightMath {

// 1 at the light, 0 at and beyond Radius. `falloff` raises the fade (1 = linear in the fade).
[[nodiscard]] inline f32 Attenuation(f32 distance, f32 radius, f32 falloff)
{
    if (radius <= 0.0f)
        return 0.0f;
    const f32 t = Clamp(1.0f - distance / radius, 0.0f, 1.0f);
    return std::pow(t, Max(falloff, 1e-3f));
}

// Smooth 0..1 between cosOuter and cosInner (HLSL's smoothstep). Point lights pass cosOuter = -2
// so this always returns 1.
[[nodiscard]] inline f32 SpotFactor(f32 cosAngle, f32 cosInner, f32 cosOuter)
{
    if (cosOuter <= -1.5f)
        return 1.0f;
    if (cosAngle <= cosOuter)
        return 0.0f;
    if (cosAngle >= cosInner)
        return 1.0f;
    const f32 t = (cosAngle - cosOuter) / (cosInner - cosOuter);
    return t * t * (3.0f - 2.0f * t);
}

// Unit vector from the surface (xy at z = 0) toward the light, and the distance used for falloff.
struct ToLight {
    Vec3 Direction{}; // normalized
    f32 Distance = 0.0f;
};
[[nodiscard]] inline ToLight DirectionTo(const Vec2& surface, const Light& light)
{
    const Vec3 delta{light.Position.x - surface.x, light.Position.y - surface.y, light.Z};
    const f32 length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (length < 1e-5f)
        return {{0.0f, 0.0f, 1.0f}, 0.0f};
    return {delta * (1.0f / length), length};
}

// Lambert diffuse of one light on a surface with world-space normal `normal` (usually from a
// normal map). Returns the RGB to multiply the albedo by (no ambient).
[[nodiscard]] inline Vec3 ShadeLight(const Vec2& surface, const Vec3& normal, const Light& light)
{
    const ToLight to = DirectionTo(surface, light);
    const f32 ndotl = Max(Dot(normal, to.Direction), 0.0f);
    const f32 atten = Attenuation(to.Distance, light.Radius, light.Falloff);
    f32 spot = 1.0f;
    if (light.Kind == LightKind::Spot) {
        // Angle between the light's aim and the vector from the light to the surface.
        const Vec2 fromLight = surface - light.Position;
        const f32 len = Length(fromLight);
        const f32 cosAngle = len > 1e-5f ? Dot(fromLight / len, Normalize(light.Direction)) : 1.0f;
        spot = SpotFactor(cosAngle, std::cos(light.InnerAngle), std::cos(light.OuterAngle));
    }
    return light.Color * (light.Intensity * ndotl * atten * spot);
}

// Ambient plus every light. `normal` should be unit length; a missing normal map is (0, 0, 1).
[[nodiscard]] inline Vec3 Shade(const Vec2& surface, const Vec3& normal, const Vec3& ambient,
                                std::span<const Light> lights)
{
    Vec3 rgb = ambient;
    for (const Light& light : lights)
        rgb += ShadeLight(surface, normal, light);
    return rgb;
}

} // namespace LightMath

// GPU cbuffer of the lit sprite fragment shader (SpriteLit.frag.hlsl). Laid out as float4s so
// HLSL packing matches C++. Ambient in [0], then LightCount in [1].x, then 4 float4s per light.
struct LightingUniforms {
    static constexpr u32 kFloatsPerLight = 16; // 4 float4s
    Vec4 Ambient{};                            // rgb, a unused
    Vec4 Count{};                              // x = light count
    Vec4 Lights[kMaxLights * 4]{};

    // Packs ambient and up to kMaxLights lights (extras dropped).
    [[nodiscard]] static LightingUniforms Make(const Vec3& ambient, std::span<const Light> lights);
};

inline LightingUniforms LightingUniforms::Make(const Vec3& ambient, std::span<const Light> lights)
{
    LightingUniforms u;
    u.Ambient = {ambient.x, ambient.y, ambient.z, 0.0f};
    const u32 count = static_cast<u32>(Min(lights.size(), static_cast<usize>(kMaxLights)));
    u.Count.x = static_cast<f32>(count);
    for (u32 i = 0; i < count; ++i) {
        const Light& L = lights[i];
        Vec4* slot = &u.Lights[i * 4];
        // float4 PosRadius, ColorIntensity, DirCos, FalloffPad
        slot[0] = {L.Position.x, L.Position.y, L.Z, L.Radius};
        slot[1] = {L.Color.x, L.Color.y, L.Color.z, L.Intensity};
        if (L.Kind == LightKind::Spot) {
            const Vec2 dir = Normalize(L.Direction);
            slot[2] = {dir.x, dir.y, std::cos(L.InnerAngle), std::cos(L.OuterAngle)};
        } else {
            slot[2] = {0.0f, 0.0f, 1.0f, -2.0f}; // cosOuter <= -1.5: SpotFactor = 1
        }
        slot[3] = {L.Falloff, 0.0f, 0.0f, 0.0f};
    }
    return u;
}

} // namespace Emerald

#pragma once

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

class Texture;

// A rectangle of a texture, in pixels with (0, 0) at the texture's top-left corner.
struct TextureRegion {
    Vec2 Position{};
    Vec2 Size{};
};

// Something to draw: a texture and the part of it to use (usually from a TextureAtlas). Cheap to
// copy; it only points at the texture, which must outlive it.
struct Sprite {
    const Texture* Source = nullptr;
    TextureRegion Region{};

    // The whole texture.
    [[nodiscard]] static Sprite FromTexture(const Texture& texture);
    // A part of this sprite, e.g. one animation frame. `offset` is relative to the region.
    [[nodiscard]] Sprite Crop(const Vec2& offset, const Vec2& size) const
    {
        return {Source, {Region.Position + offset, size}};
    }
};

// How to place a sprite. Everything is optional:
//
//   r.DrawSprite(ship, position, {.Scale = Vec2(0.5f), .Rotation = angle});
//
// The sprite is scaled, then rotated around its origin, and the origin is put at `position`.
// (Tint, a Vec4, is 16-byte aligned, so the compiler pads the struct around it. That is fine, but
// MSVC reports it at /W4 as warning C4324 in every file that includes this header, so it is
// switched off for this struct only. The member order stays as is: it is the order designated
// initializers must use.)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
struct SpriteOptions {
    // Size in world units; zero = the region's pixel size times Scale.
    Vec2 Size{};
    Vec2 Scale{1.0f, 1.0f};
    // Radians; positive turns clockwise on screen (y-down), like Transform2D.
    f32 Rotation = 0.0f;
    // Pivot for rotation and placement, as a fraction of the size: (0, 0) = top-left corner,
    // (0.5, 0.5) = center, (1, 1) = bottom-right.
    Vec2 Origin{0.5f, 0.5f};
    // Multiplies the texture's color and alpha (e.g. {1, 1, 1, 0.5} = half transparent).
    Vec4 Tint{1.0f, 1.0f, 1.0f, 1.0f};
    bool FlipX = false; // mirror left <-> right
    bool FlipY = false; // mirror top <-> bottom
    // Moves the sprite so its top-left corner (before rotation) lands on a whole unit - with a
    // pixel-space projection, a whole pixel - so unrotated pixel art stays crisp. The pixel grid
    // only matches if the size is whole pixels too.
    bool PixelSnap = false;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace Emerald

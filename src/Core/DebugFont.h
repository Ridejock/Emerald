#pragma once

// Tiny 5x7 text for the profiler overlay when ImGui is off. Scope names can contain quotes and
// lowercase, so this stays separate from the public uppercase font in Renderer/DebugText.h.
// Games that want real text use Font / Renderer2D::DrawString.

#include <string_view>

#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

class Renderer2D;

// Top-left of the first line. `scale` is pixels per font pixel (2 is readable). '\n' breaks lines.
void DrawDebugText(Renderer2D& renderer, const Vec2& position, std::string_view text,
                   const Vec4& color, f32 scale);

[[nodiscard]] f32 DebugTextLineHeight(f32 scale);

} // namespace Emerald

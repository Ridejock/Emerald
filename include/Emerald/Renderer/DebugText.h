#pragma once

// Built-in 5x7 debug text. Each glyph is a few filled rectangles, so there is no font file and
// no atlas: the quads go through Renderer2D's sprite batch. Call DrawDebugText between Begin
// and End. Letters are uppercase only ('a' is drawn as 'A').
//
// Glyphs: A-Z, 0-9, and . , : ; - + / ! ? ' ( ) [ ] > %
// Space advances and draws nothing. '\n' starts a line, '\r' is skipped. Any other byte is a
// hollow box. For real UI text, use Font and DrawString.

#include <string_view>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

class Renderer2D;

inline constexpr i32 kDebugTextColumns = 5;
inline constexpr i32 kDebugTextRows = 7;
inline constexpr i32 kDebugTextAdvance = 6;    // columns, plus a one-column gap
inline constexpr i32 kDebugTextLineHeight = 9; // rows, plus a two-row gap

// Pixels of one line at `scale` (the glyph plus the gap under it). `scale` is pixels per font
// pixel; a non-positive scale is 0.
[[nodiscard]] constexpr f32 DebugTextLineHeight(i32 scale)
{
    return scale > 0 ? static_cast<f32>(kDebugTextLineHeight) * static_cast<f32>(scale) : 0.0f;
}

// Width of the widest line and the height of every line, in the same layout as DrawDebugText
// (pen advance, including the gap after the last glyph of a line). `maxWidth` of 0 does not
// wrap. Empty text is (0, one line). A non-positive scale is (0, 0).
[[nodiscard]] Vec2 MeasureDebugText(std::string_view text, i32 scale = 1, f32 maxWidth = 0.0f);

// True when DrawDebugText draws `code` as itself. Space, newline, and carriage return are
// layout. A letter matches in either case. A byte that draws the hollow box is false.
[[nodiscard]] bool DebugTextHasGlyph(char code);

// Top-left of the first line. `maxWidth` of 0 draws one line per '\n' and does not wrap; a
// positive width wraps on spaces before a word that would pass the right edge. A word longer
// than the width stays on one line. Returns the y just below the last line (`position.y` when
// `scale` is not positive).
//
//   DrawDebugText(r, {8.0f, 8.0f}, "SCORE 12", {1.0f, 1.0f, 1.0f, 1.0f}, 2);
f32 DrawDebugText(Renderer2D& renderer, const Vec2& position, std::string_view text,
                  const Vec4& color, i32 scale = 1, f32 maxWidth = 0.0f);

} // namespace Emerald

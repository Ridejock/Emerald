#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/Texture.h"

namespace Emerald {

// A block of Unicode codepoints to bake: First, First + 1, ..., First + Count - 1.
struct GlyphRange {
    u32 First = 0;
    u32 Count = 0;
};
inline constexpr GlyphRange kAsciiGlyphs{32, 95};   // ' ' .. '~'
inline constexpr GlyphRange kLatin1Glyphs{160, 96}; // U+00A0 .. U+00FF (e.g. e with accents)

enum class TextAlign : u8 {
    Left,   // the position is the left edge of every line
    Center, // ... the center of every line
    Right,  // ... the right edge of every line
};

// How to bake a font. Everything is optional:
//
//   Font::Load(device, path, {.Size = 16.0f, .Oversample = 1, .Filter = TextureFilter::Nearest});
struct FontOptions {
    // Em size in pixels, like a CSS font-size. Pixel fonts stay crisp at multiples of their grid
    // (Press Start 2P: 8, 16, 24, ...).
    f32 Size = 32.0f;
    // Codepoints to bake. Characters outside them are drawn as '?' (if baked) or skipped.
    std::vector<GlyphRange> Ranges{kAsciiGlyphs};
    // Glyphs are rendered this many times larger horizontally and vertically, then filtered down
    // by the GPU: smoother edges and sub-pixel placement for Linear fonts. Use 1 for pixel fonts.
    u32 Oversample = 2;
    // Linear for smooth fonts; Nearest keeps pixel fonts sharp (and snaps glyphs to whole units).
    TextureFilter Filter = TextureFilter::Linear;
};

// One baked character. Offsets and sizes are in output pixels at the baked size.
struct Glyph {
    TextureRegion Region{}; // where it is in the atlas (oversampled, so maybe larger than Size)
    Vec2 Offset{};          // quad top-left relative to the pen position on the baseline
    Vec2 Size{};            // quad size
    f32 Advance = 0.0f;     // how far the pen moves after it
    i32 Index = 0;          // the font's glyph index (for kerning)
};

// A glyph placed by Font::LayoutText: its quad's top-left relative to the text position, unscaled.
struct PlacedGlyph {
    const Glyph* Source = nullptr;
    Vec2 Position{};
};

// A TrueType/OpenType font baked at one size into a texture atlas with stb_truetype:
//
//   std::optional<Font> font = Font::Load(device, Paths::GetBasePath() / "assets/Inter.ttf",
//                                         {.Size = 24.0f});
//   r.DrawString(*font, "Hello\nworld", {20, 20}, {1, 1, 1, 1});
//
// Load one Font per size you need; each has its own atlas texture (one draw call per font and
// run of text). Text is UTF-8; '\n' starts a new line. Positions are the top-left of the first
// line (its ascent line) for TextAlign::Left, the top center or top right for the others. Draw
// and layout scale uniformly by `scale`. The font keeps the file's bytes for kerning lookups.
// Move-only; the texture must outlive the frames that draw it (like any Texture).
class Font {
public:
    Font();
    ~Font();
    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // `device` may be null: everything but drawing works (tests, tools). Returns nullopt on
    // failure (logged).
    [[nodiscard]] static std::optional<Font>
    Load(SDL_GPUDevice* device, const std::filesystem::path& path, const FontOptions& options = {});
    // From a .ttf/.otf file in memory (copied).
    [[nodiscard]] static std::optional<Font> LoadFromMemory(SDL_GPUDevice* device,
                                                            std::span<const u8> data,
                                                            const FontOptions& options = {});

    // A stand-in for a font that could not be loaded (the asset manager uses it): every
    // character in options.Ranges is a hollow box ("tofu"), so text still shows up, clearly
    // wrong, and measures about like real text at options.Size. `device` may be null.
    [[nodiscard]] static Font CreatePlaceholder(SDL_GPUDevice* device, const FontOptions& options);

    // --- Metrics, in pixels at the baked size (times `scale` where given) ---
    [[nodiscard]] f32 GetSize() const { return m_Size; }
    // Baseline to the top of the tallest letters (positive).
    [[nodiscard]] f32 GetAscent() const { return m_Ascent; }
    // Baseline to the bottom of descenders like 'g' (negative: below the baseline).
    [[nodiscard]] f32 GetDescent() const { return m_Descent; }
    // Baseline to baseline: ascent - descent + the font's line gap.
    [[nodiscard]] f32 GetLineHeight() const { return m_LineHeight; }
    // Width of the widest line (pen advances + kerning) and height of all lines: the last line
    // counts ascent to descent, the others a full line height. Empty text measures (0, 0).
    [[nodiscard]] Vec2 MeasureText(std::string_view text, f32 scale = 1.0f) const;
    // Extra space between two characters (usually negative, e.g. "AV"); 0 if not in the font.
    [[nodiscard]] f32 GetKerning(u32 left, u32 right) const;

    // The baked glyph for a codepoint, or nullptr.
    [[nodiscard]] const Glyph* FindGlyph(u32 codepoint) const;
    // Places every visible glyph of `text` (appended to `out`), aligned like DrawString. Renderer2D
    // uses this; it is public for custom text effects and tests.
    void LayoutText(std::string_view text, TextAlign align, std::vector<PlacedGlyph>& out) const;

    [[nodiscard]] const Texture& GetTexture() const { return *m_Texture; }
    // Nearest-filtered fonts are drawn snapped to whole units to keep their pixels sharp.
    [[nodiscard]] bool IsPixelFont() const { return m_Filter == TextureFilter::Nearest; }

    // Next codepoint of UTF-8 `text` at `pos` (advanced past it). Invalid bytes give U+FFFD.
    [[nodiscard]] static u32 DecodeUtf8(std::string_view text, usize& pos);

private:
    struct Face; // stb_truetype state (kept for kerning)

    // Pen advance of one line (no '\n' inside), including kerning.
    [[nodiscard]] f32 MeasureLine(std::string_view line) const;
    // Kerning between two glyphs in pixels (GPOS or kern table).
    [[nodiscard]] f32 Kern(const Glyph& left, const Glyph& right) const;
    // The glyph to draw for `codepoint`: itself, or the '?' fallback.
    [[nodiscard]] const Glyph* ResolveGlyph(u32 codepoint) const;

    std::unique_ptr<Face> m_Face;
    std::unique_ptr<Texture> m_Texture; // heap: stays put when the Font moves
    std::unordered_map<u32, Glyph> m_Glyphs;
    TextureFilter m_Filter = TextureFilter::Linear;
    f32 m_Size = 0.0f;
    f32 m_Scale = 0.0f; // font units -> pixels
    f32 m_Ascent = 0.0f;
    f32 m_Descent = 0.0f;
    f32 m_LineHeight = 0.0f;
};

} // namespace Emerald

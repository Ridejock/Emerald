// Font loading, metrics, measuring, layout and Renderer2D::DrawText, with the sandbox's Press
// Start 2P (a monospace pixel font: every glyph advances exactly one em, no kerning). No GPU is
// needed: fonts are loaded without a device, so their atlas is a CreateWithoutGpu texture.

#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include <Emerald/Core/Log.h>
#include <Emerald/Renderer/Font.h>
#include <Emerald/Renderer/Renderer2D.h>

#include "Test.h"

using namespace Emerald;

namespace {

// The pixel font at 16 px: one glyph = 16 px wide, ascent 16, descent 0.
std::optional<Font> LoadPixelFont(f32 size = 16.0f)
{
    Log::Init({}); // the loader logs (safe to call more than once)
    return Font::Load(nullptr, EMERALD_TEST_FONT,
                      {.Size = size,
                       .Ranges = {kAsciiGlyphs, kLatin1Glyphs},
                       .Oversample = 1,
                       .Filter = TextureFilter::Nearest});
}

} // namespace

TEST(FontLoadsAndReportsMetrics)
{
    const std::optional<Font> font = LoadPixelFont();
    CHECK(font.has_value());
    if (!font)
        return;
    CHECK(font->GetSize() == 16.0f);
    CHECK_NEAR(font->GetAscent(), 16.0f);
    CHECK_NEAR(font->GetDescent(), 0.0f);
    CHECK_NEAR(font->GetLineHeight(), 16.0f);
    CHECK(font->IsPixelFont());
    CHECK(font->GetTexture().GetWidth() > 0);

    const Glyph* a = font->FindGlyph('A');
    CHECK(a != nullptr);
    if (a) {
        CHECK_NEAR(a->Advance, 16.0f);
        CHECK(a->Offset.y < 0.0f); // drawn above the baseline
        CHECK(a->Region.Size.x > 0.0f);
    }
    CHECK(font->FindGlyph(0xE9) != nullptr);   // 'e acute' from the Latin-1 range
    CHECK(font->FindGlyph(0x263A) == nullptr); // not baked
    CHECK(font->GetKerning('A', 'V') == 0.0f); // monospace: no kerning pairs
}

TEST(FontMeasuresKnownStrings)
{
    const std::optional<Font> font = LoadPixelFont();
    if (!font)
        return;
    CHECK_NEAR(font->MeasureText("HELLO"), Vec2(80.0f, 16.0f));
    CHECK_NEAR(font->MeasureText("A B"), Vec2(48.0f, 16.0f)); // the space advances too
    CHECK_NEAR(font->MeasureText("HELLO", 2.0f), Vec2(160.0f, 32.0f));
    CHECK_NEAR(font->MeasureText(""), Vec2(0.0f, 0.0f));
    // Widest line; two lines = ascent-to-descent + one line height.
    CHECK_NEAR(font->MeasureText("AB\nABCD"), Vec2(64.0f, 32.0f));
    CHECK_NEAR(font->MeasureText("A\n"), Vec2(16.0f, 32.0f)); // a trailing newline adds a line
    // UTF-8: two bytes, one glyph; unknown characters use the '?' glyph's width.
    CHECK_NEAR(font->MeasureText("caf\xC3\xA9"), Vec2(64.0f, 16.0f));
    CHECK_NEAR(font->MeasureText("\xE2\x98\xBA!"), Vec2(32.0f, 16.0f));
}

TEST(FontSizesAreIndependent)
{
    const std::optional<Font> small = LoadPixelFont(8.0f);
    const std::optional<Font> big = LoadPixelFont(32.0f);
    if (!small || !big)
        return;
    CHECK_NEAR(small->MeasureText("ROCK"), Vec2(32.0f, 8.0f));
    CHECK_NEAR(big->MeasureText("ROCK"), Vec2(128.0f, 32.0f));
    CHECK(small->GetTexture().GetId() != big->GetTexture().GetId());
}

TEST(FontOversamplingBakesLargerGlyphs)
{
    const std::optional<Font> font =
        Font::Load(nullptr, EMERALD_TEST_FONT, {.Size = 16.0f, .Oversample = 3});
    if (!font)
        return;
    CHECK(!font->IsPixelFont()); // Linear by default
    const Glyph* a = font->FindGlyph('A');
    CHECK(a != nullptr);
    if (a) {
        CHECK_NEAR(a->Advance, 16.0f);              // metrics stay in output pixels
        CHECK(a->Region.Size.x > 2.5f * a->Size.x); // ... the bitmap is three times as detailed
    }
    CHECK_NEAR(font->MeasureText("HELLO"), Vec2(80.0f, 16.0f));
}

TEST(FontLayoutAligns)
{
    const std::optional<Font> font = LoadPixelFont();
    if (!font)
        return;
    const f32 offsetX = font->FindGlyph('A')->Offset.x;
    const auto firstX = [&](std::string_view text, TextAlign align) {
        std::vector<PlacedGlyph> glyphs;
        font->LayoutText(text, align, glyphs);
        return glyphs.empty() ? -999.0f : glyphs[0].Position.x;
    };
    CHECK_NEAR(firstX("AB", TextAlign::Left), offsetX);
    CHECK_NEAR(firstX("AB", TextAlign::Center), offsetX - 16.0f);
    CHECK_NEAR(firstX("AB", TextAlign::Right), offsetX - 32.0f);

    // Two lines, each centered on its own; the second is one line height lower. Spaces and
    // control characters get no quad.
    std::vector<PlacedGlyph> glyphs;
    font->LayoutText("A B\r\nABCD", TextAlign::Center, glyphs);
    CHECK(glyphs.size() == 6);
    if (glyphs.size() == 6) {
        CHECK_NEAR(glyphs[0].Position.x, offsetX - 24.0f);
        CHECK_NEAR(glyphs[1].Position.x, offsetX + 8.0f);
        CHECK_NEAR(glyphs[2].Position.x, offsetX - 32.0f);
        CHECK_NEAR(glyphs[2].Position.y - glyphs[0].Position.y, 16.0f);
        CHECK_NEAR(glyphs[0].Position.y, 16.0f + font->FindGlyph('A')->Offset.y);
    }
}

TEST(FontDecodesUtf8)
{
    const auto decode = [](std::string_view text) {
        std::vector<u32> out;
        for (usize pos = 0; pos < text.size();)
            out.push_back(Font::DecodeUtf8(text, pos));
        return out;
    };
    CHECK(decode("Az") == std::vector<u32>({'A', 'z'}));
    CHECK(decode("\xC3\xA9") == std::vector<u32>({0xE9}));            // e acute
    CHECK(decode("\xE2\x82\xAC") == std::vector<u32>({0x20AC}));      // euro sign
    CHECK(decode("\xF0\x9F\x98\x80") == std::vector<u32>({0x1F600})); // emoji
    CHECK(decode("\xFF"
                 "A") == std::vector<u32>({0xFFFD, 'A'})); // invalid byte
    CHECK(decode("\xC3"
                 "A") == std::vector<u32>({0xFFFD, 'A'})); // truncated
}

TEST(FontRejectsBadInput)
{
    Log::Init({});
    const u8 garbage[] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!Font::LoadFromMemory(nullptr, garbage).has_value());
    CHECK(!Font::Load(nullptr, "does-not-exist.ttf").has_value());
    CHECK(!Font::Load(nullptr, EMERALD_TEST_FONT, {.Size = 0.0f}).has_value());
}

TEST(Renderer2DDrawsText)
{
    const std::optional<Font> font = LoadPixelFont();
    if (!font)
        return;
    Renderer2D r;
    r.Begin(Mat4::OrthoPixelSpace(640.0f, 360.0f));
    r.DrawText(*font, "HI there", {10.3f, 20.0f}, {1.0f, 0.5f, 0.0f, 1.0f}, 2.0f);
    r.End();

    CHECK(r.GetSpriteCount() == 7);     // the space has no quad
    CHECK(r.GetCommands().size() == 1); // one texture: one draw call
    const auto vertices = r.GetSpriteVertices();
    if (vertices.size() >= 6) {
        // 'H' at 2x: pixel font, so its top-left is snapped to a whole unit.
        const Glyph* h = font->FindGlyph('H');
        const Vec2 expected(std::round(10.3f + 2.0f * h->Offset.x),
                            std::round(20.0f + 2.0f * (16.0f + h->Offset.y)));
        CHECK_NEAR(vertices[0].Position, expected);
        CHECK_NEAR(vertices[2].Position, expected + h->Size * 2.0f); // bottom-right corner
        CHECK(vertices[0].Color == Renderer2D::PackColor({1.0f, 0.5f, 0.0f, 1.0f}));
    }
}

#include "Emerald/Renderer/Font.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"
#include "Emerald/Math/Common.h"

namespace Emerald {

// The font file's bytes and stb's view of them (which points into Data, so both live here).
struct Font::Face {
    std::vector<u8> Data;
    stbtt_fontinfo Info{};
};

namespace {

constexpr u32 kFallback = '?';
constexpr u32 kReplacement = 0xFFFD; // what DecodeUtf8 returns for invalid bytes
constexpr i32 kPadding = 1;          // empty pixels around each glyph (no bleeding when filtered)
constexpr i32 kMaxAtlasSide = 4096;

// A square power-of-two side that probably fits all glyphs; packing retries larger if not.
i32 EstimateAtlasSide(const FontOptions& options, u32 glyphCount)
{
    const f32 cell =
        options.Size * static_cast<f32>(options.Oversample) + 2.0f * static_cast<f32>(kPadding);
    const f32 area = cell * cell * static_cast<f32>(glyphCount) * 0.7f; // glyphs are not squares
    i32 side = 64;
    while (side < kMaxAtlasSide && static_cast<f32>(side) * static_cast<f32>(side) < area)
        side *= 2;
    return side;
}

// Packs the ranges into a `side` x `side` coverage bitmap. Returns false if they did not fit.
bool PackGlyphs(const std::vector<u8>& data, const FontOptions& options, i32 side,
                std::vector<u8>& bitmap, std::vector<std::vector<stbtt_packedchar>>& packed)
{
    bitmap.assign(static_cast<usize>(side) * static_cast<usize>(side), 0);
    stbtt_pack_context context{};
    if (!stbtt_PackBegin(&context, bitmap.data(), side, side, 0, kPadding, nullptr))
        return false;
    stbtt_PackSetOversampling(&context, options.Oversample, options.Oversample);

    std::vector<stbtt_pack_range> ranges;
    packed.assign(options.Ranges.size(), {});
    for (usize i = 0; i < options.Ranges.size(); ++i) {
        packed[i].resize(options.Ranges[i].Count);
        stbtt_pack_range range{};
        range.font_size = STBTT_POINT_SIZE(options.Size); // em size, not ascent-to-descent
        range.first_unicode_codepoint_in_range = static_cast<int>(options.Ranges[i].First);
        range.num_chars = static_cast<int>(options.Ranges[i].Count);
        range.chardata_for_range = packed[i].data();
        ranges.push_back(range);
    }
    const int ok = stbtt_PackFontRanges(&context, data.data(), 0, ranges.data(),
                                        static_cast<int>(ranges.size()));
    stbtt_PackEnd(&context);
    return ok != 0;
}

// The requested ranges split into runs of codepoints the font has. stb reports a failed pack
// for a missing codepoint (its rectangle is empty), so those are left out before packing.
std::vector<GlyphRange> ExistingRuns(const stbtt_fontinfo& info,
                                     const std::vector<GlyphRange>& ranges)
{
    std::vector<GlyphRange> runs;
    for (const GlyphRange& range : ranges) {
        for (u32 i = 0; i < range.Count; ++i) {
            const u32 codepoint = range.First + i;
            if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0)
                continue;
            if (!runs.empty() && runs.back().First + runs.back().Count == codepoint)
                ++runs.back().Count;
            else
                runs.push_back({codepoint, 1});
        }
    }
    return runs;
}

} // namespace

Font::Font() = default;
Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

std::optional<Font> Font::Load(SDL_GPUDevice* device, const std::filesystem::path& path,
                               const FontOptions& options)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate); // opened at the end: its size
    if (!file) {
        EM_CORE_ERROR("Failed to open font '{}'", path.string());
        return std::nullopt;
    }
    std::vector<u8> data(static_cast<usize>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    std::optional<Font> font = LoadFromMemory(device, data, options);
    if (font)
        EM_CORE_INFO("Loaded font '{}' at {} px ({} glyphs, {}x{} atlas)", path.string(),
                     options.Size, font->m_Glyphs.size(), font->GetTexture().GetWidth(),
                     font->GetTexture().GetHeight());
    return font;
}

std::optional<Font> Font::LoadFromMemory(SDL_GPUDevice* device, std::span<const u8> data,
                                         const FontOptions& options)
{
    Font font;
    font.m_Face = std::make_unique<Face>();
    Face& face = *font.m_Face;
    face.Data.assign(data.begin(), data.end());
    const int offset = face.Data.empty() ? -1 : stbtt_GetFontOffsetForIndex(face.Data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&face.Info, face.Data.data(), offset)) {
        EM_CORE_ERROR("Font data is not a TrueType/OpenType font");
        return std::nullopt;
    }
    if (options.Size <= 0.0f || options.Ranges.empty()) {
        EM_CORE_ERROR("Font needs a positive size and at least one glyph range");
        return std::nullopt;
    }
    FontOptions bake = options;
    bake.Oversample = Clamp(options.Oversample, 1u, 8u); // stb's limit
    bake.Ranges = ExistingRuns(face.Info, options.Ranges);
    if (bake.Ranges.empty()) {
        EM_CORE_ERROR("The font has none of the requested glyphs");
        return std::nullopt;
    }

    // Vertical metrics, font units -> pixels at the em size.
    font.m_Size = bake.Size;
    font.m_Filter = bake.Filter;
    font.m_Scale = stbtt_ScaleForMappingEmToPixels(&face.Info, bake.Size);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&face.Info, &ascent, &descent, &lineGap);
    font.m_Ascent = static_cast<f32>(ascent) * font.m_Scale;
    font.m_Descent = static_cast<f32>(descent) * font.m_Scale;
    font.m_LineHeight = static_cast<f32>(ascent - descent + lineGap) * font.m_Scale;

    // Bake, growing the atlas until everything fits.
    u32 glyphCount = 0;
    for (const GlyphRange& range : bake.Ranges)
        glyphCount += range.Count;
    std::vector<u8> coverage;
    std::vector<std::vector<stbtt_packedchar>> packed;
    i32 side = EstimateAtlasSide(bake, glyphCount);
    while (!PackGlyphs(face.Data, bake, side, coverage, packed)) {
        if (side >= kMaxAtlasSide) {
            EM_CORE_ERROR("Font glyphs do not fit a {0}x{0} atlas; use a smaller size or fewer "
                          "ranges",
                          kMaxAtlasSide);
            return std::nullopt;
        }
        side *= 2;
    }

    // Every baked codepoint exists in the font (ExistingRuns).
    for (usize r = 0; r < bake.Ranges.size(); ++r) {
        for (u32 i = 0; i < bake.Ranges[r].Count; ++i) {
            const u32 codepoint = bake.Ranges[r].First + i;
            const int index = stbtt_FindGlyphIndex(&face.Info, static_cast<int>(codepoint));
            const stbtt_packedchar& c = packed[r][i];
            font.m_Glyphs[codepoint] = {
                .Region = {{static_cast<f32>(c.x0), static_cast<f32>(c.y0)},
                           {static_cast<f32>(c.x1 - c.x0), static_cast<f32>(c.y1 - c.y0)}},
                .Offset = {c.xoff, c.yoff},
                .Size = {c.xoff2 - c.xoff, c.yoff2 - c.yoff},
                .Advance = c.xadvance,
                .Index = index,
            };
        }
    }

    // White RGBA with the coverage as alpha: tinting then sets the text color, and linear
    // filtering has no dark fringes.
    Image image;
    image.Width = side;
    image.Height = side;
    image.Pixels.resize(coverage.size() * 4);
    for (usize i = 0; i < coverage.size(); ++i) {
        image.Pixels[i * 4 + 0] = 255;
        image.Pixels[i * 4 + 1] = 255;
        image.Pixels[i * 4 + 2] = 255;
        image.Pixels[i * 4 + 3] = coverage[i];
    }
    if (device) {
        std::optional<Texture> texture = Texture::Create(device, image, {.Filter = bake.Filter});
        if (!texture)
            return std::nullopt;
        font.m_Texture = std::make_unique<Texture>(std::move(*texture));
    } else {
        font.m_Texture = std::make_unique<Texture>(
            Texture::CreateWithoutGpu(static_cast<u32>(side), static_cast<u32>(side)));
    }
    return font;
}

Font Font::CreatePlaceholder(SDL_GPUDevice* device, const FontOptions& options)
{
    // An 8 x 8 white outline; every glyph uses all of it, stretched to the box size.
    constexpr i32 kSide = 8;
    Image image;
    image.Width = kSide;
    image.Height = kSide;
    image.Pixels.assign(static_cast<usize>(kSide * kSide) * 4, 255);
    for (i32 y = 1; y < kSide - 1; ++y)
        for (i32 x = 1; x < kSide - 1; ++x)
            image.Pixels[static_cast<usize>(y * kSide + x) * 4 + 3] = 0; // transparent inside

    Font font;
    const f32 size = options.Size > 0.0f ? options.Size : 16.0f;
    font.m_Size = size;
    font.m_Filter = TextureFilter::Nearest;
    font.m_Ascent = 0.8f * size;
    font.m_Descent = -0.2f * size;
    font.m_LineHeight = 1.2f * size;
    const Glyph box{.Region = {{0.0f, 0.0f}, {static_cast<f32>(kSide), static_cast<f32>(kSide)}},
                    .Offset = {0.05f * size, -0.7f * size},
                    .Size = {0.5f * size, 0.7f * size},
                    .Advance = 0.6f * size,
                    .Index = 0};
    for (const GlyphRange& range : options.Ranges)
        for (u32 i = 0; i < range.Count; ++i)
            font.m_Glyphs[range.First + i] = box;
    font.m_Glyphs[' '] = {
        .Region = {}, .Offset = {}, .Size = {}, .Advance = box.Advance, .Index = 0};

    std::optional<Texture> texture;
    if (device)
        texture = Texture::Create(device, image, {.Filter = TextureFilter::Nearest});
    font.m_Texture = std::make_unique<Texture>(texture ? std::move(*texture)
                                                       : Texture::CreateWithoutGpu(kSide, kSide));
    return font;
}

const Glyph* Font::FindGlyph(u32 codepoint) const
{
    const auto it = m_Glyphs.find(codepoint);
    return it != m_Glyphs.end() ? &it->second : nullptr;
}

const Glyph* Font::ResolveGlyph(u32 codepoint) const
{
    if (const Glyph* glyph = FindGlyph(codepoint))
        return glyph;
    return FindGlyph(kFallback);
}

f32 Font::GetKerning(u32 left, u32 right) const
{
    const Glyph* a = FindGlyph(left);
    const Glyph* b = FindGlyph(right);
    return a && b ? Kern(*a, *b) : 0.0f;
}

f32 Font::Kern(const Glyph& left, const Glyph& right) const
{
    if (!m_Face)
        return 0.0f;
    const int units = stbtt_GetGlyphKernAdvance(&m_Face->Info, left.Index, right.Index);
    return static_cast<f32>(units) * m_Scale;
}

u32 Font::DecodeUtf8(std::string_view text, usize& pos)
{
    const auto byte = [&](usize i) { return static_cast<u8>(text[i]); };
    const u8 lead = byte(pos++);
    if (lead < 0x80)
        return lead;
    // Lead byte: how many continuation bytes follow, and the bits it contributes.
    u32 extra = 0;
    u32 codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
        extra = 1;
        codepoint = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        extra = 2;
        codepoint = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        extra = 3;
        codepoint = lead & 0x07u;
    } else {
        return kReplacement; // a stray continuation byte or an invalid lead
    }
    for (u32 i = 0; i < extra; ++i) {
        if (pos >= text.size() || (byte(pos) & 0xC0) != 0x80)
            return kReplacement; // truncated; the next byte is read as a new character
        codepoint = codepoint << 6 | (byte(pos++) & 0x3Fu);
    }
    return codepoint;
}

f32 Font::MeasureLine(std::string_view line) const
{
    f32 pen = 0.0f;
    const Glyph* previous = nullptr;
    for (usize pos = 0; pos < line.size();) {
        const u32 codepoint = DecodeUtf8(line, pos);
        const Glyph* glyph = codepoint < 32 ? nullptr : ResolveGlyph(codepoint);
        if (!glyph)
            continue;
        if (previous)
            pen += Kern(*previous, *glyph);
        pen += glyph->Advance;
        previous = glyph;
    }
    return pen;
}

Vec2 Font::MeasureText(std::string_view text, f32 scale) const
{
    if (text.empty())
        return {};
    f32 width = 0.0f;
    u32 lines = 0;
    for (usize start = 0; start <= text.size(); ++lines) {
        const usize end = std::min(text.find('\n', start), text.size());
        width = Max(width, MeasureLine(text.substr(start, end - start)));
        start = end + 1;
    }
    const f32 height = m_Ascent - m_Descent + static_cast<f32>(lines - 1) * m_LineHeight;
    return Vec2(width, height) * scale;
}

void Font::LayoutText(std::string_view text, TextAlign align, std::vector<PlacedGlyph>& out) const
{
    f32 baseline = m_Ascent; // the text position is the top of the first line
    for (usize start = 0; start <= text.size(); baseline += m_LineHeight) {
        const usize end = std::min(text.find('\n', start), text.size());
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;

        const f32 width = align == TextAlign::Left ? 0.0f : MeasureLine(line);
        f32 pen = align == TextAlign::Left     ? 0.0f
                  : align == TextAlign::Center ? -0.5f * width
                                               : -width;
        const Glyph* previous = nullptr;
        for (usize pos = 0; pos < line.size();) {
            const u32 codepoint = DecodeUtf8(line, pos);
            const Glyph* glyph = codepoint < 32 ? nullptr : ResolveGlyph(codepoint);
            if (!glyph)
                continue; // control characters, or nothing to draw them with
            if (previous)
                pen += Kern(*previous, *glyph);
            if (glyph->Size.x > 0.0f && glyph->Size.y > 0.0f) // spaces have no quad
                out.push_back({glyph, Vec2(pen, baseline) + glyph->Offset});
            pen += glyph->Advance;
            previous = glyph;
        }
    }
}

} // namespace Emerald

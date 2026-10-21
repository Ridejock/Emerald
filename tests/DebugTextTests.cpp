// Built-in 5x7 debug text: measuring, wrapping, uppercase, and the rectangle each glyph covers.
// No GPU and no font file: DrawDebugText records FillRect sprites on the CPU.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <Emerald/Renderer/DebugText.h>
#include <Emerald/Renderer/Renderer2D.h>

#include "Test.h"

using namespace Emerald;

namespace {

const Vec4 kInk{0.2f, 0.5f, 0.9f, 1.0f};

struct GlyphSpec {
    char Code;
    const char* Rows; // 5 columns x 7 rows, '#' is ink
};

// The rectangle placeholder set. A mismatch here is a changed glyph, not a layout quirk.
const GlyphSpec kGlyphs[] = {
    {'A',
     ".###."
     "#...#"
     "#...#"
     "#####"
     "#...#"
     "#...#"
     "#...#"},
    {'B',
     "####."
     "#...#"
     "#...#"
     "####."
     "#...#"
     "#...#"
     "####."},
    {'C',
     ".###."
     "#...#"
     "#...."
     "#...."
     "#...."
     "#...#"
     ".###."},
    {'D',
     "####."
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "####."},
    {'E',
     "#####"
     "#...."
     "#...."
     "####."
     "#...."
     "#...."
     "#####"},
    {'F',
     "#####"
     "#...."
     "#...."
     "####."
     "#...."
     "#...."
     "#...."},
    {'G',
     ".###."
     "#...#"
     "#...."
     "#.###"
     "#...#"
     "#...#"
     ".####"},
    {'H',
     "#...#"
     "#...#"
     "#...#"
     "#####"
     "#...#"
     "#...#"
     "#...#"},
    {'I',
     ".###."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     ".###."},
    {'J',
     "..###"
     "...#."
     "...#."
     "...#."
     "#..#."
     "#..#."
     ".##.."},
    {'K',
     "#...#"
     "#..#."
     "#.#.."
     "##..."
     "#.#.."
     "#..#."
     "#...#"},
    {'L',
     "#...."
     "#...."
     "#...."
     "#...."
     "#...."
     "#...."
     "#####"},
    {'M',
     "#...#"
     "##.##"
     "#.#.#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"},
    {'N',
     "#...#"
     "##..#"
     "#.#.#"
     "#..##"
     "#...#"
     "#...#"
     "#...#"},
    {'O',
     ".###."
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     ".###."},
    {'P',
     "####."
     "#...#"
     "#...#"
     "####."
     "#...."
     "#...."
     "#...."},
    {'Q',
     ".###."
     "#...#"
     "#...#"
     "#...#"
     "#.#.#"
     "#..#."
     ".##.#"},
    {'R',
     "####."
     "#...#"
     "#...#"
     "####."
     "#.#.."
     "#..#."
     "#...#"},
    {'S',
     ".####"
     "#...."
     "#...."
     ".###."
     "....#"
     "....#"
     "####."},
    {'T',
     "#####"
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     "..#.."},
    {'U',
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     ".###."},
    {'V',
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     "#...#"
     ".#.#."
     "..#.."},
    {'W',
     "#...#"
     "#...#"
     "#...#"
     "#.#.#"
     "#.#.#"
     "#.#.#"
     ".#.#."},
    {'X',
     "#...#"
     "#...#"
     ".#.#."
     "..#.."
     ".#.#."
     "#...#"
     "#...#"},
    {'Y',
     "#...#"
     "#...#"
     ".#.#."
     "..#.."
     "..#.."
     "..#.."
     "..#.."},
    {'Z',
     "#####"
     "....#"
     "...#."
     "..#.."
     ".#..."
     "#...."
     "#####"},
    {'0',
     ".###."
     "#...#"
     "#..##"
     "#.#.#"
     "##..#"
     "#...#"
     ".###."},
    {'1',
     "..#.."
     ".##.."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     ".###."},
    {'2',
     ".###."
     "#...#"
     "....#"
     "...#."
     "..#.."
     ".#..."
     "#####"},
    {'3',
     ".###."
     "#...#"
     "....#"
     "..##."
     "....#"
     "#...#"
     ".###."},
    {'4',
     "...#."
     "..##."
     ".#.#."
     "#..#."
     "#####"
     "...#."
     "...#."},
    {'5',
     "#####"
     "#...."
     "#...."
     "####."
     "....#"
     "....#"
     "####."},
    {'6',
     ".###."
     "#...."
     "#...."
     "####."
     "#...#"
     "#...#"
     ".###."},
    {'7',
     "#####"
     "....#"
     "...#."
     "..#.."
     ".#..."
     ".#..."
     ".#..."},
    {'8',
     ".###."
     "#...#"
     "#...#"
     ".###."
     "#...#"
     "#...#"
     ".###."},
    {'9',
     ".###."
     "#...#"
     "#...#"
     ".####"
     "....#"
     "....#"
     ".###."},
    {'.',
     "....."
     "....."
     "....."
     "....."
     "....."
     "..#.."
     "..#.."},
    {',',
     "....."
     "....."
     "....."
     "....."
     "..#.."
     "..#.."
     ".#..."},
    {':',
     "....."
     "..#.."
     "..#.."
     "....."
     "..#.."
     "..#.."
     "....."},
    {'-',
     "....."
     "....."
     "....."
     "#####"
     "....."
     "....."
     "....."},
    {'+',
     "....."
     "..#.."
     "..#.."
     "#####"
     "..#.."
     "..#.."
     "....."},
    {'/',
     "....#"
     "...#."
     "...#."
     "..#.."
     ".#..."
     ".#..."
     "#...."},
    {'!',
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     "....."
     "..#.."
     "....."},
    {'?',
     ".###."
     "#...#"
     "....#"
     "...#."
     "..#.."
     "....."
     "..#.."},
    {'\'',
     "..#.."
     "..#.."
     "....."
     "....."
     "....."
     "....."
     "....."},
    {'(',
     "..#.."
     ".#..."
     ".#..."
     ".#..."
     ".#..."
     ".#..."
     "..#.."},
    {')',
     ".#..."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     "..#.."
     ".#..."},
    {';',
     "....."
     "..#.."
     "..#.."
     "....."
     "..#.."
     "..#.."
     ".#..."},
    {'[',
     ".##.."
     ".#..."
     ".#..."
     ".#..."
     ".#..."
     ".#..."
     ".##.."},
    {']',
     "..##."
     "...#."
     "...#."
     "...#."
     "...#."
     "...#."
     "..##."},
    {'>',
     ".#..."
     "..#.."
     "...#."
     "....#"
     "...#."
     "..#.."
     ".#..."},
    {'%',
     "##..#"
     "##.#."
     "...#."
     "..#.."
     ".#..."
     ".#.##"
     "#..##"},
};

const char* kBox = "#####"
                   "#...#"
                   "#...#"
                   "#...#"
                   "#...#"
                   "#...#"
                   "#####";

struct Rect {
    f32 X = 0.0f;
    f32 Y = 0.0f;
    f32 W = 0.0f;
    f32 H = 0.0f;
};

// Horizontal runs, the same way DrawDebugText turns a row of '#' into one FillRect.
std::vector<Rect> Runs(const char* rows, const Vec2& origin, i32 scale)
{
    std::vector<Rect> rects;
    const f32 pixel = static_cast<f32>(scale);
    for (int row = 0; row < kDebugTextRows; ++row) {
        int col = 0;
        while (col < kDebugTextColumns) {
            if (rows[row * kDebugTextColumns + col] != '#') {
                ++col;
                continue;
            }
            const int start = col;
            while (col < kDebugTextColumns && rows[row * kDebugTextColumns + col] == '#')
                ++col;
            rects.push_back({origin.x + static_cast<f32>(start) * pixel,
                             origin.y + static_cast<f32>(row) * pixel,
                             static_cast<f32>(col - start) * pixel, pixel});
        }
    }
    return rects;
}

std::vector<Rect> Drawn(Renderer2D& renderer)
{
    std::vector<Rect> rects;
    const auto vertices = renderer.GetSpriteVertices();
    for (usize i = 0; i + 5 < vertices.size(); i += 6) {
        const Vec2 topLeft = vertices[i].Position;
        const Vec2 bottomRight = vertices[i + 2].Position;
        rects.push_back(
            {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y});
    }
    return rects;
}

void CheckRects(const std::vector<Rect>& got, const std::vector<Rect>& expected)
{
    CHECK(got.size() == expected.size());
    if (got.size() != expected.size())
        return;
    for (usize i = 0; i < got.size(); ++i) {
        CHECK_NEAR(got[i].X, expected[i].X);
        CHECK_NEAR(got[i].Y, expected[i].Y);
        CHECK_NEAR(got[i].W, expected[i].W);
        CHECK_NEAR(got[i].H, expected[i].H);
    }
}

u64 Mask(const char* rows)
{
    u64 mask = 0;
    for (int i = 0; i < kDebugTextColumns * kDebugTextRows; ++i) {
        if (rows[i] == '#')
            mask |= u64{1} << i;
    }
    return mask;
}

} // namespace

TEST(DebugTextMeasuresLinesAndScale)
{
    CHECK(DebugTextLineHeight(1) == 9.0f);
    CHECK(DebugTextLineHeight(2) == 18.0f);
    CHECK(DebugTextLineHeight(0) == 0.0f);
    CHECK(DebugTextLineHeight(-3) == 0.0f);

    CHECK_NEAR(MeasureDebugText(""), Vec2(0.0f, 9.0f));
    CHECK_NEAR(MeasureDebugText("", 2), Vec2(0.0f, 18.0f));
    CHECK_NEAR(MeasureDebugText("HI"), Vec2(12.0f, 9.0f));
    CHECK_NEAR(MeasureDebugText("hi", 2), Vec2(24.0f, 18.0f)); // lowercase, same cells
    CHECK_NEAR(MeasureDebugText("A B"), Vec2(18.0f, 9.0f));     // the space advances
    CHECK_NEAR(MeasureDebugText("A\rB"), Vec2(12.0f, 9.0f));    // '\r' is skipped
    CHECK_NEAR(MeasureDebugText("AB\nABCD"), Vec2(24.0f, 18.0f));
    CHECK_NEAR(MeasureDebugText("A\n"), Vec2(6.0f, 18.0f));
    CHECK_NEAR(MeasureDebugText("\n"), Vec2(0.0f, 18.0f));
    CHECK_NEAR(MeasureDebugText("NO", 0), Vec2(0.0f, 0.0f));
    CHECK_NEAR(MeasureDebugText("NO", -1), Vec2(0.0f, 0.0f));
    // Pen width includes the gap column: 'E' is five pixels of ink and advances six.
    CHECK_NEAR(MeasureDebugText("E", 3), Vec2(18.0f, 27.0f));
}

TEST(DebugTextWrapsOnSpaces)
{
    // "AA AA" is 5 advances (30). A limit of 24 fits "AA " (18) and not the next word.
    CHECK_NEAR(MeasureDebugText("AA AA", 1, 24.0f), Vec2(18.0f, 18.0f));
    CHECK_NEAR(MeasureDebugText("AA AA", 1, 30.0f), Vec2(30.0f, 9.0f)); // exact fit, one line
    CHECK_NEAR(MeasureDebugText("AA AA"), Vec2(30.0f, 9.0f));           // 0 does not wrap
    // A word longer than the width stays intact, at the start of a line.
    CHECK_NEAR(MeasureDebugText("AAAAAA", 1, 10.0f), Vec2(36.0f, 9.0f));
    CHECK_NEAR(MeasureDebugText("AA AAAAAA", 1, 20.0f), Vec2(36.0f, 18.0f));

    Renderer2D renderer;
    renderer.Begin(Mat4::Identity());
    const f32 bottom = DrawDebugText(renderer, {10.0f, 4.0f}, "AA AA", kInk, 1, 24.0f);
    renderer.End();
    CHECK_NEAR(bottom, 4.0f + MeasureDebugText("AA AA", 1, 24.0f).y);
    // The second "AA" starts at x = 10 on the next line.
    const std::vector<Rect> rects = Drawn(renderer);
    CHECK(!rects.empty());
    bool sawSecondLine = false;
    for (const Rect& rect : rects) {
        if (rect.Y >= 4.0f + 9.0f) {
            sawSecondLine = true;
            CHECK(rect.X >= 10.0f && rect.X < 10.0f + 12.0f);
        }
    }
    CHECK(sawSecondLine);
}

TEST(DebugTextGlyphCoverage)
{
    CHECK(sizeof(kGlyphs) / sizeof(kGlyphs[0]) == 52);
    CHECK(DebugTextHasGlyph('A') && DebugTextHasGlyph('a') && DebugTextHasGlyph(' '));
    CHECK(DebugTextHasGlyph('>') && DebugTextHasGlyph('[') && DebugTextHasGlyph(']'));
    CHECK(DebugTextHasGlyph(';') && DebugTextHasGlyph('%'));
    CHECK(!DebugTextHasGlyph('@') && !DebugTextHasGlyph('{') && !DebugTextHasGlyph('_'));

    std::vector<u64> masks;
    masks.reserve(53);
    const u64 box = Mask(kBox);
    masks.push_back(box);

    Renderer2D renderer;
    renderer.Begin(Mat4::Identity());
    const Vec2 at{8.0f, 3.0f};
    constexpr i32 scale = 2;
    std::string alphabet;
    for (const GlyphSpec& glyph : kGlyphs) {
        CHECK(std::char_traits<char>::length(glyph.Rows) ==
              static_cast<usize>(kDebugTextColumns * kDebugTextRows));
        const u64 mask = Mask(glyph.Rows);
        CHECK(mask != 0);
        CHECK(mask != box);
        for (const u64 earlier : masks)
            CHECK(mask != earlier);
        masks.push_back(mask);

        const u32 before = renderer.GetSpriteCount();
        const char text[2] = {glyph.Code, '\0'};
        DrawDebugText(renderer, at, std::string_view(text, 1), kInk, scale);
        const std::vector<Rect> all = Drawn(renderer);
        std::vector<Rect> got(all.begin() + before, all.end());
        CheckRects(got, Runs(glyph.Rows, at, scale));
        alphabet.push_back(glyph.Code);
    }
    renderer.End();

    // One sprite command for the whole set: every glyph is the white 1x1 texture.
    CHECK(renderer.GetCommands().size() == 1);
    CHECK(renderer.GetCommands()[0].Type == Renderer2D::CommandType::Sprites);
    CHECK(renderer.GetSpriteVertices()[0].Color == Renderer2D::PackColor(kInk));

    // Lowercase letters are the uppercase glyphs. A byte with no glyph is the hollow box.
    Renderer2D letters;
    letters.Begin(Mat4::Identity());
    DrawDebugText(letters, {0.0f, 0.0f}, "Ab", kInk, 1);
    letters.End();
    Renderer2D upper;
    upper.Begin(Mat4::Identity());
    DrawDebugText(upper, {0.0f, 0.0f}, "AB", kInk, 1);
    upper.End();
    CheckRects(Drawn(letters), Drawn(upper));

    Renderer2D unknown;
    unknown.Begin(Mat4::Identity());
    DrawDebugText(unknown, at, "@#\t", kInk, scale);
    unknown.End();
    const std::vector<Rect> boxes = Drawn(unknown);
    std::vector<Rect> expected;
    const f32 step = static_cast<f32>(kDebugTextAdvance * scale);
    for (int i = 0; i < 3; ++i) {
        const std::vector<Rect> one = Runs(kBox, {at.x + step * static_cast<f32>(i), at.y}, scale);
        expected.insert(expected.end(), one.begin(), one.end());
    }
    CheckRects(boxes, expected);

    Renderer2D blank;
    blank.Begin(Mat4::Identity());
    const f32 bottom = DrawDebugText(blank, {1.0f, 2.0f}, " ", kInk, 4);
    CHECK(blank.GetSpriteCount() == 0);
    CHECK_NEAR(bottom, 2.0f + DebugTextLineHeight(4));
    CHECK_NEAR(DrawDebugText(blank, {5.0f, 6.0f}, "HI", kInk, 0), 6.0f);
    CHECK(blank.GetSpriteCount() == 0);
    blank.End();

    // 'E' uses every column on its top row, so the ink ends one pixel before the next cell.
    Renderer2D wide;
    wide.Begin(Mat4::Identity());
    DrawDebugText(wide, {0.0f, 0.0f}, "E", kInk, 3);
    wide.End();
    f32 right = 0.0f;
    for (const Rect& rect : Drawn(wide))
        right = std::max(right, rect.X + rect.W);
    CHECK_NEAR(right, 15.0f);
    CHECK_NEAR(MeasureDebugText("E", 3).x, 18.0f);

    CHECK(alphabet.size() == 52);
}

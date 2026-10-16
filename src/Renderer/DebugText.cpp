#include "Emerald/Renderer/DebugText.h"

#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

namespace {

// Five columns, seven rows. '#' is ink. A run of '#' on one row is one rectangle.
const char* GlyphRows(char code)
{
    switch (code) {
    case 'A':
        return ".###."
               "#...#"
               "#...#"
               "#####"
               "#...#"
               "#...#"
               "#...#";
    case 'B':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#...#"
               "#...#"
               "####.";
    case 'C':
        return ".###."
               "#...#"
               "#...."
               "#...."
               "#...."
               "#...#"
               ".###.";
    case 'D':
        return "####."
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "####.";
    case 'E':
        return "#####"
               "#...."
               "#...."
               "####."
               "#...."
               "#...."
               "#####";
    case 'F':
        return "#####"
               "#...."
               "#...."
               "####."
               "#...."
               "#...."
               "#....";
    case 'G':
        return ".###."
               "#...#"
               "#...."
               "#.###"
               "#...#"
               "#...#"
               ".####";
    case 'H':
        return "#...#"
               "#...#"
               "#...#"
               "#####"
               "#...#"
               "#...#"
               "#...#";
    case 'I':
        return ".###."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".###.";
    case 'J':
        return "..###"
               "...#."
               "...#."
               "...#."
               "#..#."
               "#..#."
               ".##..";
    case 'K':
        return "#...#"
               "#..#."
               "#.#.."
               "##..."
               "#.#.."
               "#..#."
               "#...#";
    case 'L':
        return "#...."
               "#...."
               "#...."
               "#...."
               "#...."
               "#...."
               "#####";
    case 'M':
        return "#...#"
               "##.##"
               "#.#.#"
               "#...#"
               "#...#"
               "#...#"
               "#...#";
    case 'N':
        return "#...#"
               "##..#"
               "#.#.#"
               "#..##"
               "#...#"
               "#...#"
               "#...#";
    case 'O':
        return ".###."
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".###.";
    case 'P':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#...."
               "#...."
               "#....";
    case 'Q':
        return ".###."
               "#...#"
               "#...#"
               "#...#"
               "#.#.#"
               "#..#."
               ".##.#";
    case 'R':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#.#.."
               "#..#."
               "#...#";
    case 'S':
        return ".####"
               "#...."
               "#...."
               ".###."
               "....#"
               "....#"
               "####.";
    case 'T':
        return "#####"
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#..";
    case 'U':
        return "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".###.";
    case 'V':
        return "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".#.#."
               "..#..";
    case 'W':
        return "#...#"
               "#...#"
               "#...#"
               "#.#.#"
               "#.#.#"
               "#.#.#"
               ".#.#.";
    case 'X':
        return "#...#"
               "#...#"
               ".#.#."
               "..#.."
               ".#.#."
               "#...#"
               "#...#";
    case 'Y':
        return "#...#"
               "#...#"
               ".#.#."
               "..#.."
               "..#.."
               "..#.."
               "..#..";
    case 'Z':
        return "#####"
               "....#"
               "...#."
               "..#.."
               ".#..."
               "#...."
               "#####";
    case '0':
        return ".###."
               "#...#"
               "#..##"
               "#.#.#"
               "##..#"
               "#...#"
               ".###.";
    case '1':
        return "..#.."
               ".##.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".###.";
    case '2':
        return ".###."
               "#...#"
               "....#"
               "...#."
               "..#.."
               ".#..."
               "#####";
    case '3':
        return ".###."
               "#...#"
               "....#"
               "..##."
               "....#"
               "#...#"
               ".###.";
    case '4':
        return "...#."
               "..##."
               ".#.#."
               "#..#."
               "#####"
               "...#."
               "...#.";
    case '5':
        return "#####"
               "#...."
               "#...."
               "####."
               "....#"
               "....#"
               "####.";
    case '6':
        return ".###."
               "#...."
               "#...."
               "####."
               "#...#"
               "#...#"
               ".###.";
    case '7':
        return "#####"
               "....#"
               "...#."
               "..#.."
               ".#..."
               ".#..."
               ".#...";
    case '8':
        return ".###."
               "#...#"
               "#...#"
               ".###."
               "#...#"
               "#...#"
               ".###.";
    case '9':
        return ".###."
               "#...#"
               "#...#"
               ".####"
               "....#"
               "....#"
               ".###.";
    case '.':
        return "....."
               "....."
               "....."
               "....."
               "....."
               "..#.."
               "..#..";
    case ',':
        return "....."
               "....."
               "....."
               "....."
               "..#.."
               "..#.."
               ".#...";
    case ':':
        return "....."
               "..#.."
               "..#.."
               "....."
               "..#.."
               "..#.."
               ".....";
    case '-':
        return "....."
               "....."
               "....."
               "#####"
               "....."
               "....."
               ".....";
    case '+':
        return "....."
               "..#.."
               "..#.."
               "#####"
               "..#.."
               "..#.."
               ".....";
    case '/':
        return "....#"
               "...#."
               "...#."
               "..#.."
               ".#..."
               ".#..."
               "#....";
    case '!':
        return "..#.."
               "..#.."
               "..#.."
               "..#.."
               "....."
               "..#.."
               ".....";
    case '?':
        return ".###."
               "#...#"
               "....#"
               "...#."
               "..#.."
               "....."
               "..#..";
    case '\'':
        return "..#.."
               "..#.."
               "....."
               "....."
               "....."
               "....."
               ".....";
    case '(':
        return "..#.."
               ".#..."
               ".#..."
               ".#..."
               ".#..."
               ".#..."
               "..#..";
    case ')':
        return ".#..."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".#...";
    default:
        return "#####"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#####";
    }
}

char Upper(char code)
{
    if (code >= 'a' && code <= 'z')
        return static_cast<char>(code - 'a' + 'A');
    return code;
}

void DrawGlyph(Renderer2D& renderer, f32 x, f32 y, char code, const Vec4& color, i32 scale)
{
    if (code == ' ')
        return;
    const char* rows = GlyphRows(Upper(code));
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
            renderer.FillRect(
                {x + static_cast<f32>(start) * pixel, y + static_cast<f32>(row) * pixel},
                {static_cast<f32>(col - start) * pixel, pixel}, color);
        }
    }
}

struct Layout {
    f32 Width = 0.0f;
    f32 Bottom = 0.0f; // y just below the last line
};

// One walk for measuring and drawing. `draw` sees each byte that occupies a cell (spaces too).
template <typename DrawFn>
Layout LayoutText(const Vec2& position, std::string_view text, i32 scale, f32 maxWidth,
                  DrawFn&& draw)
{
    if (scale <= 0)
        return {.Bottom = position.y};
    const f32 advance = static_cast<f32>(kDebugTextAdvance) * static_cast<f32>(scale);
    const f32 line = DebugTextLineHeight(scale);
    const f32 limit = maxWidth > 0.0f ? position.x + maxWidth : 0.0f;
    f32 x = position.x;
    f32 y = position.y;
    f32 maxX = x;

    const auto newline = [&]() {
        if (x > maxX)
            maxX = x;
        x = position.x;
        y += line;
    };

    usize i = 0;
    while (i < text.size()) {
        const char code = text[i];
        if (code == '\n') {
            newline();
            ++i;
            continue;
        }
        if (code == '\r') {
            ++i;
            continue;
        }
        usize end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != '\n' && text[end] != '\r')
            ++end;
        const f32 word = static_cast<f32>(end - i) * advance;
        if (limit > 0.0f && x > position.x && x + word > limit)
            newline();
        for (; i < end; ++i) {
            draw(x, y, text[i]);
            x += advance;
        }
        if (i < text.size() && text[i] == ' ') {
            draw(x, y, ' ');
            x += advance;
            ++i;
        }
    }
    if (x > maxX)
        maxX = x;
    return {.Width = maxX - position.x, .Bottom = y + line};
}

} // namespace

Vec2 MeasureDebugText(std::string_view text, i32 scale, f32 maxWidth)
{
    const Layout layout =
        LayoutText({0.0f, 0.0f}, text, scale, maxWidth, [](f32, f32, char) {});
    return {layout.Width, layout.Bottom};
}

f32 DrawDebugText(Renderer2D& renderer, const Vec2& position, std::string_view text,
                  const Vec4& color, i32 scale, f32 maxWidth)
{
    const Layout layout = LayoutText(position, text, scale, maxWidth, [&](f32 x, f32 y, char code) {
        DrawGlyph(renderer, x, y, code, color, scale);
    });
    return layout.Bottom;
}

} // namespace Emerald

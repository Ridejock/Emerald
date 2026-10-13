#pragma once

// Private helpers for text files (saves, input recordings, particle effects, tweaks): floats as
// the shortest text that reads back to the same float, so files stay readable and round-trip.
// Both ways ignore the C locale (setlocale): always a '.', never a ','.

#include <charconv>
#include <string>
#include <string_view>

#include "Emerald/Core/Defines.h"

namespace Emerald {

[[nodiscard]] inline std::string FloatToText(f32 value)
{
    char text[32];
    const auto [end, error] = std::to_chars(text, text + sizeof(text), value);
    if (error != std::errc())
        return "0";
    return {text, end};
}

[[nodiscard]] inline std::string FloatToText(f64 value)
{
    char text[32];
    const auto [end, error] = std::to_chars(text, text + sizeof(text), value);
    if (error != std::errc())
        return "0";
    return {text, end};
}

// The whole of `text` as a number ("1.5", "-2", "1e-05", "inf"); false if it isn't one. Also
// takes what strtod did in older files: a leading '+', and a ',' for the decimal point (written
// under a locale that uses one).
[[nodiscard]] inline bool TextToFloat(std::string_view text, f64& value)
{
    if (text.size() > 1 && text[0] == '+' && text[1] != '-')
        text.remove_prefix(1);
    std::string comma;
    if (text.find(',') != std::string_view::npos && text.find('.') == std::string_view::npos) {
        comma.assign(text);
        comma[comma.find(',')] = '.';
        text = comma;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && error == std::errc() && end == text.data() + text.size();
}

} // namespace Emerald

#pragma once

// Private helpers for text files (saves, input recordings, particle effects, tweaks): floats as
// the shortest text that reads back to the same float, so files stay readable and round-trip.
// Both ways ignore the C locale (setlocale): always a '.', never a ','.

#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <version>

#include "Emerald/Core/Defines.h"

// Floating-point std::from_chars. libstdc++ and MSVC ship it with <charconv>
// (__cpp_lib_to_chars is 201611L there). libc++ keeps the overloads deleted
// until LLVM 20, and still reports __cpp_lib_to_chars as 201611L, so the
// version check is what selects the fallback on Apple.
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 202306L
#define EMERALD_FLOAT_FROM_CHARS 1
#elif defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 200000
#define EMERALD_FLOAT_FROM_CHARS 0
#elif defined(__cpp_lib_to_chars)
#define EMERALD_FLOAT_FROM_CHARS 1
#else
#define EMERALD_FLOAT_FROM_CHARS 0
#endif

#if !EMERALD_FLOAT_FROM_CHARS
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <locale.h>
#else
#include <locale.h> // newlocale / strtod_l, the C locale (not setlocale)
#endif
#endif

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
    if (text.empty())
        return false;
#if EMERALD_FLOAT_FROM_CHARS
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
#else
    // from_chars does not skip whitespace and does not take a hex float (0x...).
    const char first = text.front();
    if (first == ' ' || first == '\t' || first == '\n' || first == '\r' || first == '\v' ||
        first == '\f')
        return false;
    if (text.find('x') != std::string_view::npos || text.find('X') != std::string_view::npos)
        return false;

    // strtod needs a terminator. The C locale, not the global one: a decimal comma
    // in LC_NUMERIC must not change the parse. Same acceptance as from_chars,
    // including a full-string match and range errors.
    const std::string copy(text);
#if defined(_WIN32)
    static const _locale_t cLocale = _create_locale(LC_ALL, "C");
#else
    static const locale_t cLocale = newlocale(LC_ALL_MASK, "C", nullptr);
#endif
    if (cLocale == nullptr)
        return false;
    char* end = nullptr;
    errno = 0;
#if defined(_WIN32)
    const double parsed = _strtod_l(copy.c_str(), &end, cLocale);
#else
    const double parsed = strtod_l(copy.c_str(), &end, cLocale);
#endif
    if (end != copy.c_str() + copy.size() || errno == ERANGE)
        return false;
    value = parsed;
    return true;
#endif
}

} // namespace Emerald

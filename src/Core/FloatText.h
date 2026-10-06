#pragma once

// Private helper for the hand-written JSON files (particle effects, tweaks): floats as the
// shortest text that reads back to the same float, so files stay readable and round-trip.

#include <charconv>
#include <string>

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

} // namespace Emerald

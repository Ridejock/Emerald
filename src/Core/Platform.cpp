#include "Emerald/Core/Platform.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <string_view>
#endif

namespace Emerald {

bool IsSteamDeck()
{
#if defined(_WIN32)
    // getenv is deprecated on MSVC (C4996) and the Windows jobs treat warnings as errors.
    char value[8] = {};
    const DWORD length =
        GetEnvironmentVariableA("SteamDeck", value, static_cast<DWORD>(sizeof(value)));
    return length == 1 && value[0] == '1';
#else
    const char* value = std::getenv("SteamDeck");
    return value != nullptr && std::string_view(value) == "1";
#endif
}

} // namespace Emerald

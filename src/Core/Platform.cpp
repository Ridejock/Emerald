#include "Emerald/Core/Platform.h"

#include <cstdlib>
#include <string_view>

namespace Emerald {

bool IsSteamDeck()
{
    const char* value = std::getenv("SteamDeck");
    return value != nullptr && std::string_view(value) == "1";
}

} // namespace Emerald

#include "Emerald/Core/Paths.h"

#include <string>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

namespace Emerald::Paths {

namespace {

// SDL hands out UTF-8; char8_t tells std::filesystem so (matters on Windows).
std::filesystem::path FromUtf8(const char* text)
{
    return std::filesystem::path(reinterpret_cast<const char8_t*>(text));
}

} // namespace

std::filesystem::path GetBasePath()
{
    const char* base = SDL_GetBasePath(); // owned by SDL; works before SDL_Init
    if (!base)
        return std::filesystem::current_path();
    return FromUtf8(base);
}

std::filesystem::path GetPrefPath(std::string_view org, std::string_view app)
{
    // SDL wants null-terminated strings.
    char* pref = SDL_GetPrefPath(std::string(org).c_str(), std::string(app).c_str());
    if (!pref)
        return {};
    std::filesystem::path path = FromUtf8(pref);
    SDL_free(pref); // unlike the base path, this one is ours to free
    return path;
}

} // namespace Emerald::Paths

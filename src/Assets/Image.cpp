#include "Emerald/Assets/Image.h"

#include <stb_image.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

std::optional<Image> FromStb(stbi_uc* pixels, int width, int height)
{
    if (!pixels)
        return std::nullopt;

    Image image;
    image.Width = width;
    image.Height = height;
    image.Pixels.assign(pixels, pixels + static_cast<usize>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

} // namespace

std::optional<Image> Image::LoadFromFile(const std::filesystem::path& path)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!pixels)
        EM_CORE_ERROR("Failed to load image '{}': {}", path.string(), stbi_failure_reason());
    return FromStb(pixels, w, h);
}

std::optional<Image> Image::LoadFromMemory(const u8* data, usize size)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels)
        EM_CORE_ERROR("Failed to decode image from memory: {}", stbi_failure_reason());
    return FromStb(pixels, w, h);
}

} // namespace Emerald

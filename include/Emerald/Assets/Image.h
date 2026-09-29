#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// CPU-side RGBA8 image, loaded with stb_image.
struct Image {
    i32 Width = 0;
    i32 Height = 0;
    std::vector<u8> Pixels; // Width * Height * 4 bytes, RGBA

    static std::optional<Image> LoadFromFile(const std::filesystem::path& path);
    static std::optional<Image> LoadFromMemory(const u8* data, usize size);
};

} // namespace Emerald

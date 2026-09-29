#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Emerald {

// CPU-side RGBA8 image, loaded with stb_image.
struct Image {
    int Width = 0;
    int Height = 0;
    std::vector<std::uint8_t> Pixels; // Width * Height * 4 bytes, RGBA

    static std::optional<Image> LoadFromFile(const std::filesystem::path& path);
    static std::optional<Image> LoadFromMemory(const std::uint8_t* data, std::size_t size);
};

} // namespace Emerald

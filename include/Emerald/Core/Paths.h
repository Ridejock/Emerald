#pragma once

#include <filesystem>
#include <string_view>

// Where to find and store files, independent of the working directory.
namespace Emerald::Paths {

// The folder containing the executable (assets and shaders are copied there).
// Falls back to the working directory if the OS can't tell us.
[[nodiscard]] std::filesystem::path GetBasePath();

// A per-user folder that is safe to write to (settings, saves, high scores). Created if needed.
//   Windows: %APPDATA%\<org>\<app>\      Linux: ~/.local/share/<org>/<app>/
//   macOS:   ~/Library/Application Support/<org>/<app>/
// Returns an empty path if it can't be found or created.
[[nodiscard]] std::filesystem::path GetPrefPath(std::string_view org, std::string_view app);

} // namespace Emerald::Paths

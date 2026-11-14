#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// One APAK version 1 file. Little-endian, no compression. The reader indexes paths and
// returns bytes. It does not unpack onto disk.
//
//   std::string error;
//   Pak pak;
//   if (!pak.Open(path, &error))
//       return; // error names the file and the field (magic, version, flags, crc, ...)
//   std::optional<std::vector<u8>> manifest = pak.Read("manifest.json", &error);
//
// A path that is not in the table is not a pack error: Read returns nullopt and clears
// `error`. A bad header, table, or CRC names the file and the field.
//
// Version must be 1. `flags` must be 0: bit 0 is reserved for a future zstd payload and
// bit 1 for a future XOR, and a reader rejects a flags value it does not understand.
class Pak {
public:
    // Opens and indexes `path`. On failure the pak is closed.
    [[nodiscard]] bool Open(const std::filesystem::path& path, std::string* error = nullptr);
    void Close();

    [[nodiscard]] bool IsOpen() const { return m_Open; }
    [[nodiscard]] const std::filesystem::path& GetPath() const { return m_Path; }
    [[nodiscard]] u32 GetCount() const { return static_cast<u32>(m_Entries.size()); }
    [[nodiscard]] bool Contains(std::string_view path) const;

    // Copies one blob. Missing path: nullopt and `error` cleared. A bad CRC or a short
    // file: nullopt and `error` names the file and the field.
    [[nodiscard]] std::optional<std::vector<u8>> Read(std::string_view path,
                                                      std::string* error = nullptr) const;

private:
    struct Entry {
        std::string Path;
        u64 Offset = 0;
        u64 Size = 0;
        u32 Crc = 0;
        u32 Flags = 0;
    };

    [[nodiscard]] const Entry* Find(std::string_view path) const;

    std::filesystem::path m_Path;
    std::vector<u8> m_Bytes;
    std::vector<Entry> m_Entries;
    bool m_Open = false;
};

} // namespace Emerald

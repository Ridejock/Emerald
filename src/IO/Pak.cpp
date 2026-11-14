#include "Emerald/IO/Pak.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

constexpr u8 kMagic[4] = {0x41, 0x50, 0x41, 0x4B}; // APAK
constexpr u16 kVersion = 1;
constexpr u64 kHeaderBytes = 32;
constexpr u64 kMaxFileBytes = 256ull * 1024ull * 1024ull;
constexpr u32 kMaxEntries = 100000;

void Note(std::string_view message)
{
    if (Log::Core())
        Log::Core()->error("{}", message);
}

std::string FileName(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    return name.empty() ? path.string() : name;
}

std::string Fail(const std::filesystem::path& path, std::string_view field, std::string* error)
{
    std::string message = FileName(path);
    message += ": ";
    message += field;
    Note(message);
    if (error != nullptr)
        *error = message;
    return message;
}

bool Fits(u64 offset, u64 count, u64 size)
{
    return count <= size && offset <= size - count;
}

u16 ReadU16(const u8* bytes)
{
    return static_cast<u16>(bytes[0] | (static_cast<u16>(bytes[1]) << 8));
}

u32 ReadU32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8) |
           (static_cast<u32>(bytes[2]) << 16) | (static_cast<u32>(bytes[3]) << 24);
}

u64 ReadU64(const u8* bytes)
{
    return static_cast<u64>(ReadU32(bytes)) | (static_cast<u64>(ReadU32(bytes + 4)) << 32);
}

u32 Crc32(const u8* bytes, u64 size)
{
    u32 crc = 0xFFFFFFFFu;
    for (u64 index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            const u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool PathOk(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.back() == '/')
        return false;
    if (path.find('\\') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view part = path.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return true;
}

bool PathLess(std::string_view left, std::string_view right)
{
    const usize count = left.size() < right.size() ? left.size() : right.size();
    for (usize index = 0; index < count; ++index) {
        const unsigned char a = static_cast<unsigned char>(left[index]);
        const unsigned char b = static_cast<unsigned char>(right[index]);
        if (a != b)
            return a < b;
    }
    return left.size() < right.size();
}

} // namespace

bool Pak::Open(const std::filesystem::path& path, std::string* error)
{
    Close();
    if (error != nullptr)
        error->clear();

    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code) || code) {
        Fail(path, "file", error);
        return false;
    }
    const u64 fileSize = static_cast<u64>(std::filesystem::file_size(path, code));
    if (code || fileSize < kHeaderBytes || fileSize > kMaxFileBytes) {
        Fail(path, "file", error);
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Fail(path, "file", error);
        return false;
    }
    std::vector<u8> bytes(static_cast<usize>(fileSize));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<u64>(in.gcount()) != fileSize) {
        Fail(path, "file", error);
        return false;
    }

    if (std::memcmp(bytes.data(), kMagic, 4) != 0) {
        Fail(path, "magic", error);
        return false;
    }
    const u16 version = ReadU16(bytes.data() + 4);
    if (version != kVersion) {
        Fail(path, "version", error);
        return false;
    }
    const u16 flags = ReadU16(bytes.data() + 6);
    if (flags != 0) {
        Fail(path, "flags", error);
        return false;
    }
    const u32 count = ReadU32(bytes.data() + 8);
    const u64 tocOffset = ReadU64(bytes.data() + 12);
    const u64 tocSize = ReadU64(bytes.data() + 20);
    const u32 reserved = ReadU32(bytes.data() + 28);
    if (reserved != 0) {
        Fail(path, "reserved", error);
        return false;
    }
    if (tocOffset != kHeaderBytes) {
        Fail(path, "toc_offset", error);
        return false;
    }
    if (count > kMaxEntries || !Fits(tocOffset, tocSize, fileSize)) {
        Fail(path, count > kMaxEntries ? "entry count" : "toc_size", error);
        return false;
    }

    std::vector<Entry> entries;
    entries.reserve(count);
    u64 cursor = tocOffset;
    const u64 tocEnd = tocOffset + tocSize;
    for (u32 index = 0; index < count; ++index) {
        if (!Fits(cursor, 2, tocEnd)) {
            Fail(path, "toc_size", error);
            return false;
        }
        const u16 pathLen = ReadU16(bytes.data() + cursor);
        cursor += 2;
        if (pathLen == 0 || !Fits(cursor, pathLen, tocEnd)) {
            Fail(path, "path", error);
            return false;
        }
        std::string entryPath(reinterpret_cast<const char*>(bytes.data() + cursor), pathLen);
        cursor += pathLen;
        if (!PathOk(entryPath)) {
            Fail(path, "path", error);
            return false;
        }
        if (!entries.empty() && !PathLess(entries.back().Path, entryPath)) {
            Fail(path, "path", error);
            return false;
        }
        constexpr u64 kTail = 8 + 8 + 4 + 4;
        if (!Fits(cursor, kTail, tocEnd)) {
            Fail(path, "toc_size", error);
            return false;
        }
        Entry entry;
        entry.Path = std::move(entryPath);
        entry.Offset = ReadU64(bytes.data() + cursor);
        entry.Size = ReadU64(bytes.data() + cursor + 8);
        entry.Crc = ReadU32(bytes.data() + cursor + 16);
        entry.Flags = ReadU32(bytes.data() + cursor + 20);
        cursor += kTail;
        if (entry.Flags != 0) {
            Fail(path, "flags", error);
            return false;
        }
        if ((entry.Offset % 8) != 0 || entry.Offset < tocEnd ||
            !Fits(entry.Offset, entry.Size, fileSize)) {
            Fail(path, "offset", error);
            return false;
        }
        entries.push_back(std::move(entry));
    }
    if (cursor != tocEnd) {
        Fail(path, "toc_size", error);
        return false;
    }

    m_Path = path;
    m_Bytes = std::move(bytes);
    m_Entries = std::move(entries);
    m_Open = true;
    if (!Contains("manifest.json")) {
        Fail(path, "manifest.json", error);
        Close();
        return false;
    }
    if (error != nullptr)
        error->clear();
    return true;
}

void Pak::Close()
{
    m_Path.clear();
    m_Bytes.clear();
    m_Entries.clear();
    m_Open = false;
}

bool Pak::Contains(std::string_view path) const
{
    return Find(path) != nullptr;
}

const Pak::Entry* Pak::Find(std::string_view path) const
{
    const auto it = std::lower_bound(
        m_Entries.begin(), m_Entries.end(), path,
        [](const Entry& entry, std::string_view key) { return PathLess(entry.Path, key); });
    if (it == m_Entries.end() || it->Path != path)
        return nullptr;
    return &*it;
}

std::optional<std::vector<u8>> Pak::Read(std::string_view path, std::string* error) const
{
    if (error != nullptr)
        error->clear();
    if (!m_Open) {
        Fail(m_Path, "file", error);
        return std::nullopt;
    }
    const Entry* entry = Find(path);
    if (entry == nullptr)
        return std::nullopt;
    if (!Fits(entry->Offset, entry->Size, m_Bytes.size())) {
        Fail(m_Path, "offset", error);
        return std::nullopt;
    }
    const u8* blob = m_Bytes.data() + entry->Offset;
    if (Crc32(blob, entry->Size) != entry->Crc) {
        std::string message = FileName(m_Path);
        message += ": ";
        message += entry->Path;
        message += ": crc";
        Note(message);
        if (error != nullptr)
            *error = std::move(message);
        return std::nullopt;
    }
    return std::vector<u8>(blob, blob + entry->Size);
}

} // namespace Emerald

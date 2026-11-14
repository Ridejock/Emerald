// APAK v1: header, table order, CRC, and path lookup. The bytes are generated here.
// No window, GPU, or paid asset.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <Emerald/IO/Pak.h>

#include "Test.h"

using namespace Emerald;

namespace {

void AppendU16(std::vector<u8>& out, u16 value)
{
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
}

void AppendU32(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 24));
}

void AppendU64(std::vector<u8>& out, u64 value)
{
    AppendU32(out, static_cast<u32>(value));
    AppendU32(out, static_cast<u32>(value >> 32));
}

u32 Crc32(const std::vector<u8>& bytes)
{
    u32 crc = 0xFFFFFFFFu;
    for (u8 byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

struct Item {
    std::string Path;
    std::vector<u8> Bytes;
    u32 Flags = 0;
};

// Builds one APAK. `version`, `flags`, and `reserved` override the header. Entries stay in
// the order given so a test can hand the reader an unsorted table.
std::vector<u8> BuildPak(const std::vector<Item>& items, u16 version = 1, u16 flags = 0,
                         u32 reserved = 0, u64 tocOffset = 32)
{
    u64 tocSize = 0;
    for (const Item& item : items)
        tocSize += 2 + item.Path.size() + 8 + 8 + 4 + 4;
    u64 blobAt = tocOffset + tocSize;
    blobAt = (blobAt + 7ull) & ~7ull;

    std::vector<u8> toc;
    std::vector<u8> blobs;
    u64 cursor = blobAt;
    for (const Item& item : items) {
        while (blobs.size() + blobAt < cursor)
            blobs.push_back(0);
        AppendU16(toc, static_cast<u16>(item.Path.size()));
        toc.insert(toc.end(), item.Path.begin(), item.Path.end());
        AppendU64(toc, cursor);
        AppendU64(toc, item.Bytes.size());
        AppendU32(toc, Crc32(item.Bytes));
        AppendU32(toc, item.Flags);
        blobs.insert(blobs.end(), item.Bytes.begin(), item.Bytes.end());
        cursor += item.Bytes.size();
        const u64 aligned = (cursor + 7ull) & ~7ull;
        cursor = aligned;
    }

    std::vector<u8> file;
    file.insert(file.end(), {'A', 'P', 'A', 'K'});
    AppendU16(file, version);
    AppendU16(file, flags);
    AppendU32(file, static_cast<u32>(items.size()));
    AppendU64(file, tocOffset);
    AppendU64(file, toc.size());
    AppendU32(file, reserved);
    while (file.size() < tocOffset)
        file.push_back(0);
    file.insert(file.end(), toc.begin(), toc.end());
    while (file.size() < blobAt)
        file.push_back(0);
    file.insert(file.end(), blobs.begin(), blobs.end());
    return file;
}

std::filesystem::path WriteTemp(const std::vector<u8>& bytes, const char* name)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "emerald-pak-tests" / name;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::vector<u8> Text(const char* text)
{
    return {text, text + std::char_traits<char>::length(text)};
}

} // namespace

TEST(PakMinimalManifestRoundTrip)
{
    const std::vector<u8> manifest = Text("{}\n");
    const std::vector<u8> file = BuildPak({{"manifest.json", manifest}});
    // The spec's one-file example: table is 39 bytes and the blob starts at offset 72.
    CHECK(file.size() >= 72 + manifest.size());
    CHECK(file[0] == 'A' && file[1] == 'P' && file[2] == 'A' && file[3] == 'K');
    CHECK(file[4] == 1 && file[5] == 0); // version
    CHECK(file[6] == 0 && file[7] == 0); // flags
    CHECK(file[8] == 1 && file[9] == 0 && file[10] == 0 && file[11] == 0);
    CHECK(file[12] == 32 && file[13] == 0); // toc_offset
    CHECK(file[20] == 39 && file[21] == 0); // toc_size
    const std::filesystem::path path = WriteTemp(file, "minimal.pak");
    Pak pak;
    std::string error;
    CHECK(pak.Open(path, &error));
    CHECK(error.empty());
    CHECK(pak.GetCount() == 1);
    CHECK(pak.Contains("manifest.json"));
    const std::optional<std::vector<u8>> got = pak.Read("manifest.json", &error);
    CHECK(got.has_value());
    CHECK(error.empty());
    if (got)
        CHECK(*got == manifest);
    CHECK(!pak.Contains("missing.png"));
    CHECK(!pak.Read("missing.png", &error).has_value());
    CHECK(error.empty());
}

TEST(PakRejectsABadHeader)
{
    const std::vector<Item> items = {{"manifest.json", Text("{}\n")}};
    Pak pak;
    std::string error;
    CHECK(!pak.Open(WriteTemp(BuildPak(items, 2), "version.pak"), &error));
    CHECK(error.find("version") != std::string::npos);
    CHECK(error.find("version.pak") != std::string::npos);

    error.clear();
    CHECK(!pak.Open(WriteTemp(BuildPak(items, 1, 1), "flags.pak"), &error));
    CHECK(error.find("flags") != std::string::npos);

    error.clear();
    CHECK(!pak.Open(WriteTemp(BuildPak(items, 1, 0, 1), "reserved.pak"), &error));
    CHECK(error.find("reserved") != std::string::npos);

    std::vector<u8> magic = BuildPak(items);
    magic[0] = 'X';
    error.clear();
    CHECK(!pak.Open(WriteTemp(magic, "magic.pak"), &error));
    CHECK(error.find("magic") != std::string::npos);
    CHECK(!pak.IsOpen());
}

TEST(PakRejectsABadTable)
{
    Pak pak;
    std::string error;
    const std::vector<Item> backwards = {{"z.txt", Text("z")}, {"manifest.json", Text("{}\n")}};
    CHECK(!pak.Open(WriteTemp(BuildPak(backwards), "order.pak"), &error));
    CHECK(error.find("path") != std::string::npos);

    error.clear();
    const std::vector<Item> climb = {{"../out.txt", Text("no")}, {"manifest.json", Text("{}\n")}};
    CHECK(!pak.Open(WriteTemp(BuildPak(climb), "climb.pak"), &error));
    CHECK(error.find("path") != std::string::npos);

    error.clear();
    std::vector<Item> flagged = {{"manifest.json", Text("{}\n")}};
    flagged[0].Flags = 1;
    CHECK(!pak.Open(WriteTemp(BuildPak(flagged), "entry-flags.pak"), &error));
    CHECK(error.find("flags") != std::string::npos);

    error.clear();
    CHECK(!pak.Open(WriteTemp(BuildPak({{"readme.txt", Text("hi")}}), "no-manifest.pak"), &error));
    CHECK(error.find("manifest.json") != std::string::npos);
}

TEST(PakCrcFailureNamesTheField)
{
    std::vector<u8> file = BuildPak({{"manifest.json", Text("{}\n")}, {"note.txt", Text("ok")}});
    file.back() ^= 0xFF;
    const std::filesystem::path path = WriteTemp(file, "crc.pak");
    Pak pak;
    std::string error;
    CHECK(pak.Open(path, &error));
    CHECK(pak.Read("manifest.json", &error).has_value());
    CHECK(!pak.Read("note.txt", &error).has_value());
    CHECK(error.find("note.txt") != std::string::npos);
    CHECK(error.find("crc") != std::string::npos);
    CHECK(error.find("crc.pak") != std::string::npos);
}

TEST(PakLooksUpSeveralPaths)
{
    const std::vector<Item> items = {
        {"art/a.png", {0x89, 0x50, 0x4E, 0x47}},
        {"manifest.json", Text("{\"credit\":\"Test\"}\n")},
        {"sounds/blip.ogg", {0x4F, 0x67, 0x67, 0x53}},
    };
    const std::filesystem::path path = WriteTemp(BuildPak(items), "lookup.pak");
    Pak pak;
    std::string error;
    CHECK(pak.Open(path, &error));
    CHECK(pak.GetCount() == 3);
    const std::optional<std::vector<u8>> png = pak.Read("art/a.png", &error);
    CHECK(png.has_value());
    if (png)
        CHECK(png->size() == 4 && (*png)[1] == 0x50);
    CHECK(pak.Read("sounds/blip.ogg", &error)->size() == 4);
}

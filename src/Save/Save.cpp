#include "Emerald/Save/Save.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Core/Paths.h"

namespace fs = std::filesystem;

namespace Emerald {

namespace {

constexpr std::string_view kSeparator = "---";

bool IsNameChar(char c, bool allowDot)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || (allowDot && c == '.');
}

bool ParseI32(std::string_view text, i32& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

bool ParseU32(std::string_view text, u32& value, i32 base = 10)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return error == std::errc() && end == text.data() + text.size();
}

bool ParseI64(std::string_view text, i64& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size();
}

// Same as InputRecording: "%.9g" reads back as exactly the same f32.
bool ParseF32(std::string_view text, f32& value)
{
    const std::string copy(text); // strtod wants a terminated string
    char* end = nullptr;
    const f64 parsed = std::strtod(copy.c_str(), &end);
    if (copy.empty() || end != copy.c_str() + copy.size())
        return false;
    value = static_cast<f32>(parsed);
    return true;
}

// Values are one line each: newlines, carriage returns and backslashes are escaped.
std::string Escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out;
}

std::string Unescape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (usize i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next; // "\\" -> "\"
    }
    return out;
}

// CRC-32 (the zip / PNG one), bit by bit: saves are small, so no table needed.
u32 Crc32(std::string_view bytes)
{
    u32 crc = 0xFFFFFFFFu;
    for (const char c : bytes) {
        crc ^= static_cast<u8>(c);
        for (i32 bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

// Takes the next line off `rest` (without the '\n', and without a '\r' from a Windows editor).
std::string_view NextLine(std::string_view& rest)
{
    const usize end = rest.find('\n');
    std::string_view line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

// "key = value" -> key, value. The value keeps everything after "= " (inner and trailing spaces
// too); "key=value" written by hand works as well.
bool SplitKeyValue(std::string_view line, std::string_view& key, std::string_view& value)
{
    const usize equals = line.find('=');
    if (equals == std::string_view::npos)
        return false;
    key = line.substr(0, equals);
    while (!key.empty() && key.back() == ' ')
        key.remove_suffix(1);
    value = line.substr(equals + 1);
    if (!value.empty() && value.front() == ' ')
        value.remove_prefix(1);
    return true;
}

// The file split into its parts, before the version check and migrations.
struct ParsedFile {
    u32 Version = 0;
    i64 SavedAt = 0;
    std::string Summary;
    SaveData Data;
};

// Fills `out`, or returns why the text is not a (complete) save.
std::string ParseFile(std::string_view text, ParsedFile& out)
{
    if (text.empty())
        return "the file is empty";
    std::string_view rest = text;

    // "EMERALD-SAVE 1": the magic and the container format.
    const std::string_view magic = NextLine(rest);
    const std::string prefix = std::string(SaveSystem::kMagic) + " ";
    u32 format = 0;
    if (!magic.starts_with(prefix) || !ParseU32(magic.substr(prefix.size()), format))
        return "not an Emerald save (no " + std::string(SaveSystem::kMagic) + " header)";
    if (format != SaveSystem::kFormat)
        return "unsupported save format " + std::to_string(format);

    // Header lines up to "---".
    bool hasVersion = false;
    bool hasCrc = false;
    u32 crc = 0;
    bool hasSeparator = false;
    while (!rest.empty()) {
        const std::string_view line = NextLine(rest);
        if (line == kSeparator) {
            hasSeparator = true;
            break;
        }
        std::string_view key, value;
        if (!SplitKeyValue(line, key, value))
            return "bad header line '" + std::string(line) + "'";
        if (key == "version")
            hasVersion = ParseU32(value, out.Version) && out.Version >= 1;
        else if (key == "saved")
            ParseI64(value, out.SavedAt); // only informational: a bad one reads as 0
        else if (key == "summary")
            out.Summary = Unescape(value);
        else if (key == "crc32")
            hasCrc = ParseU32(value, crc, 16);
        // Unknown header keys are skipped, so later formats can add some.
    }
    if (!hasSeparator)
        return "truncated (the header doesn't end with '---')";
    if (!hasVersion)
        return "no valid 'version' in the header";

    // The checksum covers everything after "---", so a cut-off or damaged file is caught. It is
    // optional so a save can be written or edited by hand (delete the crc32 line).
    if (hasCrc && Crc32(rest) != crc)
        return "checksum mismatch (the file is truncated or damaged)";

    i32 lineNumber = 0;
    while (!rest.empty()) {
        const std::string_view line = NextLine(rest);
        ++lineNumber;
        if (line.empty() || line.front() == '#')
            continue;
        std::string_view key, value;
        if (!SplitKeyValue(line, key, value) || !SaveData::IsValidKey(key))
            return "bad data line " + std::to_string(lineNumber) + ": '" + std::string(line) + "'";
        out.Data.SetString(key, Unescape(value));
    }
    return {};
}

std::string ToUtf8(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path WithSuffix(fs::path path, std::string_view suffix)
{
    path += suffix;
    return path;
}

bool ReadFile(const fs::path& path, std::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

// Writes the whole file and flushes it to the disk (FlushFileBuffers / fsync through SDL), so
// once this returns true the data survives a crash or power loss.
bool WriteFileDurably(const fs::path& path, std::string_view text)
{
    SDL_IOStream* io = SDL_IOFromFile(ToUtf8(path).c_str(), "wb");
    if (!io)
        return false;
    const bool written = SDL_WriteIO(io, text.data(), text.size()) == text.size();
    const bool flushed = written && SDL_FlushIO(io);
    const bool closed = SDL_CloseIO(io);
    return written && flushed && closed;
}

i64 UnixNow()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

// ---------------------------------------------------------------------------
// SaveData
// ---------------------------------------------------------------------------

bool SaveData::IsValidKey(std::string_view key)
{
    if (key.empty())
        return false;
    for (const char c : key) {
        if (!IsNameChar(c, true))
            return false;
    }
    return true;
}

void SaveData::SetInt(std::string_view key, i32 value)
{
    SetString(key, std::to_string(value));
}

void SaveData::SetFloat(std::string_view key, f32 value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", static_cast<f64>(value));
    SetString(key, text);
}

void SaveData::SetBool(std::string_view key, bool value)
{
    SetString(key, value ? "true" : "false");
}

void SaveData::SetString(std::string_view key, std::string_view value)
{
    if (!IsValidKey(key)) {
        EM_CORE_ERROR("SaveData: invalid key '{}' (letters, digits and . _ - only)", key);
        return;
    }
    const auto it = m_Values.find(key);
    if (it != m_Values.end())
        it->second = value;
    else
        m_Values.emplace(std::string(key), std::string(value));
}

i32 SaveData::GetInt(std::string_view key, i32 fallback) const
{
    const auto it = m_Values.find(key);
    i32 value = 0;
    return it != m_Values.end() && ParseI32(it->second, value) ? value : fallback;
}

f32 SaveData::GetFloat(std::string_view key, f32 fallback) const
{
    const auto it = m_Values.find(key);
    f32 value = 0.0f;
    return it != m_Values.end() && ParseF32(it->second, value) ? value : fallback;
}

bool SaveData::GetBool(std::string_view key, bool fallback) const
{
    const auto it = m_Values.find(key);
    if (it == m_Values.end())
        return fallback;
    if (it->second == "true")
        return true;
    if (it->second == "false")
        return false;
    return fallback;
}

std::string SaveData::GetString(std::string_view key, std::string_view fallback) const
{
    const auto it = m_Values.find(key);
    return it != m_Values.end() ? it->second : std::string(fallback);
}

bool SaveData::Has(std::string_view key) const
{
    return m_Values.find(key) != m_Values.end();
}

void SaveData::Remove(std::string_view key)
{
    const auto it = m_Values.find(key);
    if (it != m_Values.end())
        m_Values.erase(it);
}

void SaveData::Rename(std::string_view from, std::string_view to)
{
    const auto it = m_Values.find(from);
    if (it == m_Values.end() || from == to)
        return;
    std::string value = std::move(it->second);
    m_Values.erase(it);
    SetString(to, value);
}

// ---------------------------------------------------------------------------
// SaveSystem
// ---------------------------------------------------------------------------

std::string_view ToString(SaveError error)
{
    switch (error) {
    case SaveError::None:
        return "none";
    case SaveError::NotFound:
        return "not found";
    case SaveError::BadName:
        return "bad slot name";
    case SaveError::Unreadable:
        return "unreadable";
    case SaveError::Corrupt:
        return "corrupt";
    case SaveError::TooNew:
        return "too new";
    case SaveError::NoMigration:
        return "no migration";
    case SaveError::WriteFailed:
        return "write failed";
    }
    return "unknown";
}

SaveSystem::SaveSystem(fs::path folder, u32 version)
    : m_Folder(std::move(folder)), m_Version(version < 1 ? 1 : version)
{
}

fs::path SaveSystem::DefaultFolder(std::string_view org, std::string_view app)
{
    const fs::path pref = Paths::GetPrefPath(org, app);
    return pref.empty() ? fs::path("saves") : pref / "saves";
}

void SaveSystem::AddMigration(u32 fromVersion, std::function<void(SaveData&)> migrate)
{
    m_Migrations[fromVersion] = std::move(migrate);
}

bool SaveSystem::IsValidSlotName(std::string_view slot)
{
    if (slot.empty() || slot.size() > 64)
        return false;
    for (const char c : slot) {
        if (!IsNameChar(c, false))
            return false;
    }
    return true;
}

fs::path SaveSystem::GetPath(std::string_view slot) const
{
    return m_Folder / (std::string(slot) + ".sav");
}

std::string SaveSystem::Serialize(const SaveData& data, u32 version, i64 savedAt,
                                  std::string_view summary)
{
    std::string body;
    for (const auto& [key, value] : data.GetValues())
        body += key + " = " + Escape(value) + "\n";

    char crc[16];
    std::snprintf(crc, sizeof(crc), "%08x", Crc32(body));

    std::string text = std::string(kMagic) + " " + std::to_string(kFormat) + "\n";
    text += "version = " + std::to_string(version) + "\n";
    text += "saved = " + std::to_string(savedAt) + "\n";
    if (!summary.empty())
        text += "summary = " + Escape(summary) + "\n";
    text += "crc32 = " + std::string(crc) + "\n";
    text += std::string(kSeparator) + "\n";
    return text + body;
}

LoadResult SaveSystem::Parse(std::string_view text) const
{
    LoadResult result;
    ParsedFile file;
    if (std::string why = ParseFile(text, file); !why.empty()) {
        result.Error = SaveError::Corrupt;
        result.Message = std::move(why);
        return result;
    }
    result.FileVersion = file.Version;
    if (file.Version > m_Version) {
        result.Error = SaveError::TooNew;
        result.Message = "saved with data version " + std::to_string(file.Version) +
                         ", this game reads up to " + std::to_string(m_Version);
        return result;
    }
    // Upgrade step by step: v1 -> v2 -> ... -> current.
    for (u32 v = file.Version; v < m_Version; ++v) {
        const auto it = m_Migrations.find(v);
        if (it == m_Migrations.end() || !it->second) {
            result.Error = SaveError::NoMigration;
            result.Message = "no migration from data version " + std::to_string(v) + " to " +
                             std::to_string(v + 1);
            return result;
        }
        it->second(file.Data);
    }
    result.Data = std::move(file.Data);
    return result;
}

bool SaveSystem::Save(std::string_view slot, const SaveData& data, std::string_view summary)
{
    if (!IsValidSlotName(slot)) {
        EM_CORE_ERROR("Save: invalid slot name '{}'", slot);
        return false;
    }
    std::error_code ec;
    fs::create_directories(m_Folder, ec);
    if (ec) {
        EM_CORE_ERROR("Save: can't create {}: {}", ToUtf8(m_Folder), ec.message());
        return false;
    }

    const fs::path path = GetPath(slot);
    const fs::path temp = WithSuffix(path, ".tmp");
    if (!WriteFileDurably(temp, Serialize(data, m_Version, UnixNow(), summary))) {
        EM_CORE_ERROR("Save: writing {} failed ({}); the previous save is kept", ToUtf8(temp),
                      SDL_GetError());
        fs::remove(temp, ec);
        return false;
    }

    // Keep the previous save as .bak, but only if it is a good one: a damaged .sav must not
    // replace the last good backup.
    if (GetInfo(slot).Valid &&
        !SDL_CopyFile(ToUtf8(path).c_str(), ToUtf8(WithSuffix(path, ".bak")).c_str()))
        EM_CORE_WARN("Save: couldn't back up {}: {}", ToUtf8(path), SDL_GetError());

    // The atomic step: SDL renames with MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows and
    // rename() elsewhere, which replace the old file in one go.
    if (!SDL_RenamePath(ToUtf8(temp).c_str(), ToUtf8(path).c_str())) {
        EM_CORE_ERROR("Save: replacing {} failed ({}); the previous save is kept", ToUtf8(path),
                      SDL_GetError());
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

LoadResult SaveSystem::Load(std::string_view slot) const
{
    LoadResult result;
    if (!IsValidSlotName(slot)) {
        result.Error = SaveError::BadName;
        result.Message = "invalid slot name '" + std::string(slot) + "'";
        return result;
    }
    const fs::path path = GetPath(slot);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        result.Error = SaveError::NotFound; // normal on the first run: not logged
        result.Message = "no save in slot '" + std::string(slot) + "'";
        return result;
    }
    std::string text;
    if (ReadFile(path, text)) {
        result = Parse(text);
    } else {
        result.Error = SaveError::Unreadable;
        result.Message = "can't read " + ToUtf8(path);
    }
    if (result)
        return result;

    // A damaged save: try the previous one.
    if (result.Error == SaveError::Corrupt || result.Error == SaveError::Unreadable) {
        std::string backupText;
        if (ReadFile(WithSuffix(path, ".bak"), backupText)) {
            LoadResult backup = Parse(backupText);
            if (backup) {
                EM_CORE_WARN("Load: {} is damaged ({}: {}); loaded the backup instead",
                             ToUtf8(path), ToString(result.Error), result.Message);
                backup.FromBackup = true;
                return backup;
            }
        }
    }
    EM_CORE_WARN("Load: slot '{}' failed ({}): {}", slot, ToString(result.Error), result.Message);
    return result;
}

bool SaveSystem::Delete(std::string_view slot)
{
    if (!IsValidSlotName(slot))
        return false;
    const fs::path path = GetPath(slot);
    std::error_code ec;
    bool ok = true;
    for (const fs::path& file : {path, WithSuffix(path, ".bak"), WithSuffix(path, ".tmp")}) {
        fs::remove(file, ec);
        ok = ok && !ec;
    }
    return ok;
}

bool SaveSystem::Exists(std::string_view slot) const
{
    std::error_code ec;
    return IsValidSlotName(slot) && fs::is_regular_file(GetPath(slot), ec);
}

SaveSlotInfo SaveSystem::GetInfo(std::string_view slot) const
{
    SaveSlotInfo info;
    info.Name = slot;
    info.Exists = Exists(slot);
    std::string text;
    ParsedFile file;
    if (info.Exists && ReadFile(GetPath(slot), text) && ParseFile(text, file).empty()) {
        info.Valid = true;
        info.Version = file.Version;
        info.SavedAt = file.SavedAt;
        info.Summary = std::move(file.Summary);
    }
    return info;
}

std::vector<SaveSlotInfo> SaveSystem::ListSlots() const
{
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(m_Folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        const std::string name = ToUtf8(path.stem());
        if (path.extension() == ".sav" && IsValidSlotName(name))
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    std::vector<SaveSlotInfo> slots;
    for (const std::string& name : names)
        slots.push_back(GetInfo(name));
    return slots;
}

} // namespace Emerald

#include "Emerald/Save/Save.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>

#include "Emerald/Core/Log.h"
#include "Emerald/Core/Paths.h"

namespace fs = std::filesystem;

namespace Emerald {

namespace {

constexpr std::string_view kSeparator = "---";
constexpr std::string_view kBom = "\xEF\xBB\xBF";
// Over the whole file except its own line. Older files have "crc32" instead (the body only).
constexpr std::string_view kChecksumKey = "checksum";

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
    return std::isfinite(value); // NaN / inf (or out of f32 range) is not a usable value
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

std::string FormatCrc(u32 crc)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", crc);
    return text;
}

// Without a UTF-8 BOM and with "\r\n" as "\n", so a file opened in an editor still verifies.
std::string Normalize(std::string_view text)
{
    if (text.starts_with(kBom))
        text.remove_prefix(kBom.size());
    std::string out;
    out.reserve(text.size());
    for (usize i = 0; i < text.size(); ++i) {
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n'))
            out += text[i];
    }
    return out;
}

// Fills `out`, or returns why the text is not a (complete) save.
std::string ParseFile(std::string_view original, ParsedFile& out)
{
    const std::string normalized = Normalize(original);
    const std::string_view text = normalized;
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
    bool hasCrc = false; // "crc32": the body only (files before the checksum line)
    u32 crc = 0;
    bool hasChecksum = false;
    u32 checksum = 0;
    std::string covered; // what the checksum covers: the text without its line
    bool hasSeparator = false;
    while (!rest.empty()) {
        const usize lineStart = text.size() - rest.size();
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
        else if (key == "crc32" || key == kChecksumKey) {
            // Present but unreadable is damage, not "no checksum".
            u32& target = key == "crc32" ? crc : checksum;
            if (!ParseU32(value, target, 16))
                return "bad " + std::string(key) + " line '" + std::string(line) + "'";
            (key == "crc32" ? hasCrc : hasChecksum) = true;
            if (key == kChecksumKey)
                covered = std::string(text.substr(0, lineStart)) +
                          std::string(text.substr(text.size() - rest.size()));
        }
        // Unknown header keys are skipped, so later formats can add some.
    }
    if (!hasSeparator)
        return "truncated (the header doesn't end with '---')";
    if (!hasVersion)
        return "no valid 'version' in the header";

    // The checksum catches a cut-off or damaged file. It is optional so a save can be written or
    // edited by hand (delete the line).
    if (hasChecksum && Crc32(covered) != checksum)
        return "checksum mismatch (the file is truncated or damaged)";
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

// Antivirus and sync tools can hold a file open for a moment on Windows: try a few times.
bool RenameWithRetry(const fs::path& from, const fs::path& to)
{
    for (i32 attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0)
            SDL_Delay(50);
        if (SDL_RenamePath(ToUtf8(from).c_str(), ToUtf8(to).c_str()))
            return true;
    }
    return false;
}

bool ReadInfo(const fs::path& path, ParsedFile& file)
{
    std::string text;
    return ReadFile(path, text) && ParseFile(text, file).empty();
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
    if (!std::isfinite(value)) {
        EM_CORE_ERROR("SaveData: '{}' is not a finite number, not saved", key);
        return;
    }
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
    if (!IsValidKey(to)) {
        EM_CORE_ERROR("SaveData: invalid key '{}', '{}' is not renamed", to, from);
        return;
    }
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
    // Device names on Windows, whatever the extension.
    std::string lower(slot);
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul")
        return false;
    if (lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) &&
        lower[3] >= '0' && lower[3] <= '9')
        return false;
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

    std::string text = std::string(kMagic) + " " + std::to_string(kFormat) + "\n";
    text += "version = " + std::to_string(version) + "\n";
    text += "saved = " + std::to_string(savedAt) + "\n";
    if (!summary.empty())
        text += "summary = " + Escape(summary) + "\n";
    text += std::string(kSeparator) + "\n";
    text += body;
    // The checksum line goes last in the header and covers everything else.
    const std::string line = std::string(kChecksumKey) + " = " + FormatCrc(Crc32(text)) + "\n";
    text.insert(text.find("\n" + std::string(kSeparator) + "\n") + 1, line);
    return text;
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
    const fs::path backup = WithSuffix(path, ".bak");
    const fs::path temp = WithSuffix(path, ".tmp");

    // A save from a newer version of the game (a beta, before a downgrade) is moved aside to
    // <slot>.sav.v<N>, not overwritten: the newer game can still have it back.
    ParsedFile existing;
    const bool existingValid = ReadInfo(path, existing);
    if (existingValid && existing.Version > m_Version) {
        const fs::path aside = WithSuffix(path, ".v" + std::to_string(existing.Version));
        if (!RenameWithRetry(path, aside)) {
            EM_CORE_ERROR("Save: {} is from data version {} and can't be moved aside ({}); not "
                          "saved",
                          ToUtf8(path), existing.Version, SDL_GetError());
            return false;
        }
        EM_CORE_WARN("Save: {} is from data version {} (this game writes {}); kept as {}",
                     ToUtf8(path), existing.Version, m_Version, ToUtf8(aside));
    }

    if (!WriteFileDurably(temp, Serialize(data, m_Version, UnixNow(), summary))) {
        EM_CORE_ERROR("Save: writing {} failed ({}); the previous save is kept", ToUtf8(temp),
                      SDL_GetError());
        fs::remove(temp, ec);
        return false;
    }

    // The previous save becomes the .bak, but only if it is a good one: a damaged .sav must not
    // replace the last good backup. A rename replaces the old .bak in one go; a crash before the
    // next rename leaves just the .bak, which Load falls back to.
    if (existingValid && existing.Version <= m_Version && !RenameWithRetry(path, backup))
        EM_CORE_WARN("Save: couldn't back up {}: {}", ToUtf8(path), SDL_GetError());

    // The atomic step: SDL renames with MoveFileEx(MOVEFILE_REPLACE_EXISTING) on Windows and
    // rename() elsewhere, which replace the old file in one go.
    if (!RenameWithRetry(temp, path)) {
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
    const fs::path backup = WithSuffix(path, ".bak");
    std::error_code ec;
    std::string text;
    if (!fs::exists(path, ec)) {
        result.Error = SaveError::NotFound;
        result.Message = "no save in slot '" + std::string(slot) + "'";
    } else if (ReadFile(path, text)) {
        result = Parse(text);
    } else {
        result.Error = SaveError::Unreadable;
        result.Message = "can't read " + ToUtf8(path);
    }
    if (result)
        return result;

    // Missing (removed by a sync tool, or a crash mid-save) or damaged: try the previous one.
    const bool missing = result.Error == SaveError::NotFound;
    if (missing || result.Error == SaveError::Corrupt || result.Error == SaveError::Unreadable) {
        std::string backupText;
        if (fs::exists(backup, ec) && ReadFile(backup, backupText)) {
            LoadResult fallback = Parse(backupText);
            if (fallback) {
                EM_CORE_WARN("Load: {} is {} ({}); loaded the backup instead", ToUtf8(path),
                             missing ? "missing" : "damaged", result.Message);
                fallback.FromBackup = true;
                return fallback;
            }
        }
    }
    if (!missing) // a missing save is normal on the first run: not logged
        EM_CORE_WARN("Load: slot '{}' failed ({}): {}", slot, ToString(result.Error),
                     result.Message);
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
    return IsValidSlotName(slot) && (fs::is_regular_file(GetPath(slot), ec) ||
                                     fs::is_regular_file(WithSuffix(GetPath(slot), ".bak"), ec));
}

SaveSlotInfo SaveSystem::GetInfo(std::string_view slot) const
{
    SaveSlotInfo info;
    info.Name = slot;
    info.Exists = Exists(slot);
    if (!info.Exists)
        return info;
    const fs::path path = GetPath(slot);
    std::error_code ec;
    ParsedFile file;
    if (fs::is_regular_file(path, ec)) {
        info.Valid = ReadInfo(path, file);
    } else {
        info.Valid = ReadInfo(WithSuffix(path, ".bak"), file); // what Load would fall back to
        info.FromBackup = true;
    }
    if (info.Valid) {
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
        // <slot>.sav, or <slot>.sav.bak when only the backup is left.
        std::string name = ToUtf8(it->path().filename());
        for (const std::string_view suffix : {".sav.bak", ".sav"}) {
            if (name.ends_with(suffix)) {
                name.resize(name.size() - suffix.size());
                if (IsValidSlotName(name))
                    names.push_back(name);
                break;
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::vector<SaveSlotInfo> slots;
    for (const std::string& name : names)
        slots.push_back(GetInfo(name));
    return slots;
}

} // namespace Emerald

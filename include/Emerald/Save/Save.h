#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// The values in one save: text keys mapped to ints, floats, bools or strings. Everything is
// stored as text, so a save file is readable and diffs well. Keys are letters, digits and . _ -
// (e.g. "audio.master", "scores.3.name"). The getters return the fallback when a key is missing
// or doesn't hold that type, so a game can read new keys from old saves without checks.
class SaveData {
public:
    void SetInt(std::string_view key, i32 value);
    void SetFloat(std::string_view key, f32 value);
    void SetBool(std::string_view key, bool value);
    void SetString(std::string_view key, std::string_view value);

    [[nodiscard]] i32 GetInt(std::string_view key, i32 fallback = 0) const;
    [[nodiscard]] f32 GetFloat(std::string_view key, f32 fallback = 0.0f) const;
    [[nodiscard]] bool GetBool(std::string_view key, bool fallback = false) const;
    [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;

    [[nodiscard]] bool Has(std::string_view key) const;
    void Remove(std::string_view key);
    // Moves a value to another key (handy in migrations). Does nothing if `from` is missing.
    void Rename(std::string_view from, std::string_view to);
    void Clear() { m_Values.clear(); }
    [[nodiscard]] bool IsEmpty() const { return m_Values.empty(); }

    // Every key with its value as text, sorted by key (the order they are written in).
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& GetValues() const
    {
        return m_Values;
    }

    [[nodiscard]] static bool IsValidKey(std::string_view key);

private:
    // Sorted, so the same data always writes the same file.
    std::map<std::string, std::string, std::less<>> m_Values;
};

enum class SaveError : u8 {
    None,
    NotFound,    // no such slot
    BadName,     // slot names are letters, digits, _ and -
    Unreadable,  // the file exists but couldn't be read
    Corrupt,     // not a save, truncated, or the checksum doesn't match
    TooNew,      // written by a newer version of the game
    NoMigration, // older version, but a migration step is missing
    WriteFailed, // the new save couldn't be written; the old one is untouched
};

[[nodiscard]] std::string_view ToString(SaveError error);

struct LoadResult {
    SaveData Data;       // migrated to the current version
    u32 FileVersion = 0; // the version the file was written with
    SaveError Error = SaveError::None;
    std::string Message;     // what went wrong, for the log or a dialog
    bool FromBackup = false; // the save was damaged; this is the previous one (<slot>.bak)

    explicit operator bool() const { return Error == SaveError::None; }
};

struct SaveSlotInfo {
    std::string Name;    // "slot_1", "settings", ...
    bool Exists = false; // there is a file
    bool Valid = false;  // ...and it reads (the version may still be too new)
    u32 Version = 0;
    i64 SavedAt = 0;     // Unix time (seconds, UTC)
    std::string Summary; // the game's one-line description, e.g. "Level 3 - 12:40"
};

// Named save slots in one folder (one file per slot: <folder>/<name>.sav), with a data version
// and migrations from older versions.
//
//   SaveSystem saves(SaveSystem::DefaultFolder("Ridejock", "RockBlaster"), 2); // data version 2
//   saves.AddMigration(1, [](SaveData& d) { d.Rename("volume", "audio.master"); }); // v1 -> v2
//
//   SaveData settings;
//   settings.SetFloat("audio.master", 0.8f);
//   saves.Save("settings", settings);
//
//   if (LoadResult loaded = saves.Load("settings"))
//       volume = loaded.Data.GetFloat("audio.master", 1.0f);
//
// Saving is atomic: the new file is written to <name>.sav.tmp and flushed to disk, then renamed
// over <name>.sav, so a crash or a full disk leaves the old save as it was. The previous save is
// also kept as <name>.bak, and Load falls back to it if <name>.sav is damaged.
class SaveSystem {
public:
    // The file format (the container, not the game's data version).
    static constexpr std::string_view kMagic = "EMERALD-SAVE";
    static constexpr u32 kFormat = 1;

    // `folder` is created on the first save. `version` is the game's current data version (>= 1).
    SaveSystem(std::filesystem::path folder, u32 version);

    // The per-user folder for saves: Paths::GetPrefPath(org, app) / "saves".
    [[nodiscard]] static std::filesystem::path DefaultFolder(std::string_view org,
                                                             std::string_view app);

    // Registers the step from `fromVersion` to fromVersion + 1. Load runs the steps in a chain,
    // so a v1 save is upgraded v1 -> v2 -> v3 when the current version is 3.
    void AddMigration(u32 fromVersion, std::function<void(SaveData&)> migrate);

    // Writes `data` with the current version. False (and logged) if it failed; the previous save
    // is then unchanged.
    bool Save(std::string_view slot, const SaveData& data, std::string_view summary = {});
    [[nodiscard]] LoadResult Load(std::string_view slot) const;
    // Removes the slot's save and backup. True if nothing is left of it.
    bool Delete(std::string_view slot);

    [[nodiscard]] bool Exists(std::string_view slot) const;
    [[nodiscard]] SaveSlotInfo GetInfo(std::string_view slot) const;
    // Every slot in the folder, sorted by name.
    [[nodiscard]] std::vector<SaveSlotInfo> ListSlots() const;

    [[nodiscard]] std::filesystem::path GetPath(std::string_view slot) const;
    [[nodiscard]] const std::filesystem::path& GetFolder() const { return m_Folder; }
    [[nodiscard]] u32 GetVersion() const { return m_Version; }

    [[nodiscard]] static bool IsValidSlotName(std::string_view slot);

    // The file contents without touching the disk (tests, tools). Parse does the migrations.
    [[nodiscard]] static std::string Serialize(const SaveData& data, u32 version, i64 savedAt,
                                               std::string_view summary);
    [[nodiscard]] LoadResult Parse(std::string_view text) const;

private:
    std::filesystem::path m_Folder;
    u32 m_Version;
    std::map<u32, std::function<void(SaveData&)>> m_Migrations; // keyed by the "from" version
};

} // namespace Emerald

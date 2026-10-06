// The versioned save system (#15): values, the file format, migrations, atomic writes and slots.
// Everything runs in a temp folder, never the real per-user one.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <Emerald/Core/Log.h>
#include <Emerald/Save/Save.h>

#include "Test.h"

using namespace Emerald;
namespace fs = std::filesystem;

namespace {

// A fresh, empty folder for one test.
fs::path TempFolder(const char* name)
{
    const fs::path folder = fs::temp_directory_path() / "emerald_save_tests" / name;
    fs::remove_all(folder);
    return folder;
}

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteText(const fs::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}

SaveData Settings(f32 master, bool fullscreen)
{
    SaveData data;
    data.SetFloat("audio.master", master);
    data.SetBool("video.fullscreen", fullscreen);
    data.SetString("player.name", "Ervin");
    return data;
}

} // namespace

TEST(SaveDataValues)
{
    Log::Init({}); // console only; invalid keys and bad loads are logged
    SaveData data;
    data.SetInt("score", -42);
    data.SetFloat("volume", 0.1f);
    data.SetBool("muted", true);
    data.SetString("name", "  two\nlines \\ = here ");
    CHECK(data.GetInt("score") == -42);
    CHECK(data.GetFloat("volume") == 0.1f); // exact, not near
    CHECK(data.GetBool("muted"));
    CHECK(data.GetString("name") == "  two\nlines \\ = here ");

    // Missing keys and other types give the fallback.
    CHECK(data.GetInt("missing", 7) == 7);
    CHECK(data.GetInt("name", 3) == 3);
    CHECK(data.GetBool("score", true));
    CHECK(data.GetFloat("score") == -42.0f); // an int reads as a float
    CHECK(data.GetString("missing", "x") == "x");

    // Keys are checked: these are ignored (and logged).
    data.SetInt("has space", 1);
    data.SetInt("", 1);
    data.SetInt("a=b", 1);
    CHECK(data.GetValues().size() == 4);

    data.Rename("score", "stats.score");
    CHECK(!data.Has("score") && data.GetInt("stats.score") == -42);
    data.Remove("muted");
    CHECK(!data.Has("muted"));
}

TEST(SaveFormatRoundTrip)
{
    SaveData data = Settings(0.75f, true);
    data.SetString("note", "line 1\nline 2\r\\end");
    const std::string text = SaveSystem::Serialize(data, 3, 1791300000, "Options");
    CHECK(text.starts_with("EMERALD-SAVE 1\nversion = 3\nsaved = 1791300000\nsummary = Options\n"
                           "crc32 = "));
    CHECK(text.find("\n---\naudio.master = 0.75\n") != std::string::npos); // sorted keys
    CHECK(text.find("note = line 1\\nline 2\\r\\\\end\n") != std::string::npos);

    const SaveSystem saves(TempFolder("format"), 3);
    const LoadResult loaded = saves.Parse(text);
    CHECK(loaded && loaded.FileVersion == 3);
    CHECK(loaded.Data.GetValues() == data.GetValues());

    // Written by hand (no checksum, CRLF line endings, no spaces): still reads.
    const LoadResult manual =
        saves.Parse("EMERALD-SAVE 1\r\nversion=3\r\n---\r\n# a comment\r\nlives=5\r\n\r\n");
    CHECK(manual && manual.Data.GetInt("lives") == 5);
}

TEST(SaveRoundTripOnDisk)
{
    const fs::path folder = TempFolder("disk");
    SaveSystem saves(folder, 1);
    CHECK(saves.Load("settings").Error == SaveError::NotFound);
    CHECK(saves.Save("settings", Settings(0.5f, false), "first"));
    CHECK(fs::exists(folder / "settings.sav"));
    CHECK(!fs::exists(folder / "settings.sav.tmp"));

    LoadResult loaded = saves.Load("settings");
    CHECK(loaded && !loaded.FromBackup);
    CHECK(loaded.Data.GetFloat("audio.master") == 0.5f);
    CHECK(!loaded.Data.GetBool("video.fullscreen", true));
    CHECK(loaded.Data.GetString("player.name") == "Ervin");

    // Saving again replaces the file (rename over an existing one) and keeps the old as .bak.
    const std::string first = ReadText(folder / "settings.sav");
    CHECK(saves.Save("settings", Settings(0.25f, true), "second"));
    CHECK(ReadText(folder / "settings.sav.bak") == first);
    loaded = saves.Load("settings");
    CHECK(loaded && loaded.Data.GetFloat("audio.master") == 0.25f);
    CHECK(saves.GetInfo("settings").Summary == "second");
}

TEST(SaveMigrationChain)
{
    const fs::path folder = TempFolder("migrate");
    // Version 1 of the game: one "volume" and a "hiscore".
    SaveSystem v1(folder, 1);
    SaveData old;
    old.SetFloat("volume", 0.6f);
    old.SetInt("hiscore", 1200);
    CHECK(v1.Save("slot_1", old));

    // Version 3: v2 renamed "volume", v3 turned the single high score into a table.
    std::string steps;
    SaveSystem v3(folder, 3);
    v3.AddMigration(1, [&](SaveData& d) {
        steps += "1";
        d.Rename("volume", "audio.master");
    });
    v3.AddMigration(2, [&](SaveData& d) {
        steps += "2";
        d.SetInt("scores.count", 1);
        d.SetString("scores.0.name", "???");
        d.Rename("hiscore", "scores.0.score");
    });
    const LoadResult loaded = v3.Load("slot_1");
    CHECK(loaded && loaded.FileVersion == 1 && steps == "12");
    CHECK(loaded.Data.GetFloat("audio.master") == 0.6f);
    CHECK(loaded.Data.GetInt("scores.0.score") == 1200 && !loaded.Data.Has("hiscore"));

    // A v2 save only runs the second step; a current one none.
    steps.clear();
    CHECK(v3.Parse(SaveSystem::Serialize(Settings(1.0f, true), 2, 0, {})) && steps == "2");
    steps.clear();
    CHECK(v3.Parse(SaveSystem::Serialize(Settings(1.0f, true), 3, 0, {})) && steps.empty());

    // A gap in the chain is an error, not a half-migrated save.
    SaveSystem gap(folder, 3);
    gap.AddMigration(2, [](SaveData&) {});
    const LoadResult missing = gap.Load("slot_1");
    CHECK(missing.Error == SaveError::NoMigration && missing.Data.IsEmpty());
}

TEST(SaveNewerVersionRejected)
{
    const fs::path folder = TempFolder("newer");
    SaveSystem future(folder, 5);
    CHECK(future.Save("slot_1", Settings(1.0f, false)));
    const SaveSystem current(folder, 3);
    const LoadResult loaded = current.Load("slot_1");
    CHECK(loaded.Error == SaveError::TooNew && loaded.FileVersion == 5);
    CHECK(loaded.Message.find("5") != std::string::npos);
    CHECK(current.GetInfo("slot_1").Valid && current.GetInfo("slot_1").Version == 5);
    // An unknown container format is refused as well.
    CHECK(current.Parse("EMERALD-SAVE 2\nversion = 1\n---\n").Error == SaveError::Corrupt);
}

TEST(SaveCorruptAndTruncatedRejected)
{
    const SaveSystem saves(TempFolder("corrupt"), 2);
    const std::string text = SaveSystem::Serialize(Settings(0.3f, true), 2, 1791300000, "x");
    // Every cut-off version of the file is caught (no separator yet, or a checksum mismatch).
    for (usize length = 0; length < text.size(); ++length)
        CHECK(saves.Parse(std::string_view(text).substr(0, length)).Error == SaveError::Corrupt);
    CHECK(saves.Parse(text));

    // One changed byte in the data.
    std::string flipped = text;
    char& digit = flipped[flipped.size() - 3];
    digit = digit == '1' ? '2' : '1';
    CHECK(saves.Parse(flipped).Error == SaveError::Corrupt);
    CHECK(saves.Parse("not a save at all").Error == SaveError::Corrupt);
    CHECK(saves.Parse("EMERALD-SAVE 1\n---\nlives = 3\n").Error == SaveError::Corrupt); // version
    CHECK(saves.Parse("EMERALD-SAVE 1\nversion = 0\n---\n").Error == SaveError::Corrupt);
    CHECK(saves.Parse("EMERALD-SAVE 1\nversion = 1\n---\nno equals sign\n").Error ==
          SaveError::Corrupt);
}

TEST(SaveCorruptFileFallsBackToBackup)
{
    const fs::path folder = TempFolder("backup");
    SaveSystem saves(folder, 1);
    CHECK(saves.Save("slot_1", Settings(0.1f, false)));
    CHECK(saves.Save("slot_1", Settings(0.2f, false))); // .bak holds 0.1

    // Damage the save: cut it in half.
    const std::string text = ReadText(folder / "slot_1.sav");
    WriteText(folder / "slot_1.sav", text.substr(0, text.size() / 2));
    LoadResult loaded = saves.Load("slot_1");
    CHECK(loaded && loaded.FromBackup);
    CHECK(loaded.Data.GetFloat("audio.master") == 0.1f);
    CHECK(!saves.GetInfo("slot_1").Valid);

    // Saving over a damaged file must not replace the good backup with it.
    CHECK(saves.Save("slot_1", Settings(0.3f, false)));
    CHECK(SaveSystem(folder, 1)
              .Parse(ReadText(folder / "slot_1.sav.bak"))
              .Data.GetFloat("audio.master") == 0.1f);

    // Without a backup the error comes through.
    fs::remove(folder / "slot_1.sav.bak");
    WriteText(folder / "slot_1.sav", "garbage");
    loaded = saves.Load("slot_1");
    CHECK(loaded.Error == SaveError::Corrupt && !loaded.FromBackup);
}

TEST(SaveFailedWriteKeepsOldSave)
{
    const fs::path folder = TempFolder("failed");
    SaveSystem saves(folder, 1);
    CHECK(saves.Save("slot_1", Settings(0.4f, true), "old"));
    const std::string before = ReadText(folder / "slot_1.sav");

    // Make the temp file impossible to write: a folder (not empty, so it can't be removed) sits
    // where slot_1.sav.tmp would go. The save fails and the old one is untouched.
    fs::create_directories(folder / "slot_1.sav.tmp");
    WriteText(folder / "slot_1.sav.tmp" / "blocker", "x");
    CHECK(!saves.Save("slot_1", Settings(0.9f, false), "new"));
    CHECK(ReadText(folder / "slot_1.sav") == before);
    LoadResult loaded = saves.Load("slot_1");
    CHECK(loaded && !loaded.FromBackup && loaded.Data.GetFloat("audio.master") == 0.4f);
    fs::remove_all(folder / "slot_1.sav.tmp");

    // A crash halfway through writing leaves a partial .tmp behind: loading ignores it and the
    // next save replaces it.
    WriteText(folder / "slot_1.sav.tmp", before.substr(0, 20));
    loaded = saves.Load("slot_1");
    CHECK(loaded && loaded.Data.GetFloat("audio.master") == 0.4f);
    CHECK(saves.Save("slot_1", Settings(0.9f, false), "new"));
    CHECK(!fs::exists(folder / "slot_1.sav.tmp"));
    CHECK(saves.Load("slot_1").Data.GetFloat("audio.master") == 0.9f);
}

TEST(SaveSlotsIndependent)
{
    const fs::path folder = TempFolder("slots");
    SaveSystem saves(folder, 2);
    CHECK(saves.ListSlots().empty()); // no folder yet
    CHECK(saves.Save("slot_2", Settings(0.2f, false), "Level 2"));
    CHECK(saves.Save("slot_1", Settings(0.1f, false), "Level 1"));
    CHECK(saves.Save("settings", Settings(1.0f, true)));
    WriteText(folder / "notes.txt", "not a save"); // ignored by ListSlots

    const std::vector<SaveSlotInfo> slots = saves.ListSlots();
    CHECK(slots.size() == 3);
    if (slots.size() == 3) {
        CHECK(slots[0].Name == "settings" && slots[1].Name == "slot_1" &&
              slots[2].Name == "slot_2");
        CHECK(slots[1].Exists && slots[1].Valid && slots[1].Version == 2);
        CHECK(slots[1].Summary == "Level 1" && slots[2].Summary == "Level 2");
        CHECK(slots[1].SavedAt > 1700000000); // a real Unix time (after 2023)
    }
    CHECK(saves.Load("slot_1").Data.GetFloat("audio.master") == 0.1f);
    CHECK(saves.Load("slot_2").Data.GetFloat("audio.master") == 0.2f);

    // Deleting one slot leaves the others alone.
    CHECK(saves.Save("slot_2", Settings(0.22f, false))); // now with a .bak too
    CHECK(saves.Delete("slot_2"));
    CHECK(!saves.Exists("slot_2") && !fs::exists(folder / "slot_2.sav.bak"));
    CHECK(saves.Load("slot_2").Error == SaveError::NotFound);
    CHECK(saves.Load("slot_1").Data.GetFloat("audio.master") == 0.1f);
    CHECK(saves.ListSlots().size() == 2);
    const SaveSlotInfo none = saves.GetInfo("slot_9");
    CHECK(!none.Exists && !none.Valid);

    // Slot names can't leave the folder or carry odd characters.
    CHECK(!saves.Save("../escape", Settings(1.0f, true)));
    CHECK(!saves.Save("a b", Settings(1.0f, true)));
    CHECK(saves.Load("../escape").Error == SaveError::BadName);
    CHECK(!fs::exists(folder.parent_path() / "escape.sav"));
}

TEST(SaveHighScoreTable)
{
    // The shape a game like Rock Blaster would use: a count and numbered entries.
    struct Score {
        std::string Name;
        i32 Points;
    };
    const Score table[] = {{"ERV", 52000}, {"ACE", 31500}, {"BOB", 900}};
    SaveData data;
    data.SetInt("scores.count", 3);
    for (i32 i = 0; i < 3; ++i) {
        const std::string key = "scores." + std::to_string(i);
        data.SetString(key + ".name", table[i].Name);
        data.SetInt(key + ".points", table[i].Points);
    }
    SaveSystem saves(TempFolder("scores"), 1);
    CHECK(saves.Save("highscores", data));

    const LoadResult loaded = saves.Load("highscores");
    CHECK(loaded && loaded.Data.GetInt("scores.count") == 3);
    for (i32 i = 0; i < loaded.Data.GetInt("scores.count"); ++i) {
        const std::string key = "scores." + std::to_string(i);
        CHECK(loaded.Data.GetString(key + ".name") == table[i].Name);
        CHECK(loaded.Data.GetInt(key + ".points") == table[i].Points);
    }
}

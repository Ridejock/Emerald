// Tests for the log file sink in Emerald::Log.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <SDL3/SDL_filesystem.h>

#include <Emerald/Core/Log.h>
#include <Emerald/Core/Paths.h>

#include "Test.h"

namespace fs = std::filesystem;
using Emerald::Log;

namespace {

fs::path TempDir()
{
    const fs::path dir = fs::temp_directory_path() / "emerald-log-tests";
    fs::create_directories(dir);
    return dir;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(LogWritesToFile)
{
    const fs::path path = TempDir() / "nested" / "test.log"; // parent dirs are created
    fs::remove_all(TempDir() / "nested");

    Log::Init(path);
    CHECK(Log::GetFilePath() == path);
    EM_CORE_INFO("core message {}", 42);
    EM_WARN("client warning");
    Log::Shutdown(); // flushes

    const std::string text = ReadFile(path);
    CHECK(Contains(text, "[EMERALD] [info] core message 42"));
    CHECK(Contains(text, "[APP] [warning] client warning"));
    CHECK(!Contains(text, "\x1b[")); // no ANSI color codes in the file
}

TEST(LogFileIsTruncatedPerRun)
{
    const fs::path path = TempDir() / "truncate.log";
    Log::Init(path);
    EM_INFO("first run");
    Log::Shutdown();
    Log::Init(path);
    EM_INFO("second run");
    Log::Shutdown();

    const std::string text = ReadFile(path);
    CHECK(!Contains(text, "first run"));
    CHECK(Contains(text, "second run"));
}

TEST(LogFileFailureFallsBackToConsole)
{
    // A regular file where a directory is needed: the log file cannot be created.
    const fs::path blocker = TempDir() / "not-a-directory";
    std::ofstream(blocker) << "x";

    Log::Init(blocker / "test.log"); // must not throw
    CHECK(Log::Core() != nullptr);
    CHECK(Log::GetFilePath().empty());
    EM_INFO("still logging to the console");
    Log::Shutdown();
}

TEST(LogFileEmptyPathDisablesFile)
{
    Log::Init({});
    CHECK(Log::Core() != nullptr && Log::Client() != nullptr);
    CHECK(Log::GetFilePath().empty());
    Log::Shutdown();
}

TEST(LogRelativePathIsNextToExecutable)
{
    Log::Init("logs/EmeraldLogTests.log");
    const fs::path expected =
        fs::path(reinterpret_cast<const char8_t*>(SDL_GetBasePath())) / "logs/EmeraldLogTests.log";
    CHECK(Log::GetFilePath() == expected);
    EM_INFO("relative path");
    Log::Shutdown();
    CHECK(fs::exists(expected));
    fs::remove(expected);
}

TEST(PathsBaseAndPref)
{
    const fs::path base = Emerald::Paths::GetBasePath();
    CHECK(base == fs::path(reinterpret_cast<const char8_t*>(SDL_GetBasePath())));
    CHECK(fs::is_directory(base));

    // Creates a real per-user folder, so clean it up again.
    const fs::path pref = Emerald::Paths::GetPrefPath("EmeraldTests", "PathsTest");
    CHECK(!pref.empty() && fs::is_directory(pref));
    std::error_code error;
    fs::remove(pref, error);
    fs::remove(pref.parent_path().parent_path(), error); // pref ends with a separator
}

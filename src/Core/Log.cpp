#include "Emerald/Core/Log.h"

#include <exception>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Emerald {

std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
std::filesystem::path Log::s_FilePath;

namespace {

// Console and file use the same layout; %^ ... %$ marks the part the console colors.
constexpr const char* kConsolePattern = "%^[%T.%e] [%n] [%l]%$ %v";
constexpr const char* kFilePattern = "[%T.%e] [%n] [%l] %v";

// Relative paths are relative to the executable, not the working directory, so the log ends up
// in the same place no matter how the program was started (IDE, terminal, double-click).
std::filesystem::path ResolveLogPath(const std::filesystem::path& file)
{
    if (file.is_absolute())
        return file;
    const char* base = SDL_GetBasePath(); // UTF-8, owned by SDL; works before SDL_Init
    if (!base)
        return std::filesystem::absolute(file);
    // char8_t tells std::filesystem the string is UTF-8 (matters on Windows).
    return std::filesystem::path(reinterpret_cast<const char8_t*>(base)) / file;
}

} // namespace

void Log::Init(const std::filesystem::path& file)
{
    if (s_CoreLogger)
        return;

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern(kConsolePattern);
    std::vector<spdlog::sink_ptr> sinks{console};

    // Try to open the file sink. spdlog reports failures with exceptions; remember the reason and
    // report it once the (console-only) loggers exist.
    std::string fileError;
    if (!file.empty()) {
        try {
            const std::filesystem::path path = ResolveLogPath(file);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path()); // throws on failure
            // truncate = true: every run starts with a fresh file.
            auto fileSink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
            fileSink->set_pattern(kFilePattern);
            sinks.push_back(std::move(fileSink));
            s_FilePath = path;
        } catch (const std::exception& e) {
            fileError = e.what();
        }
    }

    s_CoreLogger = std::make_shared<spdlog::logger>("EMERALD", sinks.begin(), sinks.end());
    s_ClientLogger = std::make_shared<spdlog::logger>("APP", sinks.begin(), sinks.end());

    for (auto& logger : {s_CoreLogger, s_ClientLogger}) {
        logger->set_level(spdlog::level::trace);
        // Warnings and errors are written out immediately, so they survive a crash. Lower levels
        // are buffered and flushed on shutdown (or when the buffer fills).
        logger->flush_on(spdlog::level::warn);
    }

    if (!fileError.empty())
        EM_CORE_WARN("Could not open log file '{}', logging to the console only: {}", file.string(),
                     fileError);
    else if (!s_FilePath.empty())
        EM_CORE_INFO("Logging to {}", s_FilePath.string());
}

void Log::Shutdown()
{
    for (auto& logger : {s_CoreLogger, s_ClientLogger}) {
        if (logger)
            logger->flush();
    }
    s_ClientLogger.reset();
    s_CoreLogger.reset();
    s_FilePath.clear();
    spdlog::shutdown();
}

} // namespace Emerald

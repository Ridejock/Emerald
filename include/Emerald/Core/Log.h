#pragma once

#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>

namespace Emerald {

class Log {
public:
    // Default log file, relative to the executable's directory.
    static constexpr const char* DefaultFile = "logs/Emerald.log";

    // Creates the Core and Client loggers. Both write to the console (colored) and, unless
    // `file` is empty, to that file. A relative `file` is resolved against the executable's
    // directory (SDL_GetBasePath). The file is truncated on every run. If it cannot be opened,
    // a warning is printed and logging continues on the console only.
    static void Init(const std::filesystem::path& file = DefaultFile);
    // Flushes everything and destroys the loggers.
    static void Shutdown();

    static std::shared_ptr<spdlog::logger>& Core() { return s_CoreLogger; }
    static std::shared_ptr<spdlog::logger>& Client() { return s_ClientLogger; }

    // Absolute path of the log file, or empty if file logging is off (or failed).
    static const std::filesystem::path& GetFilePath() { return s_FilePath; }

private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
    static std::filesystem::path s_FilePath;
};

} // namespace Emerald

// Engine-internal logging
#define EM_CORE_TRACE(...) ::Emerald::Log::Core()->trace(__VA_ARGS__)
#define EM_CORE_INFO(...) ::Emerald::Log::Core()->info(__VA_ARGS__)
#define EM_CORE_WARN(...) ::Emerald::Log::Core()->warn(__VA_ARGS__)
#define EM_CORE_ERROR(...) ::Emerald::Log::Core()->error(__VA_ARGS__)

// Application logging
#define EM_TRACE(...) ::Emerald::Log::Client()->trace(__VA_ARGS__)
#define EM_INFO(...) ::Emerald::Log::Client()->info(__VA_ARGS__)
#define EM_WARN(...) ::Emerald::Log::Client()->warn(__VA_ARGS__)
#define EM_ERROR(...) ::Emerald::Log::Client()->error(__VA_ARGS__)

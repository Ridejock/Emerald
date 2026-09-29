#pragma once

#include <memory>

#include <spdlog/spdlog.h>

namespace Emerald {

class Log {
public:
    static void Init();
    static void Shutdown();

    static std::shared_ptr<spdlog::logger>& Core() { return s_CoreLogger; }
    static std::shared_ptr<spdlog::logger>& Client() { return s_ClientLogger; }

private:
    static std::shared_ptr<spdlog::logger> s_CoreLogger;
    static std::shared_ptr<spdlog::logger> s_ClientLogger;
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

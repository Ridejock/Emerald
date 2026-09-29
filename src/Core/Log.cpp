#include "Emerald/Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace Emerald {

std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

void Log::Init()
{
    if (s_CoreLogger)
        return;

    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern("%^[%T.%e] [%n] [%l]%$ %v");

    s_CoreLogger = std::make_shared<spdlog::logger>("EMERALD", sink);
    s_ClientLogger = std::make_shared<spdlog::logger>("APP", sink);

    for (auto& logger : {s_CoreLogger, s_ClientLogger}) {
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);
    }
}

void Log::Shutdown()
{
    s_ClientLogger.reset();
    s_CoreLogger.reset();
    spdlog::shutdown();
}

} // namespace Emerald

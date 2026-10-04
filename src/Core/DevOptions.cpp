#include "Emerald/Core/DevOptions.h"

#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "Emerald/Core/Log.h"

namespace Emerald {

DevOptions ParseDevOptions(std::span<char* const> args)
{
    DevOptions options;
    for (usize i = 1; i < args.size(); ++i) {
        if (!args[i])
            continue;
        // "--flag value" or "--flag=value".
        std::string_view flag(args[i]);
        std::optional<std::string_view> value;
        if (const usize equals = flag.find('='); flag.starts_with("--") && equals != flag.npos) {
            value = flag.substr(equals + 1);
            flag = flag.substr(0, equals);
        }
        const auto next = [&]() -> std::string_view {
            if (!value && i + 1 < args.size() && args[i + 1])
                value = args[++i];
            return value.value_or("");
        };

        if (flag == "--frames") {
            const std::string_view text = next();
            if (std::from_chars(text.data(), text.data() + text.size(), options.Frames).ec !=
                std::errc())
                EM_CORE_WARN("--frames needs a number, got '{}'", text);
        } else if (flag == "--capture-fps") {
            const std::string fps(next());
            const f64 rate = std::strtod(fps.c_str(), nullptr);
            if (rate > 0.0)
                options.CaptureFps = rate;
            else
                EM_CORE_WARN("--capture-fps needs a positive number, got '{}'", fps);
        } else if (flag == "--screenshot") {
            options.Screenshot = next();
        } else if (flag == "--capture") {
            options.CaptureDir = next();
        } else if (flag == "--record") {
            options.Record = next();
        } else if (flag == "--replay") {
            options.Replay = next();
        }
    }
    return options;
}

} // namespace Emerald

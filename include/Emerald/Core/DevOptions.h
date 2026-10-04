#pragma once

#include <filesystem>
#include <span>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Engine command line options for development and automated runs. Application reads them from
// ApplicationSpec::Args, so every game and the sandbox has them without code of its own:
//
//   --frames N          quit after N frames (wins over ApplicationSpec::MaxFrames)
//   --screenshot out.png  save one frame as PNG: the last one of a --frames run or a replay,
//                       otherwise frame 60
//   --capture dir       save every frame as dir/frame_000001.png, ... with a fixed frame time of
//                       1 / --capture-fps seconds (default 60), so the sequence plays at that rate
//                       however slow saving is (a replay keeps its recorded frame times)
//   --capture-fps N     the capture's frame rate
//   --record file       record the input actions (InputRecording.h) and save them on exit
//   --replay file       play a recording instead of the devices, frame-exact; quits at its end
//
// Values can also be written as --frames=N. Unknown arguments are left to the game.
struct DevOptions {
    u64 Frames = 0; // 0 = no limit
    std::filesystem::path Screenshot;
    std::filesystem::path CaptureDir;
    f64 CaptureFps = 60.0;
    std::filesystem::path Record;
    std::filesystem::path Replay;
};

// Reads the options above from main's argv (args[0], the program, is skipped).
[[nodiscard]] DevOptions ParseDevOptions(std::span<char* const> args);

} // namespace Emerald

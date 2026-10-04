#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

class Input;

// Input recording and replay at the action level: what the game asked Input for ("Jump" down,
// "MoveX" at 0.5), not raw keys, so a recording replays the same with other bindings or devices.
// Application does it all with --record file / --replay file (see DevOptions.h); these types are
// public for tests and tools.

// The actions and axes a recording covers, by name (as bound with Input::BindAction / BindAxis).
struct InputNames {
    std::vector<std::string> Actions;
    std::vector<std::string> Axes;
    bool operator==(const InputNames&) const = default;
};

// The state of every named action and axis at one moment: in one fixed step, or in a frame's
// OnUpdate (the two see different pressed / released edges, see ButtonStates.h).
struct InputSample {
    static constexpr u8 kDown = 1;
    static constexpr u8 kPressed = 2;
    static constexpr u8 kReleased = 4;
    std::vector<u8> Actions; // kDown | kPressed | kReleased per action, in InputNames order
    std::vector<f32> Axes;   // -1..1 per axis
    bool operator==(const InputSample&) const = default;
};

// A whole session: a header, then per frame its time, its OnUpdate sample and one sample per
// fixed step. Replaying the frame times gives the same fixed steps, so the game sees exactly the
// same input in exactly the same steps.
struct InputRecording {
    struct Frame {
        u64 Nanoseconds = 0; // the frame's elapsed time
        InputSample Update;  // what OnUpdate (and drawing) saw
        std::vector<InputSample> Steps;
        bool operator==(const Frame&) const = default;
    };

    std::string EngineVersion; // EMERALD_VERSION of the recording build
    u64 Seed = 0;              // Application::GetSeed() of the session
    f64 FixedRate = 120.0;     // steps per second; a replay needs the same rate
    InputNames Names;
    std::vector<Frame> Frames;
    bool operator==(const InputRecording&) const = default;

    // A small text format, one line per sample (names must not contain spaces):
    //   emerald-input 1
    //   engine 0.1.0
    //   seed 1234
    //   fixed-rate 120
    //   actions Jump Fire
    //   axes MoveX MoveY
    //   frame 16666667 10 0 0      <- ns, then each action's flags (one digit), then the axes
    //   step 30 1 0
    //   step 10 1 0
    [[nodiscard]] std::string ToText() const;
    // nullopt (and the reason in `error`) if the text is not a recording.
    [[nodiscard]] static std::optional<InputRecording> FromText(std::string_view text,
                                                                std::string* error = nullptr);
    bool Save(const std::filesystem::path& path) const; // logs failures
    [[nodiscard]] static std::optional<InputRecording> Load(const std::filesystem::path& path);
};

// Drives recording or replay through a main loop. Application uses it; tests can drive it the
// same way:
//
//   if (!session.BeginFrame(elapsedNs)) quit;  // a replay uses (and changes) the frame time
//   for each fixed step: devices' BeginFixedStep(); session.BeginStep(); OnFixedUpdate(dt) ...
//   session.BeginUpdate(); OnUpdate(dt) ...
//
// While replaying, the Input answers from the recording instead of the devices.
class InputSession {
public:
    // Records from `input`, covering the actions and axes bound right now.
    void StartRecording(Input& input, std::string engineVersion, u64 seed, f64 fixedRate);
    // Replays into `input`. False (logged) if the recording was made at another fixed rate.
    bool StartReplay(Input& input, InputRecording recording, f64 fixedRate);
    void Stop(); // the Input reads its devices again

    // A new frame. Recording: remembers `elapsedNs`. Replaying: sets it to the recorded time;
    // returns false when the recording is over.
    bool BeginFrame(u64& elapsedNs);
    void BeginStep();   // before each OnFixedUpdate
    void BeginUpdate(); // before OnUpdate

    [[nodiscard]] bool IsRecording() const { return m_Mode == Mode::Record; }
    [[nodiscard]] bool IsReplaying() const { return m_Mode == Mode::Replay; }
    [[nodiscard]] const InputRecording& GetRecording() const { return m_Recording; }
    // Replay: frames played so far, and whether a frame ran a different number of fixed steps
    // than recorded (the replay is then no longer exact; logged once).
    [[nodiscard]] usize GetFrameIndex() const { return m_Frame; }
    [[nodiscard]] bool HasDiverged() const { return m_Diverged; }

private:
    enum class Mode : u8 { Off, Record, Replay };
    void CheckStepCount(); // replay: the last frame ran as many steps as recorded

    Mode m_Mode = Mode::Off;
    Input* m_Input = nullptr;
    InputRecording m_Recording;
    usize m_Frame = 0; // replay: the next frame to play
    usize m_Step = 0;  // replay: the next step in the current frame
    bool m_Started = false;
    bool m_Diverged = false;
    InputSample m_Empty; // replay: stands in for steps beyond the recording
};

} // namespace Emerald

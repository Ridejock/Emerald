// Dev tools: the engine's command line options (DevOptions.h), input recordings (their text
// format and errors), Input answering from a replayed sample, and a recorded platformer session
// replayed twice giving bit-identical positions.

#include <bit>
#include <optional>
#include <string>
#include <vector>

#include <Emerald/Core/DevOptions.h>
#include <Emerald/Core/FixedTimestep.h>
#include <Emerald/Core/Log.h>
#include <Emerald/Input/Gamepads.h>
#include <Emerald/Input/Input.h>
#include <Emerald/Input/InputRecording.h>
#include <Emerald/Input/Keyboard.h>
#include <Emerald/Physics/Platformer.h>
#include <Emerald/Tilemap/Tilemap.h>

#include "Test.h"

using namespace Emerald;

namespace {

// ParseDevOptions on a command line given as strings (argv needs mutable char pointers).
DevOptions Parse(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (std::string& arg : args)
        argv.push_back(arg.data());
    return ParseDevOptions(argv);
}

// The sandbox platformer's bindings.
void BindPlatformer(Input& input)
{
    input.BindAxis("MoveX", Key::A, Key::D);
    input.BindAxis("MoveY", Key::W, Key::S);
    input.BindAction("Jump", {Key::Space});
}

// What the scripted player does in a frame: holds keys down and lets go of them.
void Script(Keyboard& keyboard, u64 frame)
{
    const auto hold = [&](SDL_Scancode key, bool down) {
        if (down)
            keyboard.OnKeyDown(key);
        else
            keyboard.OnKeyUp(key);
    };
    hold(SDL_SCANCODE_D, (frame >= 10 && frame < 220) || frame >= 300);
    hold(SDL_SCANCODE_A, frame >= 230 && frame < 280);
    // Taps and longer holds (variable jump height), one tap within a single frame.
    hold(SDL_SCANCODE_SPACE, (frame >= 40 && frame < 43) || (frame >= 90 && frame < 130) ||
                                 (frame >= 250 && frame < 262) || frame % 37 == 0);
    if (frame == 160) { // pressed and released before the frame runs
        keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
        keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    }
    hold(SDL_SCANCODE_S, frame >= 320 && frame < 340);
}

// Uneven frame times, 4 to 28 ms, from a small fixed-seed generator.
u64 NextFrameNs(u32& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return 4'000'000ull + (seed >> 8) % 24'000'000ull;
}

// Runs the sandbox's platformer step on the sample map through a main loop like Application's
// and returns the body's position after every fixed step. With a script it plays `frames` live
// frames (record them with `session`); without one it runs until the session's replay ends.
std::vector<Vec2> Play(const Tilemap& map, Input& input, Keyboard& keyboard, Gamepads& gamepads,
                       InputSession& session, bool live, u64 frames)
{
    const MapObject* spawn = map.FindObject("spawn");
    const Vec2 start = spawn ? spawn->Position : Vec2(64.0f, 200.0f);
    PlatformerBody body{.Position = start - Vec2(0.0f, 7.0f), .HalfSize = {5.0f, 7.0f}};
    const PlatformerTunables tunables;
    FixedTimestep timestep(120.0, 8);
    const f32 dt = timestep.GetStepSeconds();

    std::vector<Vec2> positions;
    u32 seed = 99u;
    for (u64 frame = 0; !live || frame < frames; ++frame) {
        keyboard.BeginFrame();
        gamepads.BeginFrame();
        if (live)
            Script(keyboard, frame);
        u64 elapsedNs = NextFrameNs(seed);
        if (!session.BeginFrame(elapsedNs))
            break; // the replay is over
        const u32 steps = timestep.Advance(elapsedNs);
        for (u32 step = 0; step < steps; ++step) {
            keyboard.BeginFixedStep();
            gamepads.BeginFixedStep();
            session.BeginStep();
            const PlatformerInput in{.Move = input.GetAxis("MoveX"),
                                     .JumpPressed = input.WasActionPressed("Jump"),
                                     .JumpHeld = input.IsActionDown("Jump"),
                                     .Down = input.GetAxis("MoveY") > 0.5f};
            StepPlatformer(body, in, tunables, map, dt);
            positions.push_back(body.Position);
            keyboard.EndFixedStep();
            gamepads.EndFixedStep();
        }
        session.BeginUpdate();
    }
    return positions;
}

// Exactly the same floats, bit for bit (not just ==, which treats 0 and -0 alike).
bool BitIdentical(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    if (a.size() != b.size())
        return false;
    for (usize i = 0; i < a.size(); ++i) {
        if (std::bit_cast<u32>(a[i].x) != std::bit_cast<u32>(b[i].x) ||
            std::bit_cast<u32>(a[i].y) != std::bit_cast<u32>(b[i].y))
            return false;
    }
    return true;
}

} // namespace

TEST(DevOptionsParse)
{
    Log::Init({});
    const DevOptions none = Parse({"game"});
    CHECK(none.Frames == 0 && none.Screenshot.empty() && none.Replay.empty());

    const DevOptions options =
        Parse({"game", "--frames", "120", "--screenshot=out.png", "--demo", "--capture", "shots",
               "--capture-fps=30", "--record", "run.txt", "--replay=old.txt"});
    CHECK(options.Frames == 120);
    CHECK(options.Screenshot == "out.png");
    CHECK(options.CaptureDir == "shots");
    CHECK(options.CaptureFps == 30.0);
    CHECK(options.Record == "run.txt");
    CHECK(options.Replay == "old.txt");

    // Bad numbers are ignored (with a warning); a flag at the end has no value.
    const DevOptions bad = Parse({"game", "--frames", "lots", "--capture-fps", "-5", "--record"});
    CHECK(bad.Frames == 0);
    CHECK(bad.CaptureFps == 60.0);
    CHECK(bad.Record.empty());
}

TEST(RecordingTextRoundTrip)
{
    Log::Init({});
    InputRecording recording{.EngineVersion = "0.1.0",
                             .Seed = 18446744073709551615ull,
                             .FixedRate = 120.0,
                             .Names = {{"Jump", "Fire"}, {"MoveX"}},
                             .Frames = {}};
    const InputSample a{{InputSample::kDown | InputSample::kPressed, 0}, {0.1f}};
    const InputSample b{{InputSample::kReleased, InputSample::kDown}, {-0.333333343f}};
    recording.Frames.push_back({16'666'667, a, {a, b}});
    recording.Frames.push_back({8'000'000, b, {}});
    recording.Frames.push_back({1, {{0, 0}, {1e-8f}}, {b}});

    std::string error;
    const std::optional<InputRecording> back = InputRecording::FromText(recording.ToText(), &error);
    CHECK(back.has_value());
    CHECK(back && *back == recording); // floats survive the text exactly
    CHECK(error.empty());
}

TEST(RecordingTextErrors)
{
    Log::Init({});
    const std::string header = "emerald-input 1\nengine 0.1.0\nseed 1\nfixed-rate 120\n"
                               "actions Jump\naxes MoveX\n";
    std::string error;
    CHECK(InputRecording::FromText(header + "frame 10 1 0.5\nstep 3 0\n", &error).has_value());
    CHECK(!InputRecording::FromText("hello\n", &error).has_value());
    CHECK(!error.empty());
    CHECK(!InputRecording::FromText("emerald-input 2\n" + header.substr(16), &error));
    CHECK(!InputRecording::FromText(header + "step 1 0\n", &error));      // a step before any frame
    CHECK(!InputRecording::FromText(header + "frame 10 12 0\n", &error)); // two actions' flags
    CHECK(!InputRecording::FromText(header + "frame 10 9 0\n", &error));  // not a flags digit
    CHECK(!InputRecording::FromText(header + "frame 10 1\n", &error));    // missing axis
    CHECK(!InputRecording::FromText(header + "frame x 1 0\n", &error));   // bad time
    CHECK(!InputRecording::FromText(header + "jump 1 0\n", &error));      // unknown line
}

TEST(InputAnswersFromReplay)
{
    Keyboard keyboard;
    Gamepads gamepads;
    Input live(keyboard, gamepads);
    BindPlatformer(live);
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    keyboard.OnKeyDown(SDL_SCANCODE_D);

    const InputNames names = live.GetNames();
    CHECK(names.Actions == std::vector<std::string>{"Jump"});
    CHECK(names.Axes.size() == 2);
    const InputSample sample = live.Capture(names);

    // Another Input with nothing pressed (and other bindings) answers the same from the sample.
    Keyboard idle;
    Input replay(idle, gamepads);
    replay.BindAction("Jump", {Key::Z});
    CHECK(!replay.IsActionDown("Jump"));
    replay.SetReplay(&sample, &names);
    CHECK(replay.IsReplaying());
    CHECK(replay.IsActionDown("Jump") && replay.WasActionPressed("Jump"));
    CHECK(!replay.WasActionReleased("Jump"));
    CHECK(replay.GetAxis("MoveX") == live.GetAxis("MoveX"));
    CHECK(replay.GetAxis("MoveX") == 1.0f && replay.GetAxis("MoveY") == 0.0f);
    CHECK(!replay.IsActionDown("Fire")); // not in the recording

    // Blocking (a scene without input focus) still wins.
    replay.SetBlocked(true);
    CHECK(!replay.IsActionDown("Jump") && replay.GetAxis("MoveX") == 0.0f);
    replay.SetBlocked(false);

    replay.SetReplay(nullptr, nullptr); // back to the devices
    CHECK(!replay.IsReplaying() && !replay.IsActionDown("Jump"));
}

TEST(PlatformerReplayIsBitIdentical)
{
    Log::Init({});
    const std::optional<Tilemap> map =
        Tilemap::Load(nullptr, std::filesystem::path(EMERALD_TEST_TILEMAPS) / "platformer.tmj");
    if (!map) {
        CHECK(false);
        return;
    }

    // A live, scripted session, recorded.
    Keyboard keyboard;
    Gamepads gamepads;
    Input input(keyboard, gamepads);
    BindPlatformer(input);
    InputSession recorder;
    recorder.StartRecording(input, "test", 7, 120.0);
    const std::vector<Vec2> live = Play(*map, input, keyboard, gamepads, recorder, true, 400);
    recorder.Stop();
    CHECK(recorder.GetRecording().Frames.size() == 400);
    CHECK(live.size() > 400); // frames of 4 to 28 ms: 0 to 4 steps each
    CHECK(!live.empty() && live.back().x > live.front().x + 100.0f); // it went somewhere
    CHECK(!live.empty() && live.back().y != live.front().y);

    // Through the text format, as --record / --replay do.
    const std::optional<InputRecording> loaded =
        InputRecording::FromText(recorder.GetRecording().ToText());
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    CHECK(loaded->Seed == 7 && loaded->FixedRate == 120.0);

    // Replayed twice into fresh Inputs whose keyboards nobody touches.
    std::vector<Vec2> replays[2];
    for (std::vector<Vec2>& positions : replays) {
        Keyboard idle;
        Input replay(idle, gamepads);
        BindPlatformer(replay);
        InputSession session;
        CHECK(session.StartReplay(replay, *loaded, 120.0));
        positions = Play(*map, replay, idle, gamepads, session, false, 0);
        CHECK(!session.HasDiverged());
        CHECK(session.GetFrameIndex() == 400);
    }
    CHECK(BitIdentical(replays[0], replays[1]));
    CHECK(BitIdentical(replays[0], live));

    // A replay needs the recording's fixed rate.
    Keyboard idle;
    Input other(idle, gamepads);
    InputSession wrongRate;
    CHECK(!wrongRate.StartReplay(other, *loaded, 60.0));
}

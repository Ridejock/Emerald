// Tests for the keyboard edge tracking in Input and the FixedTimestep accumulator.

#include <Emerald/Core/FixedTimestep.h>
#include <Emerald/Input/Input.h>

#include "Test.h"

using namespace Emerald;

TEST(InputPressAndRelease)
{
    Input input;
    input.BeginFrame();
    input.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(input.IsKeyDown(Key::Space));
    CHECK(input.WasKeyPressed(Key::Space));
    CHECK(!input.WasKeyReleased(Key::Space));
    CHECK(!input.IsKeyDown(Key::W));

    // Next frame: still held, but no longer "pressed".
    input.BeginFrame();
    CHECK(input.IsKeyDown(Key::Space));
    CHECK(!input.WasKeyPressed(Key::Space));

    input.BeginFrame();
    input.OnKeyUp(SDL_SCANCODE_SPACE);
    CHECK(!input.IsKeyDown(Key::Space));
    CHECK(input.WasKeyReleased(Key::Space));
}

TEST(InputTapWithinOneFrame)
{
    // Down and up before the frame is processed: not held, but the press is not lost.
    Input input;
    input.BeginFrame();
    input.OnKeyDown(SDL_SCANCODE_A);
    input.OnKeyUp(SDL_SCANCODE_A);
    CHECK(!input.IsKeyDown(Key::A));
    CHECK(input.WasKeyPressed(Key::A));
    CHECK(input.WasKeyReleased(Key::A));
}

TEST(InputIgnoresDuplicateEvents)
{
    Input input;
    input.OnKeyDown(SDL_SCANCODE_W);
    input.BeginFrame();
    input.OnKeyDown(SDL_SCANCODE_W); // already down: not a new press
    CHECK(!input.WasKeyPressed(Key::W));
    input.OnKeyUp(SDL_SCANCODE_S); // was never down
    CHECK(!input.WasKeyReleased(Key::S));
}

TEST(InputFixedStepEdges)
{
    Input input;

    // Frame 1 runs no fixed step: the press is visible to OnUpdate...
    input.BeginFrame();
    input.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(input.WasKeyPressed(Key::Space));

    // ...and frame 2's first fixed step still sees it, the second one does not.
    input.BeginFrame();
    CHECK(!input.WasKeyPressed(Key::Space)); // per-frame edge is gone
    input.BeginFixedStep();
    CHECK(input.WasKeyPressed(Key::Space));
    CHECK(input.IsKeyDown(Key::Space));
    input.EndFixedStep();
    input.BeginFixedStep();
    CHECK(!input.WasKeyPressed(Key::Space));
    CHECK(input.IsKeyDown(Key::Space));
    input.EndFixedStep();
}

TEST(InputReleaseAll)
{
    Input input;
    input.OnKeyDown(SDL_SCANCODE_LEFT);
    input.OnKeyDown(SDL_SCANCODE_UP);
    input.BeginFrame();
    input.ReleaseAll();
    CHECK(!input.IsKeyDown(Key::Left) && !input.IsKeyDown(Key::Up));
    CHECK(input.WasKeyReleased(Key::Left) && input.WasKeyReleased(Key::Up));
    CHECK(!input.WasKeyReleased(Key::Down));
}

TEST(FixedTimestepAccumulates)
{
    FixedTimestep timestep(100.0, 8); // 10 ms steps
    CHECK(timestep.GetStepNanoseconds() == 10'000'000);
    CHECK_NEAR(timestep.GetStepSeconds(), 0.01f);

    CHECK(timestep.Advance(4'000'000) == 0); // 4 ms: not enough yet
    CHECK_NEAR(timestep.GetAlpha(), 0.4f);
    CHECK(timestep.Advance(7'000'000) == 1); // 11 ms total: one step, 1 ms left
    CHECK_NEAR(timestep.GetAlpha(), 0.1f);
    CHECK(timestep.Advance(29'000'000) == 3); // 30 ms: three steps, nothing left
    CHECK_NEAR(timestep.GetAlpha(), 0.0f);
}

TEST(FixedTimestepAverageRate)
{
    // 144 fps against 120 Hz: exactly 120 steps per simulated second, some frames run none.
    FixedTimestep timestep(120.0, 8);
    const u64 frameNs = 1'000'000'000 / 144;
    u32 total = 0;
    u32 framesWithoutStep = 0;
    for (u32 frame = 0; frame < 144 * 10; ++frame) {
        const u32 steps = timestep.Advance(frameNs);
        total += steps;
        framesWithoutStep += steps == 0 ? 1 : 0;
    }
    CHECK(total >= 1199 && total <= 1200);
    CHECK(framesWithoutStep > 0);
}

TEST(FixedTimestepClampsSlowFrames)
{
    FixedTimestep timestep(100.0, 4);
    // A 1 s hitch would need 100 steps: only 4 run, and the backlog is dropped.
    CHECK(timestep.Advance(1'000'000'000) == 4);
    CHECK(timestep.GetAlpha() < 1.0f);
    CHECK(timestep.Advance(10'000'000) == 1); // back to normal right away
}

TEST(FixedTimestepInvalidArguments)
{
    FixedTimestep timestep(0.0, 0); // falls back to 120 Hz and 1 step
    CHECK(timestep.GetStepNanoseconds() == 8'333'333);
    CHECK(timestep.Advance(1'000'000'000) == 1);
}

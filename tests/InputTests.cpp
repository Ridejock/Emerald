// Tests for the keyboard edge tracking, action-based Input on top of it, saving its bindings,
// and the FixedTimestep accumulator.

#include <algorithm>
#include <array>
#include <cstdio>

#include <Emerald/Core/FixedTimestep.h>
#include <Emerald/Core/Log.h>
#include <Emerald/Input/Input.h>
#include <Emerald/Input/InputBindings.h>
#include <Emerald/Input/Keyboard.h>
#include <Emerald/Input/Mouse.h>

#include "Test.h"

using namespace Emerald;

TEST(KeyboardPressAndRelease)
{
    Keyboard keyboard;
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(keyboard.IsKeyDown(Key::Space));
    CHECK(keyboard.WasKeyPressed(Key::Space));
    CHECK(!keyboard.WasKeyReleased(Key::Space));
    CHECK(!keyboard.IsKeyDown(Key::W));

    // Next frame: still held, but no longer "pressed".
    keyboard.BeginFrame();
    CHECK(keyboard.IsKeyDown(Key::Space));
    CHECK(!keyboard.WasKeyPressed(Key::Space));

    keyboard.BeginFrame();
    keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    CHECK(!keyboard.IsKeyDown(Key::Space));
    CHECK(keyboard.WasKeyReleased(Key::Space));
}

TEST(KeyboardTapWithinOneFrame)
{
    // Down and up before the frame is processed: not held, but the press is not lost.
    Keyboard keyboard;
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_A);
    keyboard.OnKeyUp(SDL_SCANCODE_A);
    CHECK(!keyboard.IsKeyDown(Key::A));
    CHECK(keyboard.WasKeyPressed(Key::A));
    CHECK(keyboard.WasKeyReleased(Key::A));
}

TEST(KeyboardIgnoresDuplicateEvents)
{
    Keyboard keyboard;
    keyboard.OnKeyDown(SDL_SCANCODE_W);
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_W); // already down: not a new press
    CHECK(!keyboard.WasKeyPressed(Key::W));
    keyboard.OnKeyUp(SDL_SCANCODE_S); // was never down
    CHECK(!keyboard.WasKeyReleased(Key::S));
}

TEST(KeyboardFixedStepEdges)
{
    Keyboard keyboard;

    // Frame 1 runs no fixed step: the press is visible to OnUpdate...
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(keyboard.WasKeyPressed(Key::Space));

    // ...and frame 2's first fixed step still sees it, the second one does not.
    keyboard.BeginFrame();
    CHECK(!keyboard.WasKeyPressed(Key::Space)); // per-frame edge is gone
    keyboard.BeginFixedStep();
    CHECK(keyboard.WasKeyPressed(Key::Space));
    CHECK(keyboard.IsKeyDown(Key::Space));
    keyboard.EndFixedStep();
    keyboard.BeginFixedStep();
    CHECK(!keyboard.WasKeyPressed(Key::Space));
    CHECK(keyboard.IsKeyDown(Key::Space));
    keyboard.EndFixedStep();
}

TEST(KeyboardReleaseAll)
{
    Keyboard keyboard;
    keyboard.OnKeyDown(SDL_SCANCODE_LEFT);
    keyboard.OnKeyDown(SDL_SCANCODE_UP);
    keyboard.BeginFrame();
    keyboard.ReleaseAll();
    CHECK(!keyboard.IsKeyDown(Key::Left) && !keyboard.IsKeyDown(Key::Up));
    CHECK(keyboard.WasKeyReleased(Key::Left) && keyboard.WasKeyReleased(Key::Up));
    CHECK(!keyboard.WasKeyReleased(Key::Down));
}

TEST(ActionsWithSeveralKeys)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    input.BindAction("Fire", {Key::Space, Key::J});

    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_J);
    CHECK(input.IsActionDown("Fire"));
    CHECK(input.WasActionPressed("Fire"));

    // Pressing the second key while the first is held is not a new press...
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(!input.WasActionPressed("Fire"));
    // ...and releasing one of two held keys is not a release.
    keyboard.BeginFrame();
    keyboard.OnKeyUp(SDL_SCANCODE_J);
    CHECK(input.IsActionDown("Fire"));
    CHECK(!input.WasActionReleased("Fire"));

    keyboard.BeginFrame();
    keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    CHECK(!input.IsActionDown("Fire"));
    CHECK(input.WasActionReleased("Fire"));

    // Unknown names are never active.
    CHECK(!input.IsActionDown("Jump") && !input.WasActionPressed("Jump"));
    CHECK(input.GetAxis("Nothing") == 0.0f);
}

TEST(ActionsWithMouseButtons)
{
    Keyboard keyboard;
    Gamepads pads;
    Mouse mouse;
    Input input(keyboard, pads, &mouse);
    input.BindAction("Fire", {Key::Space});
    input.BindAction("Fire", {MouseButton::Left});
    CHECK(input.GetActionMouseButtons("Fire").size() == 1);

    mouse.BeginFrame();
    mouse.OnButton(MouseButton::Left, true);
    CHECK(input.IsActionDown("Fire") && input.WasActionPressed("Fire"));
    // Space while the button is held is not a new press.
    keyboard.BeginFrame();
    mouse.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(!input.WasActionPressed("Fire"));
    keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    mouse.OnButton(MouseButton::Left, false);
    CHECK(!input.IsActionDown("Fire") && input.WasActionReleased("Fire"));

    // Rebinding replaces only the mouse buttons.
    input.RebindAction("Fire", {MouseButton::Right});
    mouse.BeginFrame();
    mouse.OnButton(MouseButton::Left, true);
    CHECK(!input.IsActionDown("Fire"));
    mouse.OnButton(MouseButton::Right, true);
    CHECK(input.IsActionDown("Fire"));
    CHECK(input.GetActionKeys("Fire").size() == 1);

    // Position and motion, and a focus loss releasing everything.
    CHECK(!mouse.HasMoved() && !mouse.IsInWindow());
    mouse.OnMotion({120.0f, 45.5f});
    CHECK(mouse.HasMoved() && mouse.IsInWindow());
    CHECK(input.GetMouse().GetPosition() == Vec2(120.0f, 45.5f));
    mouse.BeginFrame();
    CHECK(!mouse.HasMoved());
    mouse.ReleaseAll();
    CHECK(!input.IsActionDown("Fire"));

    // Without a mouse, mouse bindings are never down.
    Input noMouse(keyboard, pads);
    noMouse.BindAction("Fire", {MouseButton::Left});
    CHECK(!noMouse.IsActionDown("Fire"));
}

TEST(ActionsTapAndFixedSteps)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    input.BindAction("Fire", {Key::Space});

    // A tap within one frame that runs no fixed step reaches the next step exactly once.
    keyboard.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    CHECK(input.WasActionPressed("Fire") && input.WasActionReleased("Fire"));
    keyboard.BeginFrame();
    keyboard.BeginFixedStep();
    CHECK(input.WasActionPressed("Fire"));
    keyboard.EndFixedStep();
    keyboard.BeginFixedStep();
    CHECK(!input.WasActionPressed("Fire"));
    keyboard.EndFixedStep();
}

TEST(ActionsAxis)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    input.BindAxis("Rotate", Key::A, Key::D);
    input.BindAxis("Rotate", Key::Left, Key::Right);

    CHECK(input.GetAxis("Rotate") == 0.0f);
    keyboard.OnKeyDown(SDL_SCANCODE_A);
    CHECK(input.GetAxis("Rotate") == -1.0f);
    keyboard.OnKeyDown(SDL_SCANCODE_LEFT); // two negative keys still give -1
    CHECK(input.GetAxis("Rotate") == -1.0f);
    keyboard.OnKeyDown(SDL_SCANCODE_RIGHT); // opposite keys cancel out
    CHECK(input.GetAxis("Rotate") == 0.0f);
    keyboard.OnKeyUp(SDL_SCANCODE_A);
    keyboard.OnKeyUp(SDL_SCANCODE_LEFT);
    CHECK(input.GetAxis("Rotate") == 1.0f);
}

TEST(ActionsRebind)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    input.BindAction("Thrust", {Key::W, Key::Up});
    input.BindAction("Thrust", {Key::W}); // duplicates are ignored
    CHECK(input.GetActionKeys("Thrust").size() == 2);

    input.RebindAction("Thrust", {Key::K});
    CHECK(input.GetActionKeys("Thrust").size() == 1 && input.GetActionKeys("Thrust")[0] == Key::K);
    keyboard.OnKeyDown(SDL_SCANCODE_W);
    CHECK(!input.IsActionDown("Thrust"));
    keyboard.OnKeyDown(SDL_SCANCODE_K);
    CHECK(input.IsActionDown("Thrust"));

    input.BindAxis("Turn", Key::A, Key::D);
    input.RebindAxis("Turn", Key::J, Key::L);
    keyboard.OnKeyDown(SDL_SCANCODE_D);
    CHECK(input.GetAxis("Turn") == 0.0f); // D is no longer bound
    keyboard.OnKeyDown(SDL_SCANCODE_L);
    CHECK(input.GetAxis("Turn") == 1.0f);

    input.Unbind("Thrust");
    CHECK(!input.IsActionDown("Thrust") && input.GetActionKeys("Thrust").empty());
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

// --- Saving bindings (InputBindings.h) ---

TEST(InputBindingNamesRoundTrip)
{
    CHECK(GetKeyName(Key::Space) == "Space");
    CHECK(GetKeyName(Key::LeftShift) == "Left Shift");
    CHECK(FindKey("Left Shift") == Key::LeftShift);
    CHECK(FindKey(",") == Key(SDL_SCANCODE_COMMA));
    for (u32 code = 1; code < SDL_SCANCODE_COUNT; ++code) {
        const Key key = Key(code);
        if (FindKey(GetKeyName(key)) != key) {
            std::printf("  key %u: '%s' does not read back\n", code, GetKeyName(key).c_str());
            CHECK(false);
        }
    }
    CHECK(!FindKey("NoSuchKey") && !FindKey("#99999") && !FindKey("#1x") && !FindKey("#0"));

    for (usize i = 0; i < static_cast<usize>(GamepadButton::Count); ++i) {
        const auto button = static_cast<GamepadButton>(i);
        CHECK(!GetGamepadButtonName(button).empty());
        CHECK(FindGamepadButton(GetGamepadButtonName(button)) == button);
    }
    CHECK(GetGamepadButtonName(GamepadButton::South) == "South");
    CHECK(GetGamepadButtonName(GamepadButton::LeftStickUp) == "LeftStickUp");
    CHECK(FindMouseButton(GetMouseButtonName(MouseButton::X2)) == MouseButton::X2);
    CHECK(!FindGamepadButton("Cross") && !FindMouseButton("Wheel"));
}

TEST(InputBindingsSaveAndLoad)
{
    Log::Init({}); // unknown names are logged
    constexpr std::array<std::string_view, 3> kRebindable = {"Jump", "Fire", "Pause"};
    const auto bindDefaults = [](Input& input) {
        input.BindAction("Jump", {Key::Space, Key::Z});
        input.BindAction("Jump", {GamepadButton::South});
        input.BindAction("Fire", {Key::C});
        input.BindAction("Fire", {MouseButton::Left});
        input.BindAction("Pause", {Key::Escape});
        input.BindAction("Pause", {GamepadButton::Start});
    };

    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    bindDefaults(input);
    input.RebindAction("Jump", {Key(SDL_SCANCODE_COMMA), Key::Up});
    input.RebindAction("Fire", {GamepadButton::RightTrigger});
    input.RebindAction("Pause", std::span<const Key>()); // keys cleared: pad only

    SaveData data;
    SaveBindings(input, data, kRebindable);
    CHECK(data.GetString("input.Jump.keys") == ",, Up"); // the comma key is ","
    CHECK(data.GetString("input.Jump.buttons") == "South");
    CHECK(data.GetString("input.Fire.buttons") == "RightTrigger");
    CHECK(data.GetString("input.Fire.mouse") == "Left");
    CHECK(data.Has("input.Pause.keys") && data.GetString("input.Pause.keys").empty());

    // Through a save file and back into a fresh Input with the defaults.
    const SaveSystem saves("unused", 1);
    const LoadResult loaded = saves.Parse(SaveSystem::Serialize(data, 1, 0, {}));
    CHECK(loaded);
    Input fresh(keyboard, pads);
    bindDefaults(fresh);
    fresh.BindAction("Hop", {Key::X}); // not in the list: untouched
    CHECK(LoadBindings(fresh, loaded.Data, kRebindable));
    const auto same = [](auto a, auto b) { return std::ranges::equal(a, b); };
    CHECK(same(fresh.GetActionKeys("Jump"), input.GetActionKeys("Jump")));
    CHECK(same(fresh.GetActionButtons("Fire"), input.GetActionButtons("Fire")));
    CHECK(same(fresh.GetActionMouseButtons("Fire"), input.GetActionMouseButtons("Fire")));
    CHECK(fresh.GetActionKeys("Pause").empty());
    CHECK(fresh.GetActionButtons("Pause").size() == 1);
    CHECK(fresh.GetActionKeys("Hop").size() == 1);

    // Missing values keep the defaults; unknown names are skipped and reported.
    SaveData partial;
    partial.SetString("settings.Jump.keys", "W, Bogus Key");
    Input other(keyboard, pads);
    bindDefaults(other);
    CHECK(!LoadBindings(other, partial, kRebindable, "settings"));
    CHECK(other.GetActionKeys("Jump").size() == 1 && other.GetActionKeys("Jump")[0] == Key::W);
    CHECK(other.GetActionButtons("Jump").size() == 1);
    CHECK(other.GetActionKeys("Fire").size() == 1 && other.GetActionKeys("Fire")[0] == Key::C);
}

TEST(InputPressedKeyAndButton)
{
    // "Press a key to rebind": whatever went down this frame.
    Keyboard keyboard;
    keyboard.BeginFrame();
    CHECK(!keyboard.GetPressedKey());
    keyboard.OnKeyDown(SDL_SCANCODE_SEMICOLON);
    CHECK(keyboard.GetPressedKey() == Key(SDL_SCANCODE_SEMICOLON));
    keyboard.BeginFrame();
    CHECK(!keyboard.GetPressedKey()); // still held, not a new press

    Gamepads pads;
    pads.AddPad(1, nullptr, "Test pad", SDL_GAMEPAD_TYPE_PS4);
    pads.BeginFrame();
    CHECK(!pads.GetPressedButton());
    pads.OnAxis(1, GamepadAxis::RightTrigger, 1.0f); // virtual buttons count too
    CHECK(pads.GetPressedButton() == GamepadButton::RightTrigger);
    pads.BeginFrame();
    pads.OnButton(1, GamepadButton::East, true);
    CHECK(pads.GetPressedButton() == GamepadButton::East);
}

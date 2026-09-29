// Tests for the gamepad state (deadzones, virtual buttons, several pads) and gamepad bindings in
// Input. Synthetic pads are registered with AddPad(id, nullptr, ...), so no hardware is needed.

#include <cstring>

#include <Emerald/Input/Gamepads.h>
#include <Emerald/Input/Input.h>

#include "Test.h"

using namespace Emerald;

namespace {

constexpr SDL_JoystickID kPad = 1;
constexpr SDL_JoystickID kOtherPad = 2;

bool Same(const char* a, const char* b)
{
    return std::strcmp(a, b) == 0;
}

} // namespace

TEST(DeadzonePerAxis)
{
    CHECK(ApplyDeadzone(0.1f, 0.2f) == 0.0f);
    CHECK(ApplyDeadzone(-0.2f, 0.2f) == 0.0f);
    // Rescaled: just past the deadzone is close to 0 (no jump), the middle maps to the middle.
    CHECK_NEAR(ApplyDeadzone(0.21f, 0.2f), 0.0125f);
    CHECK_NEAR(ApplyDeadzone(0.6f, 0.2f), 0.5f);
    CHECK_NEAR(ApplyDeadzone(-0.6f, 0.2f), -0.5f);
    CHECK_NEAR(ApplyDeadzone(1.0f, 0.2f), 1.0f);
    CHECK_NEAR(ApplyDeadzone(0.5f, 0.0f), 0.5f); // no deadzone: unchanged
}

TEST(DeadzoneRadial)
{
    CHECK(Length(ApplyRadialDeadzone({0.1f, 0.1f}, 0.2f)) == 0.0f);
    const Vec2 right = ApplyRadialDeadzone({0.6f, 0.0f}, 0.2f);
    CHECK_NEAR(right.x, 0.5f);
    CHECK_NEAR(right.y, 0.0f);
    // A small diagonal that per-axis would drop (each axis 0.15 < 0.2) still counts, and keeps
    // its direction.
    const Vec2 diagonal = ApplyRadialDeadzone({0.15f, 0.15f}, 0.2f);
    CHECK(diagonal.x > 0.0f && diagonal.y > 0.0f);
    CHECK_NEAR(diagonal.x, diagonal.y);
    const Vec2 full = ApplyRadialDeadzone({0.6f, 0.8f}, 0.2f); // length 1 stays length 1
    CHECK_NEAR(full.x, 0.6f);
    CHECK_NEAR(full.y, 0.8f);
}

TEST(GamepadAxesUseSettings)
{
    Gamepads pads;
    pads.AddPad(kPad, nullptr, "Test", SDL_GAMEPAD_TYPE_XBOXONE);
    pads.OnAxis(kPad, GamepadAxis::LeftX, 0.15f);
    pads.OnAxis(kPad, GamepadAxis::LeftY, 0.15f);
    CHECK(pads.GetAxis(GamepadAxis::LeftX) > 0.0f); // radial (default)
    CHECK_NEAR(pads.GetRawAxis(0, GamepadAxis::LeftX), 0.15f);

    GamepadSettings settings;
    settings.RadialDeadzone = false;
    pads.SetSettings(settings);
    CHECK(pads.GetAxis(GamepadAxis::LeftX) == 0.0f); // per axis: inside the deadzone

    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 0.05f); // trigger deadzone 0.1
    CHECK(pads.GetAxis(GamepadAxis::RightTrigger) == 0.0f);
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 1.0f);
    CHECK_NEAR(pads.GetAxis(GamepadAxis::RightTrigger), 1.0f);
}

TEST(GamepadVirtualButtons)
{
    Gamepads pads;
    pads.AddPad(kPad, nullptr, "Test", SDL_GAMEPAD_TYPE_PS4);
    // Trigger as a button: presses at the threshold (0.5 after the deadzone)...
    pads.BeginFrame();
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 0.4f);
    CHECK(!pads.IsButtonDown(GamepadButton::RightTrigger));
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 0.8f);
    CHECK(pads.WasButtonPressed(GamepadButton::RightTrigger));
    // ...and releases a bit below it, so noise around the threshold gives no extra presses.
    pads.BeginFrame();
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 0.52f);
    CHECK(pads.IsButtonDown(GamepadButton::RightTrigger));
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 0.3f);
    CHECK(pads.WasButtonReleased(GamepadButton::RightTrigger));

    // Stick directions (+Y is down).
    pads.OnAxis(kPad, GamepadAxis::LeftY, -0.9f);
    CHECK(pads.IsButtonDown(GamepadButton::LeftStickUp));
    CHECK(!pads.IsButtonDown(GamepadButton::LeftStickDown));
    // Real button events cannot fake virtual buttons.
    pads.OnButton(kPad, GamepadButton::LeftTrigger, true);
    CHECK(!pads.IsButtonDown(GamepadButton::LeftTrigger));
}

TEST(GamepadButtonsAndFixedSteps)
{
    Gamepads pads;
    pads.AddPad(kPad, nullptr, "Test", SDL_GAMEPAD_TYPE_XBOXONE);
    // A tap within a frame that runs no fixed step reaches the next step exactly once.
    pads.BeginFrame();
    pads.OnButton(kPad, GamepadButton::South, true);
    pads.OnButton(kPad, GamepadButton::South, false);
    CHECK(pads.WasButtonPressed(GamepadButton::South));
    pads.BeginFrame();
    pads.BeginFixedStep();
    CHECK(pads.WasButtonPressed(GamepadButton::South));
    CHECK(!pads.IsButtonDown(GamepadButton::South));
    pads.EndFixedStep();
    pads.BeginFixedStep();
    CHECK(!pads.WasButtonPressed(GamepadButton::South));
    pads.EndFixedStep();
}

TEST(GamepadSeveralPads)
{
    Gamepads pads;
    pads.AddPad(kPad, nullptr, "Xbox", SDL_GAMEPAD_TYPE_XBOXONE);
    pads.AddPad(kOtherPad, nullptr, "Switch", SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO);
    CHECK(pads.GetCount() == 2);

    // The axis with the largest magnitude wins.
    pads.OnAxis(kPad, GamepadAxis::LeftX, 0.6f);
    pads.OnAxis(kOtherPad, GamepadAxis::LeftX, -1.0f);
    CHECK_NEAR(pads.GetAxis(GamepadAxis::LeftX), -1.0f);
    CHECK(pads.GetLastUsedType() == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO);

    // A button is down while any pad holds it; unplugging a pad releases what it held.
    pads.BeginFrame();
    pads.OnButton(kPad, GamepadButton::East, true);
    pads.OnButton(kOtherPad, GamepadButton::East, true);
    pads.OnButton(kPad, GamepadButton::East, false);
    CHECK(pads.IsButtonDown(GamepadButton::East));
    pads.BeginFrame();
    pads.Close(kOtherPad);
    CHECK(pads.GetCount() == 1);
    CHECK(!pads.IsButtonDown(GamepadButton::East));
    CHECK(pads.WasButtonReleased(GamepadButton::East));
    CHECK_NEAR(pads.GetAxis(GamepadAxis::LeftX), 0.5f);
}

TEST(GamepadLabels)
{
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::South, SDL_GAMEPAD_TYPE_XBOXONE), "A"));
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::South, SDL_GAMEPAD_TYPE_PS4), "Cross"));
    // Nintendo prints B on the bottom button (and A on the right one).
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::South, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO),
               "B"));
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::East, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO),
               "A"));
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::North, SDL_GAMEPAD_TYPE_PS4), "Triangle"));
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::RightShoulder, SDL_GAMEPAD_TYPE_PS4), "R1"));
    CHECK(Same(
        GetGamepadButtonLabel(GamepadButton::RightTrigger, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO),
        "ZR"));
    CHECK(Same(GetGamepadButtonLabel(GamepadButton::Start, SDL_GAMEPAD_TYPE_XBOXONE), "Menu"));
    CHECK(Same(GetGamepadTypeName(SDL_GAMEPAD_TYPE_PS4), "PS4"));
}

TEST(ActionsWithGamepad)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    pads.AddPad(kPad, nullptr, "Test", SDL_GAMEPAD_TYPE_XBOXONE);
    input.BindAction("Fire", {Key::Space});
    input.BindAction("Fire", {GamepadButton::South, GamepadButton::RightTrigger});
    CHECK(input.GetActionButtons("Fire").size() == 2);

    keyboard.BeginFrame();
    pads.BeginFrame();
    pads.OnButton(kPad, GamepadButton::South, true);
    CHECK(input.IsActionDown("Fire") && input.WasActionPressed("Fire"));

    // Key pressed while the button is held: not a new press; releasing only one: no release.
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_SPACE);
    CHECK(!input.WasActionPressed("Fire"));
    pads.OnButton(kPad, GamepadButton::South, false);
    CHECK(input.IsActionDown("Fire") && !input.WasActionReleased("Fire"));
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyUp(SDL_SCANCODE_SPACE);
    CHECK(input.WasActionReleased("Fire"));

    // Trigger as a button.
    keyboard.BeginFrame();
    pads.BeginFrame();
    pads.OnAxis(kPad, GamepadAxis::RightTrigger, 1.0f);
    CHECK(input.WasActionPressed("Fire"));

    // Rebinding the keys keeps the pad buttons.
    input.RebindAction("Fire", {Key::J});
    CHECK(input.GetActionButtons("Fire").size() == 2);
}

TEST(AxisTakesLargestMagnitude)
{
    Keyboard keyboard;
    Gamepads pads;
    Input input(keyboard, pads);
    pads.AddPad(kPad, nullptr, "Test", SDL_GAMEPAD_TYPE_XBOXONE);
    input.BindAxis("Rotate", Key::A, Key::D);
    input.BindAxis("Rotate", GamepadButton::DPadLeft, GamepadButton::DPadRight);
    input.BindAxis("Rotate", GamepadAxis::LeftX);
    input.BindAxis("Up", GamepadAxis::LeftY, -1.0f); // flipped: stick up is +1

    pads.OnAxis(kPad, GamepadAxis::LeftX, 0.6f);
    CHECK_NEAR(input.GetAxis("Rotate"), 0.5f); // analog, after the deadzone
    keyboard.OnKeyDown(SDL_SCANCODE_A);
    CHECK_NEAR(input.GetAxis("Rotate"), -1.0f); // the full key press is larger
    keyboard.OnKeyUp(SDL_SCANCODE_A);
    pads.OnButton(kPad, GamepadButton::DPadRight, true);
    CHECK_NEAR(input.GetAxis("Rotate"), 1.0f);
    pads.OnButton(kPad, GamepadButton::DPadRight, false);
    pads.OnAxis(kPad, GamepadAxis::LeftX, 0.1f); // inside the deadzone
    CHECK(input.GetAxis("Rotate") == 0.0f);

    pads.OnAxis(kPad, GamepadAxis::LeftX, 0.0f);
    pads.OnAxis(kPad, GamepadAxis::LeftY, -1.0f);
    CHECK_NEAR(input.GetAxis("Up"), 1.0f);

    // Losing focus centers everything.
    pads.ReleaseAll();
    CHECK(input.GetAxis("Up") == 0.0f);
}

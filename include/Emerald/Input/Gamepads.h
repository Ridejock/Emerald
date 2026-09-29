#pragma once

#include <array>
#include <bitset>
#include <string>
#include <vector>

#include <SDL3/SDL_gamepad.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/ButtonStates.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// Gamepad buttons in SDL's standard layout, named by position so one set of bindings works on
// every pad: South is A on Xbox, Cross on PlayStation and B on Switch (see GetButtonLabel for
// what to show in prompts). The first values are SDL_GamepadButton; after them come "virtual"
// buttons made from analog inputs, so triggers and stick directions can be bound to actions.
// clang-format off
enum class GamepadButton : u8 {
    South = SDL_GAMEPAD_BUTTON_SOUTH, East = SDL_GAMEPAD_BUTTON_EAST,
    West = SDL_GAMEPAD_BUTTON_WEST, North = SDL_GAMEPAD_BUTTON_NORTH,
    Back = SDL_GAMEPAD_BUTTON_BACK, Guide = SDL_GAMEPAD_BUTTON_GUIDE,
    Start = SDL_GAMEPAD_BUTTON_START,
    LeftStick = SDL_GAMEPAD_BUTTON_LEFT_STICK, RightStick = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    LeftShoulder = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    RightShoulder = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    DPadUp = SDL_GAMEPAD_BUTTON_DPAD_UP, DPadDown = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    DPadLeft = SDL_GAMEPAD_BUTTON_DPAD_LEFT, DPadRight = SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    Misc1 = SDL_GAMEPAD_BUTTON_MISC1, Touchpad = SDL_GAMEPAD_BUTTON_TOUCHPAD,

    // Virtual: down while the (deadzoned) value is past GamepadSettings::ButtonThreshold.
    LeftTrigger = SDL_GAMEPAD_BUTTON_COUNT, RightTrigger,
    LeftStickUp, LeftStickDown, LeftStickLeft, LeftStickRight,
    RightStickUp, RightStickDown, RightStickLeft, RightStickRight,
    Count
};
// clang-format on

// Analog inputs. Sticks go from -1 to +1 (+X right, +Y DOWN, as SDL reports them); triggers from
// 0 (released) to 1 (fully pulled).
enum class GamepadAxis : u8 {
    LeftX = SDL_GAMEPAD_AXIS_LEFTX,
    LeftY = SDL_GAMEPAD_AXIS_LEFTY,
    RightX = SDL_GAMEPAD_AXIS_RIGHTX,
    RightY = SDL_GAMEPAD_AXIS_RIGHTY,
    LeftTrigger = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    RightTrigger = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
    Count
};

struct GamepadSettings {
    f32 StickDeadzone = 0.2f;   // stick values below this read as 0
    bool RadialDeadzone = true; // true: deadzone on the stick's length (smooth diagonals);
                                // false: on each axis separately (snaps to straight lines)
    f32 TriggerDeadzone = 0.1f;
    f32 ButtonThreshold = 0.5f; // virtual buttons (triggers, stick directions) press here
};

// Deadzone helpers (used by Gamepads, public for tests and custom use). Values inside the
// deadzone become 0, and the rest is rescaled so the output still runs smoothly from 0 to 1
// instead of jumping to `deadzone` at the edge.
[[nodiscard]] f32 ApplyDeadzone(f32 value, f32 deadzone);
[[nodiscard]] Vec2 ApplyRadialDeadzone(Vec2 stick, f32 deadzone);

// Names for UI: "Xbox One", "PS4", "Switch Pro", ...
[[nodiscard]] const char* GetGamepadTypeName(SDL_GamepadType type);
// What is printed on a button of that type of pad, for prompts: South is "A" on Xbox, "Cross" on
// PlayStation and "B" on Switch; RightShoulder is "RB", "R1" or "R".
[[nodiscard]] const char* GetGamepadButtonLabel(GamepadButton button, SDL_GamepadType type);

// All connected gamepads, fed from SDL events by the Application. Single-player for now: every
// connected pad drives the same buttons and axes (the largest value wins), so the player can pick
// up any of them. Games normally use actions (Input.h); this is the layer underneath. Buttons
// follow the same frame/fixed-step edge rules as keys (ButtonStates.h).
class Gamepads {
public:
    struct PadInfo {
        SDL_JoystickID Id = 0;
        std::string Name;
        SDL_GamepadType Type = SDL_GAMEPAD_TYPE_UNKNOWN;
    };

    Gamepads() = default;
    ~Gamepads();
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // --- Combined state of all pads ---
    [[nodiscard]] bool IsButtonDown(GamepadButton b) const { return m_Buttons.IsDown(Index(b)); }
    [[nodiscard]] bool WasButtonPressed(GamepadButton b) const
    {
        return m_Buttons.WasPressed(Index(b));
    }
    [[nodiscard]] bool WasButtonReleased(GamepadButton b) const
    {
        return m_Buttons.WasReleased(Index(b));
    }
    // Deadzoned value; with several pads the one with the largest magnitude.
    [[nodiscard]] f32 GetAxis(GamepadAxis axis) const;

    // --- Pads ---
    [[nodiscard]] usize GetCount() const { return m_Pads.size(); }
    [[nodiscard]] const PadInfo& GetInfo(usize pad) const { return m_Pads[pad].Info; }
    [[nodiscard]] f32 GetRawAxis(usize pad, GamepadAxis axis) const;
    [[nodiscard]] f32 GetAxis(usize pad, GamepadAxis axis) const; // deadzoned, one pad
    // Type of the pad that was used last (UNKNOWN without pads): pick button prompts with it.
    [[nodiscard]] SDL_GamepadType GetLastUsedType() const { return m_LastUsedType; }
    // Label of a button on the last used pad, e.g. GetButtonLabel(GamepadButton::South) == "Cross".
    [[nodiscard]] const char* GetButtonLabel(GamepadButton button) const
    {
        return GetGamepadButtonLabel(button, m_LastUsedType);
    }
    // Rumbles every connected pad; strengths 0..1 for the low (left, heavy) and high (right,
    // light) frequency motors. Pads without rumble ignore it. A new call replaces the old one.
    void Rumble(f32 low, f32 high, u32 milliseconds);

    [[nodiscard]] const GamepadSettings& GetSettings() const { return m_Settings; }
    void SetSettings(const GamepadSettings& settings) { m_Settings = settings; }

    // --- Called by the Application (or by tests) ---
    // Opens a pad reported by SDL_EVENT_GAMEPAD_ADDED (ignored if it is already open).
    void Open(SDL_JoystickID id);
    // Forgets a pad (SDL_EVENT_GAMEPAD_REMOVED); buttons it was holding get released.
    void Close(SDL_JoystickID id);
    void CloseAll();
    // Registers a pad without SDL (handle may be null): used by Open and by tests.
    void AddPad(SDL_JoystickID id, SDL_Gamepad* handle, std::string name, SDL_GamepadType type);
    void OnButton(SDL_JoystickID id, GamepadButton button, bool down);
    // `value`: -1..1 for sticks, 0..1 for triggers (SDL's integer values divided by 32767).
    void OnAxis(SDL_JoystickID id, GamepadAxis axis, f32 value);
    // Releases all buttons and centers all axes, e.g. when the window loses focus.
    void ReleaseAll();
    void BeginFrame() { m_Buttons.BeginFrame(); }
    void BeginFixedStep() { m_Buttons.BeginFixedStep(); }
    void EndFixedStep() { m_Buttons.EndFixedStep(); }

private:
    static constexpr usize kButtonCount = static_cast<usize>(GamepadButton::Count);
    static constexpr usize kAxisCount = static_cast<usize>(GamepadAxis::Count);

    struct Pad {
        PadInfo Info;
        SDL_Gamepad* Handle = nullptr;
        std::bitset<kButtonCount> Buttons; // this pad's buttons, virtual ones included
        std::array<f32, kAxisCount> Axes{};
    };

    [[nodiscard]] static usize Index(GamepadButton b) { return static_cast<usize>(b); }
    [[nodiscard]] Pad* Find(SDL_JoystickID id);
    [[nodiscard]] f32 Deadzoned(const Pad& pad, GamepadAxis axis) const;
    void SetPadButton(Pad& pad, usize button, bool down);
    void UpdateVirtualButtons(Pad& pad);
    void RefreshButton(usize button); // combined state = any pad holds it

    std::vector<Pad> m_Pads;
    ButtonStates<kButtonCount> m_Buttons;
    GamepadSettings m_Settings;
    SDL_GamepadType m_LastUsedType = SDL_GAMEPAD_TYPE_UNKNOWN;
};

} // namespace Emerald

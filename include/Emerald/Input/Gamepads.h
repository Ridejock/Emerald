#pragma once

#include <array>
#include <bitset>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/ButtonStates.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// One connected pad. Stable for the process; do not store it across runs.
using GamepadId = u32;

struct GamepadDevice;

// Gamepad buttons in the standard layout, named by position so one set of bindings works on
// every pad: South is A on Xbox, Cross on PlayStation and B on Switch (see GetButtonLabel for
// what to show in prompts). After the physical buttons come "virtual" buttons made from analog
// inputs, so triggers and stick directions can be bound to actions. Do not renumber: saved
// bindings store these values.
// clang-format off
enum class GamepadButton : u8 {
    South = 0, East, West, North,
    Back, Guide, Start,
    LeftStick, RightStick,
    LeftShoulder, RightShoulder,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Misc1,
    // Rear paddles (Xbox Elite P1..P4). On a Steam Deck: RightPaddle1 = R4, LeftPaddle1 = L4,
    // RightPaddle2 = R5, LeftPaddle2 = L5.
    RightPaddle1, LeftPaddle1, RightPaddle2, LeftPaddle2,
    Touchpad,
    Misc2, Misc3, Misc4, Misc5, Misc6,

    // Virtual: down while the (deadzoned) value is past GamepadSettings::ButtonThreshold.
    LeftTrigger, RightTrigger,
    LeftStickUp, LeftStickDown, LeftStickLeft, LeftStickRight,
    RightStickUp, RightStickDown, RightStickLeft, RightStickRight,
    Count
};
// clang-format on

// Analog inputs. Sticks go from -1 to +1 (+X right, +Y down); triggers from 0 (released) to 1
// (fully pulled).
enum class GamepadAxis : u8 {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
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

// The pad families games name. Do not renumber: a stored type has to keep meaning the same pad.
enum class GamepadType : i32 {
    Unknown = 0,
    Standard = 1,
    Xbox360 = 2,
    XboxOne = 3,
    PS3 = 4,
    PS4 = 5,
    PS5 = 6,
    SwitchPro = 7,
    JoyConLeft = 8,
    JoyConRight = 9,
    JoyConPair = 10,
    GameCube = 11,
};

// Names for UI: "Xbox One", "PS4", "Switch Pro", ...
[[nodiscard]] const char* GetGamepadTypeName(GamepadType type);
// What is printed on a button of that type of pad, for prompts: South is "A" on Xbox, "Cross" on
// PlayStation and "B" on Switch; RightShoulder is "RB", "R1" or "R".
[[nodiscard]] const char* GetGamepadButtonLabel(GamepadButton button, GamepadType type);

// The Steam Deck's built-in controller: HIDAPI names it "Steam Deck", and the USB id is Valve
// 0x28DE / 0x1205. Its gamepad type is Standard (face buttons A/B/X/Y, same positions as printed
// on the Deck). Steam Input can hide this device and present an Xbox pad instead; that name does
// not match here. See docs/platforms.md.
[[nodiscard]] bool IsSteamDeckGamepad(std::string_view name, u16 vendor = 0, u16 product = 0);
// Prompts for that pad: A/B/X/Y, L1/R1, L2/R2, View/Menu, and L4/R4/L5/R5 on the rear paddles.
[[nodiscard]] const char* GetSteamDeckButtonLabel(GamepadButton button);

// All connected gamepads, fed from events by the Application. Single-player for now: every
// connected pad drives the same buttons and axes (the largest value wins), so the player can pick
// up any of them. Games normally use actions (Input.h); this is the layer underneath. Buttons
// follow the same frame/fixed-step edge rules as keys (ButtonStates.h).
class Gamepads {
public:
    struct PadInfo {
        GamepadId Id = 0;
        std::string Name;
        GamepadType Type = GamepadType::Unknown;
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
    // A button pressed since the last frame / fixed step (the first in GamepadButton order if
    // several), virtual ones included: for "press a button to rebind" screens.
    [[nodiscard]] std::optional<GamepadButton> GetPressedButton() const
    {
        for (usize i = 0; i < kButtonCount; ++i) {
            if (m_Buttons.WasPressed(i))
                return static_cast<GamepadButton>(i);
        }
        return std::nullopt;
    }
    // Deadzoned value; with several pads the one with the largest magnitude.
    [[nodiscard]] f32 GetAxis(GamepadAxis axis) const;

    // --- Pads ---
    [[nodiscard]] usize GetCount() const { return m_Pads.size(); }
    [[nodiscard]] const PadInfo& GetInfo(usize pad) const { return m_Pads[pad].Info; }
    [[nodiscard]] f32 GetRawAxis(usize pad, GamepadAxis axis) const;
    [[nodiscard]] f32 GetAxis(usize pad, GamepadAxis axis) const; // deadzoned, one pad
    // Type of the pad that was used last (UNKNOWN without pads): pick button prompts with it.
    [[nodiscard]] GamepadType GetLastUsedType() const { return m_LastUsedType; }
    // The last used pad is the Deck's built-in controller (name or USB id), so prompts use
    // GetSteamDeckButtonLabel rather than the generic type (that pad reports as Standard).
    [[nodiscard]] bool LastUsedIsSteamDeck() const { return m_LastUsedSteamDeck; }
    // Label of a button on the last used pad, e.g. GetButtonLabel(GamepadButton::South) == "Cross".
    [[nodiscard]] const char* GetButtonLabel(GamepadButton button) const;
    // Rumbles every connected pad; strengths 0..1 for the low (left, heavy) and high (right,
    // light) frequency motors. Pads without rumble ignore it. A new call replaces the old one.
    void Rumble(f32 low, f32 high, u32 milliseconds);

    [[nodiscard]] const GamepadSettings& GetSettings() const { return m_Settings; }
    void SetSettings(const GamepadSettings& settings) { m_Settings = settings; }

    // --- Called by the Application (or by tests) ---
    // Opens a pad reported as added (ignored if it is already open).
    void Open(GamepadId id);
    // Forgets a pad; buttons it was holding get released.
    void Close(GamepadId id);
    void CloseAll();
    // Registers a pad (handle may be null): used by Open and by tests.
    void AddPad(GamepadId id, GamepadDevice* handle, std::string name, GamepadType type);
    // A pad with no device handle. Tests and tools use this.
    void AddSyntheticPad(u32 id, std::string name, GamepadType type);
    void OnButton(GamepadId id, GamepadButton button, bool down);
    // `value`: -1..1 for sticks, 0..1 for triggers.
    void OnAxis(GamepadId id, GamepadAxis axis, f32 value);
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
        GamepadDevice* Handle = nullptr;
        std::bitset<kButtonCount> Buttons; // this pad's buttons, virtual ones included
        std::array<f32, kAxisCount> Axes{};
    };

    [[nodiscard]] static usize Index(GamepadButton b) { return static_cast<usize>(b); }
    [[nodiscard]] Pad* Find(GamepadId id);
    [[nodiscard]] bool PadIsSteamDeck(const Pad& pad) const;
    void Remember(const Pad& pad);
    [[nodiscard]] f32 Deadzoned(const Pad& pad, GamepadAxis axis) const;
    void SetPadButton(Pad& pad, usize button, bool down);
    void UpdateVirtualButtons(Pad& pad);
    void RefreshButton(usize button); // combined state = any pad holds it

    std::vector<Pad> m_Pads;
    ButtonStates<kButtonCount> m_Buttons;
    GamepadSettings m_Settings;
    GamepadType m_LastUsedType = GamepadType::Unknown;
    bool m_LastUsedSteamDeck = false;
};

} // namespace Emerald

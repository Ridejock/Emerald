#pragma once

#include "Emerald/Input/Gamepads.h"
#include "Emerald/Input/Keyboard.h"
#include "Emerald/Input/Mouse.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// What happened. OnEvent receives one of these; the fields that belong to Type are the ones to
// read. An event is only valid for that call: Text.Text in particular points at memory that
// dies when OnEvent returns (the string is also copied into Application::GetTextInput for the
// rest of the frame).
enum class EventType : u8 {
    None,
    Quit,
    WindowCloseRequested,
    WindowResized,
    WindowFocusGained,
    WindowFocusLost,
    WindowMouseEntered,
    WindowMouseLeft,
    WindowFullscreenChanged,
    KeyDown,
    KeyUp,
    TextInput,
    MouseButtonDown,
    MouseButtonUp,
    MouseMoved,
    MouseWheel,
    GamepadAdded,
    GamepadRemoved,
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAxisMoved,
};

struct KeyEvent {
    Key Code = Key::Unknown;
    KeyMod Mods = KeyMod::None;
    bool Down = false;
    bool Repeat = false; // held-key repeat; the keyboard state ignores these
};

struct TextEvent {
    const char* Text = nullptr; // UTF-8, valid only during OnEvent
};

struct MouseButtonEvent {
    MouseButton Button = MouseButton::Left;
    Vec2 Position{}; // window coordinates, top-left origin
    u8 Clicks = 1;
    bool Down = false;
};

struct MouseMotionEvent {
    Vec2 Position{};
    Vec2 Delta{};
};

// Notches, with natural scrolling already undone: y > 0 is away from the user (scroll up).
struct MouseWheelEvent {
    Vec2 Notches{};
};

struct GamepadDeviceEvent {
    GamepadId Id = 0;
};

struct GamepadButtonEvent {
    GamepadId Id = 0;
    GamepadButton Button = GamepadButton::South;
    bool Down = false;
};

// Sticks are -1..1 (+Y down), triggers 0..1. The same values Gamepads::OnAxis records.
struct GamepadAxisEvent {
    GamepadId Id = 0;
    GamepadAxis Axis = GamepadAxis::LeftX;
    f32 Value = 0.0f;
};

struct WindowSizeEvent {
    i32 Width = 0; // window coordinates
    i32 Height = 0;
};

struct FullscreenEvent {
    bool Fullscreen = false;
};

struct Event {
    EventType Type = EventType::None;
    union {
        KeyEvent Key;
        TextEvent Text;
        MouseButtonEvent MouseButton;
        MouseMotionEvent MouseMotion;
        MouseWheelEvent Wheel;
        GamepadDeviceEvent Gamepad;
        GamepadButtonEvent GamepadButton;
        GamepadAxisEvent GamepadAxis;
        WindowSizeEvent WindowSize;
        FullscreenEvent Fullscreen;
    };

    Event() : Type(EventType::None), Key{} {}
};

} // namespace Emerald

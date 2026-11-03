#pragma once

#include <optional>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/ButtonStates.h"

namespace Emerald {

// How many physical keys the keyboard state tracks. Any value in 0 .. kKeyCount-1 is a valid
// Key (cast one); the names below are the ones games usually bind.
inline constexpr u16 kKeyCount = 512;

// Modifier flags on a key event. Test with HasKeyMod(event.Key.Mods, KeyMod::Alt).
enum class KeyMod : u16 {
    None = 0,
    LShift = 0x0001,
    RShift = 0x0002,
    Level5 = 0x0004,
    LCtrl = 0x0040,
    RCtrl = 0x0080,
    LAlt = 0x0100,
    RAlt = 0x0200,
    LGui = 0x0400,
    RGui = 0x0800,
    Num = 0x1000,
    Caps = 0x2000,
    Mode = 0x4000,
    Scroll = 0x8000,
    Ctrl = 0x00C0,
    Shift = 0x0003,
    Alt = 0x0300,
    Gui = 0x0C00,
};

[[nodiscard]] constexpr KeyMod operator|(KeyMod a, KeyMod b)
{
    return static_cast<KeyMod>(static_cast<u16>(a) | static_cast<u16>(b));
}
[[nodiscard]] constexpr KeyMod operator&(KeyMod a, KeyMod b)
{
    return static_cast<KeyMod>(static_cast<u16>(a) & static_cast<u16>(b));
}
[[nodiscard]] constexpr bool HasKeyMod(KeyMod mods, KeyMod flag)
{
    return (static_cast<u16>(mods) & static_cast<u16>(flag)) != 0;
}

// Physical keys, named after the US layout (the same key is Key::W on AZERTY too, where it is
// labelled Z) - what games want for movement keys. The numbers are stable: do not renumber them,
// saved bindings store them. A key that has no name here is still a Key of that number.
// clang-format off
enum class Key : u16 {
    Unknown = 0,

    A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num1 = 30, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,

    Enter = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,
    Minus = 45,
    Semicolon = 51, Apostrophe = 52,
    Comma = 54,

    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    Right = 79, Left = 80, Down = 81, Up = 82,

    KpEnter = 88,

    LeftCtrl = 224, LeftShift = 225, LeftAlt = 226,
    RightCtrl = 228, RightShift = 229, RightAlt = 230,
};
// clang-format on

// Physical letter keys are contiguous: 0 is A and 25 is Z.
[[nodiscard]] constexpr Key LetterKey(i32 index)
{
    return static_cast<Key>(static_cast<u16>(Key::A) + static_cast<u16>(index));
}

// Low-level keyboard state with edge tracking, fed from events by the Application. Games
// normally use actions instead (Input.h), which are built on top of this. Pressed/released follow
// the frame/fixed-step rules described in ButtonStates.h.
class Keyboard {
public:
    [[nodiscard]] bool IsKeyDown(Key key) const { return m_Keys.IsDown(Index(key)); }
    [[nodiscard]] bool WasKeyPressed(Key key) const { return m_Keys.WasPressed(Index(key)); }
    [[nodiscard]] bool WasKeyReleased(Key key) const { return m_Keys.WasReleased(Index(key)); }
    // A key pressed since the last frame / fixed step (the lowest key number if several), for
    // "press a key to rebind" screens. Any key number, not only the named Key values.
    [[nodiscard]] std::optional<Key> GetPressedKey() const
    {
        for (usize i = 0; i < kKeyCount; ++i) {
            if (m_Keys.WasPressed(i))
                return static_cast<Key>(i);
        }
        return std::nullopt;
    }

    // --- Called by the Application (or by tests) ---
    // Key repeats (holding a key down) must not be passed in; they are not new presses.
    void OnKeyDown(Key key) { Set(key, true); }
    void OnKeyUp(Key key) { Set(key, false); }
    // Releases every held key, e.g. when the window loses focus and would miss the key-up events.
    void ReleaseAll() { m_Keys.ReleaseAll(); }
    void BeginFrame() { m_Keys.BeginFrame(); }
    void BeginFixedStep() { m_Keys.BeginFixedStep(); }
    void EndFixedStep() { m_Keys.EndFixedStep(); }

private:
    [[nodiscard]] static usize Index(Key key) { return static_cast<usize>(key); }
    void Set(Key key, bool down)
    {
        const usize index = Index(key);
        if (index < kKeyCount)
            m_Keys.Set(index, down);
    }

    ButtonStates<kKeyCount> m_Keys;
};

} // namespace Emerald

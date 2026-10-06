#pragma once

#include <optional>

#include <SDL3/SDL_scancode.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/ButtonStates.h"

namespace Emerald {

// Physical keys, named after the US layout (the same key is `Key::W` on AZERTY too, where it is
// labelled Z) - what games want for movement keys. The values are SDL scancodes, so any
// SDL_Scancode can also be used through Key(scancode).
// clang-format off
enum class Key : u16 {
    A = SDL_SCANCODE_A, B = SDL_SCANCODE_B, C = SDL_SCANCODE_C, D = SDL_SCANCODE_D,
    E = SDL_SCANCODE_E, F = SDL_SCANCODE_F, G = SDL_SCANCODE_G, H = SDL_SCANCODE_H,
    I = SDL_SCANCODE_I, J = SDL_SCANCODE_J, K = SDL_SCANCODE_K, L = SDL_SCANCODE_L,
    M = SDL_SCANCODE_M, N = SDL_SCANCODE_N, O = SDL_SCANCODE_O, P = SDL_SCANCODE_P,
    Q = SDL_SCANCODE_Q, R = SDL_SCANCODE_R, S = SDL_SCANCODE_S, T = SDL_SCANCODE_T,
    U = SDL_SCANCODE_U, V = SDL_SCANCODE_V, W = SDL_SCANCODE_W, X = SDL_SCANCODE_X,
    Y = SDL_SCANCODE_Y, Z = SDL_SCANCODE_Z,

    Num0 = SDL_SCANCODE_0, Num1 = SDL_SCANCODE_1, Num2 = SDL_SCANCODE_2, Num3 = SDL_SCANCODE_3,
    Num4 = SDL_SCANCODE_4, Num5 = SDL_SCANCODE_5, Num6 = SDL_SCANCODE_6, Num7 = SDL_SCANCODE_7,
    Num8 = SDL_SCANCODE_8, Num9 = SDL_SCANCODE_9,

    Up = SDL_SCANCODE_UP, Down = SDL_SCANCODE_DOWN,
    Left = SDL_SCANCODE_LEFT, Right = SDL_SCANCODE_RIGHT,

    Space = SDL_SCANCODE_SPACE, Enter = SDL_SCANCODE_RETURN, Escape = SDL_SCANCODE_ESCAPE,
    Tab = SDL_SCANCODE_TAB, Backspace = SDL_SCANCODE_BACKSPACE,
    LeftShift = SDL_SCANCODE_LSHIFT, RightShift = SDL_SCANCODE_RSHIFT,
    LeftCtrl = SDL_SCANCODE_LCTRL, RightCtrl = SDL_SCANCODE_RCTRL,
    LeftAlt = SDL_SCANCODE_LALT, RightAlt = SDL_SCANCODE_RALT,

    F1 = SDL_SCANCODE_F1, F2 = SDL_SCANCODE_F2, F3 = SDL_SCANCODE_F3, F4 = SDL_SCANCODE_F4,
    F5 = SDL_SCANCODE_F5, F6 = SDL_SCANCODE_F6, F7 = SDL_SCANCODE_F7, F8 = SDL_SCANCODE_F8,
    F9 = SDL_SCANCODE_F9, F10 = SDL_SCANCODE_F10, F11 = SDL_SCANCODE_F11, F12 = SDL_SCANCODE_F12,
};
// clang-format on

// Low-level keyboard state with edge tracking, fed from SDL events by the Application. Games
// normally use actions instead (Input.h), which are built on top of this. Pressed/released follow
// the frame/fixed-step rules described in ButtonStates.h.
class Keyboard {
public:
    [[nodiscard]] bool IsKeyDown(Key key) const { return m_Keys.IsDown(Index(key)); }
    [[nodiscard]] bool WasKeyPressed(Key key) const { return m_Keys.WasPressed(Index(key)); }
    [[nodiscard]] bool WasKeyReleased(Key key) const { return m_Keys.WasReleased(Index(key)); }
    // A key pressed since the last frame / fixed step (the lowest scancode if several), for
    // "press a key to rebind" screens. Any scancode, not only the named Key values.
    [[nodiscard]] std::optional<Key> GetPressedKey() const
    {
        for (usize i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            if (m_Keys.WasPressed(i))
                return static_cast<Key>(i);
        }
        return std::nullopt;
    }

    // --- Called by the Application (or by tests) ---
    // Key repeats (holding a key down) must not be passed in; they are not new presses.
    void OnKeyDown(SDL_Scancode scancode) { m_Keys.Set(static_cast<usize>(scancode), true); }
    void OnKeyUp(SDL_Scancode scancode) { m_Keys.Set(static_cast<usize>(scancode), false); }
    // Releases every held key, e.g. when the window loses focus and would miss the key-up events.
    void ReleaseAll() { m_Keys.ReleaseAll(); }
    void BeginFrame() { m_Keys.BeginFrame(); }
    void BeginFixedStep() { m_Keys.BeginFixedStep(); }
    void EndFixedStep() { m_Keys.EndFixedStep(); }

private:
    [[nodiscard]] static usize Index(Key key) { return static_cast<usize>(key); }

    ButtonStates<SDL_SCANCODE_COUNT> m_Keys;
};

} // namespace Emerald

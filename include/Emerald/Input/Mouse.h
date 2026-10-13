#pragma once

#include <SDL3/SDL_mouse.h>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/ButtonStates.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// Mouse buttons; the values are SDL's button numbers (SDL_BUTTON_LEFT, ...).
enum class MouseButton : u8 {
    Left = SDL_BUTTON_LEFT,
    Middle = SDL_BUTTON_MIDDLE,
    Right = SDL_BUTTON_RIGHT,
    X1 = SDL_BUTTON_X1, // the "back" side button
    X2 = SDL_BUTTON_X2, // the "forward" side button
};

// Low-level mouse state with edge tracking, fed from SDL events by the Application (like
// Keyboard). Games bind buttons to actions (Input.h) and read the position from here.
class Mouse {
public:
    [[nodiscard]] bool IsButtonDown(MouseButton b) const { return m_Buttons.IsDown(Index(b)); }
    [[nodiscard]] bool WasButtonPressed(MouseButton b) const
    {
        return m_Buttons.WasPressed(Index(b));
    }
    [[nodiscard]] bool WasButtonReleased(MouseButton b) const
    {
        return m_Buttons.WasReleased(Index(b));
    }
    // The cursor in window coordinates (the units of Window::GetSize; (0, 0) = top-left).
    [[nodiscard]] Vec2 GetPosition() const { return m_Position; }
    // The cursor moved during the current frame (e.g. to switch aiming from a stick to the mouse).
    [[nodiscard]] bool HasMoved() const { return m_Moved; }
    // Inside the window (it has mouse focus).
    [[nodiscard]] bool IsInWindow() const { return m_InWindow; }
    // Wheel scrolling, in notches (fractions on smooth wheels and touchpads): y > 0 away from
    // the user (scroll up), x > 0 to the right; natural scrolling is already undone. Like the
    // button edges: since the last frame in OnUpdate, since the last fixed step in OnFixedUpdate.
    [[nodiscard]] Vec2 GetWheel() const { return m_InFixedStep ? m_StepWheel : m_FrameWheel; }

    // --- Called by the Application (or by tests) ---
    void OnButton(MouseButton button, bool down) { m_Buttons.Set(Index(button), down); }
    void OnMotion(const Vec2& position)
    {
        m_Position = position;
        m_Moved = true;
        m_InWindow = true;
    }
    void OnLeave() { m_InWindow = false; }
    void OnWheel(const Vec2& notches)
    {
        m_FrameWheel += notches;
        m_StepWheel += notches;
    }
    void ReleaseAll() { m_Buttons.ReleaseAll(); }
    void BeginFrame()
    {
        m_Buttons.BeginFrame();
        m_Moved = false;
        m_FrameWheel = {};
    }
    void BeginFixedStep()
    {
        m_Buttons.BeginFixedStep();
        m_InFixedStep = true;
    }
    void EndFixedStep()
    {
        m_Buttons.EndFixedStep();
        m_InFixedStep = false;
        m_StepWheel = {};
    }

private:
    [[nodiscard]] static usize Index(MouseButton b) { return static_cast<usize>(b); }

    ButtonStates<8> m_Buttons; // indexed by SDL button number (1..5)
    Vec2 m_Position{};
    bool m_Moved = false;
    bool m_InWindow = false;
    Vec2 m_FrameWheel{}; // since BeginFrame
    Vec2 m_StepWheel{};  // since the last EndFixedStep
    bool m_InFixedStep = false;
};

} // namespace Emerald

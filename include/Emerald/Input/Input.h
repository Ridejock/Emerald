#pragma once

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/Gamepads.h"
#include "Emerald/Input/Keyboard.h"

namespace Emerald {

// Action-based input: the game names what the player can do ("Fire", "Thrust") and binds keys
// and gamepad inputs to those names; game code only asks about actions, so controls can be
// remapped at runtime without touching it. Get it with Application::GetInput():
//
//   // Once, e.g. in OnStart:
//   input.BindAction("Fire", {Key::Space, Key::J});
//   input.BindAction("Fire", {GamepadButton::South, GamepadButton::RightTrigger});
//   input.BindAxis("Rotate", Key::A, Key::D);          // negative key, positive key
//   input.BindAxis("Rotate", Key::Left, Key::Right);   // more pairs for the same axis
//   input.BindAxis("Rotate", GamepadAxis::LeftX);      // analog stick (deadzoned)
//   input.BindAxis("Rotate", GamepadButton::DPadLeft, GamepadButton::DPadRight);
//
//   // Every (fixed) update:
//   if (input.WasActionPressed("Fire")) Shoot();       // once per press
//   angle += input.GetAxis("Rotate") * turnSpeed * dt; // -1..+1
//
//   // Rebinding, e.g. from an options menu (replaces only the keys; pad bindings stay):
//   input.RebindAction("Fire", {Key::LeftCtrl});
//
// Pressed/released follow the ButtonStates rules: since the last frame in OnUpdate, since the
// last fixed step in OnFixedUpdate. Unknown names are simply "not down" / 0. Every connected
// gamepad drives the actions (single player); see Gamepads.h for deadzones, labels and pads.
class Input {
public:
    Input(const Keyboard& keyboard, Gamepads& gamepads) : m_Keyboard(keyboard), m_Gamepads(gamepads)
    {
    }

    // --- Action bindings (any bound key or button triggers the action) ---
    void BindAction(std::string_view action, std::initializer_list<Key> keys);
    void BindAction(std::string_view action, std::initializer_list<GamepadButton> buttons);
    // Replace only the keys, or only the gamepad buttons, of an action.
    void RebindAction(std::string_view action, std::initializer_list<Key> keys);
    void RebindAction(std::string_view action, std::initializer_list<GamepadButton> buttons);

    // --- Axis bindings ---
    // A key or button pair: `negative` gives -1, `positive` +1, both or neither 0.
    void BindAxis(std::string_view axis, Key negative, Key positive);
    void BindAxis(std::string_view axis, GamepadButton negative, GamepadButton positive);
    // An analog stick or trigger, multiplied by `scale` (-1 flips it, e.g. so stick up is +1).
    void BindAxis(std::string_view axis, GamepadAxis analog, f32 scale = 1.0f);
    // Replace all bindings of that kind with one.
    void RebindAxis(std::string_view axis, Key negative, Key positive);
    void RebindAxis(std::string_view axis, GamepadButton negative, GamepadButton positive);
    void RebindAxis(std::string_view axis, GamepadAxis analog, f32 scale = 1.0f);

    // Removes an action or axis with all its bindings.
    void Unbind(std::string_view name);
    // What is bound to an action (empty if there is no such action).
    [[nodiscard]] std::span<const Key> GetActionKeys(std::string_view action) const;
    [[nodiscard]] std::span<const GamepadButton> GetActionButtons(std::string_view action) const;

    // --- Queries ---
    // True while any bound key or button is held.
    [[nodiscard]] bool IsActionDown(std::string_view action) const;
    // True once when the action goes down (pressing a second bound input does not repeat it).
    [[nodiscard]] bool WasActionPressed(std::string_view action) const;
    // True once when the action stops being down, i.e. its last held input is released.
    [[nodiscard]] bool WasActionReleased(std::string_view action) const;
    // -1..+1: the binding with the largest magnitude wins. Key and button pairs together give
    // -1, 0 or +1 (opposite directions cancel), analog bindings anything in between.
    [[nodiscard]] f32 GetAxis(std::string_view axis) const;

    // Rumbles the gamepads; 0..1 per motor (low = heavy, high = light).
    void Rumble(f32 low, f32 high, u32 milliseconds) { m_Gamepads.Rumble(low, high, milliseconds); }

    // Raw state, for the rare cases actions do not fit (e.g. "press a key to rebind"), and the
    // gamepad list, settings (deadzones) and button labels.
    [[nodiscard]] const Keyboard& GetKeyboard() const { return m_Keyboard; }
    [[nodiscard]] Gamepads& GetGamepads() { return m_Gamepads; }
    [[nodiscard]] const Gamepads& GetGamepads() const { return m_Gamepads; }

private:
    struct Action {
        std::string Name;
        std::vector<Key> Keys;
        std::vector<GamepadButton> Buttons;
    };
    template <typename T> struct Pair {
        T Negative;
        T Positive;
    };
    struct Analog {
        GamepadAxis Axis;
        f32 Scale;
    };
    struct Axis {
        std::string Name;
        std::vector<Pair<Key>> KeyPairs;
        std::vector<Pair<GamepadButton>> ButtonPairs;
        std::vector<Analog> Analogs;
    };

    // A handful of actions per game: a linear search by name is simple and fast enough.
    [[nodiscard]] const Action* FindAction(std::string_view name) const;
    [[nodiscard]] const Axis* FindAxis(std::string_view name) const;
    Action& GetOrAddAction(std::string_view name);
    Axis& GetOrAddAxis(std::string_view name);

    const Keyboard& m_Keyboard;
    Gamepads& m_Gamepads;
    std::vector<Action> m_Actions;
    std::vector<Axis> m_Axes;
};

} // namespace Emerald

#pragma once

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/Keyboard.h"

namespace Emerald {

// Action-based input: the game names what the player can do ("Fire", "Thrust") and binds keys
// to those names; game code only asks about actions, so controls can be remapped at runtime
// without touching it. Get it with Application::GetInput():
//
//   // Once, e.g. in OnStart:
//   input.BindAction("Fire", {Key::Space, Key::J});
//   input.BindAxis("Rotate", Key::A, Key::D);          // negative key, positive key
//   input.BindAxis("Rotate", Key::Left, Key::Right);   // more pairs for the same axis
//
//   // Every (fixed) update:
//   if (input.WasActionPressed("Fire")) Shoot();       // once per press
//   angle += input.GetAxis("Rotate") * turnSpeed * dt; // -1, 0 or +1
//
//   // Rebinding, e.g. from an options menu:
//   input.RebindAction("Fire", {Key::LeftCtrl});
//
// Pressed/released follow the Keyboard rules: since the last frame in OnUpdate, since the last
// fixed step in OnFixedUpdate. Unknown names are simply "not down" / 0. Only keyboard keys for
// now; gamepad buttons and sticks would become more binding types here, without changing the
// queries.
class Input {
public:
    explicit Input(const Keyboard& keyboard) : m_Keyboard(keyboard) {}

    // --- Bindings ---
    // Adds keys to an action (an action can have any number of keys; any of them triggers it).
    void BindAction(std::string_view action, std::initializer_list<Key> keys);
    // Replaces all keys of an action.
    void RebindAction(std::string_view action, std::initializer_list<Key> keys);
    // Adds a key pair to an axis: `negative` gives -1, `positive` +1, both or neither 0.
    void BindAxis(std::string_view axis, Key negative, Key positive);
    // Replaces all key pairs of an axis with one pair.
    void RebindAxis(std::string_view axis, Key negative, Key positive);
    // Removes an action or axis with all its bindings.
    void Unbind(std::string_view name);
    // The keys bound to an action (empty if there is no such action).
    [[nodiscard]] std::span<const Key> GetActionKeys(std::string_view action) const;

    // --- Queries ---
    // True while any bound key is held.
    [[nodiscard]] bool IsActionDown(std::string_view action) const;
    // True once when a bound key goes down (holding a second bound key does not repeat it).
    [[nodiscard]] bool WasActionPressed(std::string_view action) const;
    // True once when the action stops being down, i.e. its last held key is released.
    [[nodiscard]] bool WasActionReleased(std::string_view action) const;
    // -1, 0 or +1 from the axis' key pairs: +1 if any positive key is held, -1 if any negative
    // one is, 0 for both or neither. (Analog sticks would give values in between.)
    [[nodiscard]] f32 GetAxis(std::string_view axis) const;

    // Raw key state, for the rare cases actions do not fit (e.g. "press a key to rebind").
    [[nodiscard]] const Keyboard& GetKeyboard() const { return m_Keyboard; }

private:
    struct Action {
        std::string Name;
        std::vector<Key> Keys;
    };
    struct AxisPair {
        Key Negative;
        Key Positive;
    };
    struct Axis {
        std::string Name;
        std::vector<AxisPair> Pairs;
    };

    // A handful of actions per game: a linear search by name is simple and fast enough.
    [[nodiscard]] const Action* FindAction(std::string_view name) const;
    [[nodiscard]] const Axis* FindAxis(std::string_view name) const;
    Action& GetOrAddAction(std::string_view name);
    Axis& GetOrAddAxis(std::string_view name);

    const Keyboard& m_Keyboard;
    std::vector<Action> m_Actions;
    std::vector<Axis> m_Axes;
};

} // namespace Emerald

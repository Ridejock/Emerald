#include "Emerald/Input/Input.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

namespace {

template <typename T> void AddUnique(std::vector<T>& list, std::initializer_list<T> items)
{
    for (const T& item : items) {
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }
}

} // namespace

void Input::BindAction(std::string_view action, std::initializer_list<Key> keys)
{
    AddUnique(GetOrAddAction(action).Keys, keys);
}

void Input::BindAction(std::string_view action, std::initializer_list<GamepadButton> buttons)
{
    AddUnique(GetOrAddAction(action).Buttons, buttons);
}

void Input::RebindAction(std::string_view action, std::initializer_list<Key> keys)
{
    GetOrAddAction(action).Keys.clear();
    BindAction(action, keys);
}

void Input::RebindAction(std::string_view action, std::initializer_list<GamepadButton> buttons)
{
    GetOrAddAction(action).Buttons.clear();
    BindAction(action, buttons);
}

void Input::BindAxis(std::string_view axis, Key negative, Key positive)
{
    GetOrAddAxis(axis).KeyPairs.push_back({negative, positive});
}

void Input::BindAxis(std::string_view axis, GamepadButton negative, GamepadButton positive)
{
    GetOrAddAxis(axis).ButtonPairs.push_back({negative, positive});
}

void Input::BindAxis(std::string_view axis, GamepadAxis analog, f32 scale)
{
    GetOrAddAxis(axis).Analogs.push_back({analog, scale});
}

void Input::RebindAxis(std::string_view axis, Key negative, Key positive)
{
    GetOrAddAxis(axis).KeyPairs.clear();
    BindAxis(axis, negative, positive);
}

void Input::RebindAxis(std::string_view axis, GamepadButton negative, GamepadButton positive)
{
    GetOrAddAxis(axis).ButtonPairs.clear();
    BindAxis(axis, negative, positive);
}

void Input::RebindAxis(std::string_view axis, GamepadAxis analog, f32 scale)
{
    GetOrAddAxis(axis).Analogs.clear();
    BindAxis(axis, analog, scale);
}

void Input::Unbind(std::string_view name)
{
    std::erase_if(m_Actions, [&](const Action& a) { return a.Name == name; });
    std::erase_if(m_Axes, [&](const Axis& a) { return a.Name == name; });
}

std::span<const Key> Input::GetActionKeys(std::string_view action) const
{
    const Action* a = FindAction(action);
    return a ? std::span<const Key>(a->Keys) : std::span<const Key>();
}

std::span<const GamepadButton> Input::GetActionButtons(std::string_view action) const
{
    const Action* a = FindAction(action);
    return a ? std::span<const GamepadButton>(a->Buttons) : std::span<const GamepadButton>();
}

bool Input::IsActionDown(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a)
        return false;
    return std::any_of(a->Keys.begin(), a->Keys.end(),
                       [&](Key k) { return m_Keyboard.IsKeyDown(k); }) ||
           std::any_of(a->Buttons.begin(), a->Buttons.end(),
                       [&](GamepadButton b) { return m_Gamepads.IsButtonDown(b); });
}

bool Input::WasActionPressed(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a)
        return false;
    // Pressed if some input went down now while no other bound input was already held before.
    bool pressedNow = false;
    bool heldBefore = false;
    const auto check = [&](bool down, bool pressed) {
        heldBefore = heldBefore || (down && !pressed);
        pressedNow = pressedNow || pressed;
    };
    for (Key k : a->Keys)
        check(m_Keyboard.IsKeyDown(k), m_Keyboard.WasKeyPressed(k));
    for (GamepadButton b : a->Buttons)
        check(m_Gamepads.IsButtonDown(b), m_Gamepads.WasButtonPressed(b));
    return pressedNow && !heldBefore;
}

bool Input::WasActionReleased(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a || IsActionDown(action))
        return false;
    return std::any_of(a->Keys.begin(), a->Keys.end(),
                       [&](Key k) { return m_Keyboard.WasKeyReleased(k); }) ||
           std::any_of(a->Buttons.begin(), a->Buttons.end(),
                       [&](GamepadButton b) { return m_Gamepads.WasButtonReleased(b); });
}

f32 Input::GetAxis(std::string_view axis) const
{
    const Axis* a = FindAxis(axis);
    if (!a)
        return 0.0f;
    // Digital bindings: any held input per direction counts once, so opposite directions cancel.
    bool negative = false;
    bool positive = false;
    for (const Pair<Key>& pair : a->KeyPairs) {
        negative = negative || m_Keyboard.IsKeyDown(pair.Negative);
        positive = positive || m_Keyboard.IsKeyDown(pair.Positive);
    }
    for (const Pair<GamepadButton>& pair : a->ButtonPairs) {
        negative = negative || m_Gamepads.IsButtonDown(pair.Negative);
        positive = positive || m_Gamepads.IsButtonDown(pair.Positive);
    }
    f32 value = (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    // Analog bindings: the largest magnitude wins (a full key press beats a half-tilted stick).
    for (const Analog& analog : a->Analogs) {
        const f32 v = std::clamp(m_Gamepads.GetAxis(analog.Axis) * analog.Scale, -1.0f, 1.0f);
        if (std::abs(v) > std::abs(value))
            value = v;
    }
    return value;
}

const Input::Action* Input::FindAction(std::string_view name) const
{
    for (const Action& a : m_Actions) {
        if (a.Name == name)
            return &a;
    }
    return nullptr;
}

const Input::Axis* Input::FindAxis(std::string_view name) const
{
    for (const Axis& a : m_Axes) {
        if (a.Name == name)
            return &a;
    }
    return nullptr;
}

Input::Action& Input::GetOrAddAction(std::string_view name)
{
    for (Action& a : m_Actions) {
        if (a.Name == name)
            return a;
    }
    m_Actions.push_back({std::string(name), {}, {}});
    return m_Actions.back();
}

Input::Axis& Input::GetOrAddAxis(std::string_view name)
{
    for (Axis& a : m_Axes) {
        if (a.Name == name)
            return a;
    }
    m_Axes.push_back({std::string(name), {}, {}, {}});
    return m_Axes.back();
}

} // namespace Emerald

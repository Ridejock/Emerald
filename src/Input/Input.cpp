#include "Emerald/Input/Input.h"

#include <algorithm>

namespace Emerald {

void Input::BindAction(std::string_view action, std::initializer_list<Key> keys)
{
    Action& a = GetOrAddAction(action);
    for (Key key : keys) {
        if (std::find(a.Keys.begin(), a.Keys.end(), key) == a.Keys.end())
            a.Keys.push_back(key);
    }
}

void Input::RebindAction(std::string_view action, std::initializer_list<Key> keys)
{
    GetOrAddAction(action).Keys.clear();
    BindAction(action, keys);
}

void Input::BindAxis(std::string_view axis, Key negative, Key positive)
{
    GetOrAddAxis(axis).Pairs.push_back({negative, positive});
}

void Input::RebindAxis(std::string_view axis, Key negative, Key positive)
{
    Axis& a = GetOrAddAxis(axis);
    a.Pairs.clear();
    a.Pairs.push_back({negative, positive});
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

bool Input::IsActionDown(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a)
        return false;
    return std::any_of(a->Keys.begin(), a->Keys.end(),
                       [&](Key key) { return m_Keyboard.IsKeyDown(key); });
}

bool Input::WasActionPressed(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a)
        return false;
    // Pressed if some key went down now while no other bound key was already held before.
    bool pressedNow = false;
    for (Key key : a->Keys) {
        const bool pressed = m_Keyboard.WasKeyPressed(key);
        if (m_Keyboard.IsKeyDown(key) && !pressed)
            return false; // already held since earlier: the action was down before
        pressedNow = pressedNow || pressed;
    }
    return pressedNow;
}

bool Input::WasActionReleased(std::string_view action) const
{
    const Action* a = FindAction(action);
    if (!a || IsActionDown(action))
        return false;
    return std::any_of(a->Keys.begin(), a->Keys.end(),
                       [&](Key key) { return m_Keyboard.WasKeyReleased(key); });
}

f32 Input::GetAxis(std::string_view axis) const
{
    const Axis* a = FindAxis(axis);
    if (!a)
        return 0.0f;
    // Any held key per direction counts once, so opposite directions cancel out.
    bool negative = false;
    bool positive = false;
    for (const AxisPair& pair : a->Pairs) {
        negative = negative || m_Keyboard.IsKeyDown(pair.Negative);
        positive = positive || m_Keyboard.IsKeyDown(pair.Positive);
    }
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
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
    m_Actions.push_back({std::string(name), {}});
    return m_Actions.back();
}

Input::Axis& Input::GetOrAddAxis(std::string_view name)
{
    for (Axis& a : m_Axes) {
        if (a.Name == name)
            return a;
    }
    m_Axes.push_back({std::string(name), {}});
    return m_Axes.back();
}

} // namespace Emerald

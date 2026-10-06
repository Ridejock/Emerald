#include "Emerald/Input/Input.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

namespace {

template <typename T> void AddUnique(std::vector<T>& list, std::span<const T> items)
{
    for (const T& item : items) {
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }
}

} // namespace

void Input::BindAction(std::string_view action, std::initializer_list<Key> keys)
{
    AddUnique(GetOrAddAction(action).Keys, std::span(keys.begin(), keys.size()));
}

void Input::BindAction(std::string_view action, std::initializer_list<GamepadButton> buttons)
{
    AddUnique(GetOrAddAction(action).Buttons, std::span(buttons.begin(), buttons.size()));
}

void Input::BindAction(std::string_view action, std::initializer_list<MouseButton> buttons)
{
    AddUnique(GetOrAddAction(action).MouseButtons, std::span(buttons.begin(), buttons.size()));
}

void Input::RebindAction(std::string_view action, std::initializer_list<Key> keys)
{
    RebindAction(action, std::span(keys.begin(), keys.size()));
}

void Input::RebindAction(std::string_view action, std::initializer_list<GamepadButton> buttons)
{
    RebindAction(action, std::span(buttons.begin(), buttons.size()));
}

void Input::RebindAction(std::string_view action, std::initializer_list<MouseButton> buttons)
{
    RebindAction(action, std::span(buttons.begin(), buttons.size()));
}

void Input::RebindAction(std::string_view action, std::span<const Key> keys)
{
    Action& a = GetOrAddAction(action);
    a.Keys.clear();
    AddUnique(a.Keys, keys);
}

void Input::RebindAction(std::string_view action, std::span<const GamepadButton> buttons)
{
    Action& a = GetOrAddAction(action);
    a.Buttons.clear();
    AddUnique(a.Buttons, buttons);
}

void Input::RebindAction(std::string_view action, std::span<const MouseButton> buttons)
{
    Action& a = GetOrAddAction(action);
    a.MouseButtons.clear();
    AddUnique(a.MouseButtons, buttons);
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

std::span<const MouseButton> Input::GetActionMouseButtons(std::string_view action) const
{
    const Action* a = FindAction(action);
    return a ? std::span<const MouseButton>(a->MouseButtons) : std::span<const MouseButton>();
}

bool Input::IsActionDown(std::string_view action) const
{
    if (m_Blocked)
        return false;
    if (m_ReplaySample)
        return (ReplayFlags(action) & InputSample::kDown) != 0;
    const Action* a = FindAction(action);
    return a && DevicesDown(*a);
}

bool Input::WasActionPressed(std::string_view action) const
{
    if (m_Blocked)
        return false;
    if (m_ReplaySample)
        return (ReplayFlags(action) & InputSample::kPressed) != 0;
    const Action* a = FindAction(action);
    return a && DevicesPressed(*a);
}

bool Input::WasActionReleased(std::string_view action) const
{
    if (m_Blocked)
        return false;
    if (m_ReplaySample)
        return (ReplayFlags(action) & InputSample::kReleased) != 0;
    const Action* a = FindAction(action);
    return a && DevicesReleased(*a);
}

f32 Input::GetAxis(std::string_view axis) const
{
    if (m_Blocked)
        return 0.0f;
    if (m_ReplaySample)
        return ReplayAxis(axis);
    const Axis* a = FindAxis(axis);
    return a ? DevicesAxis(*a) : 0.0f;
}

InputNames Input::GetNames() const
{
    InputNames names;
    for (const Action& a : m_Actions)
        names.Actions.push_back(a.Name);
    for (const Axis& a : m_Axes)
        names.Axes.push_back(a.Name);
    return names;
}

InputSample Input::Capture(const InputNames& names) const
{
    InputSample sample;
    for (const std::string& name : names.Actions) {
        const Action* a = FindAction(name);
        if (!a) {
            sample.Actions.push_back(0);
            continue;
        }
        sample.Actions.push_back(
            static_cast<u8>((DevicesDown(*a) ? InputSample::kDown : 0) |
                            (DevicesPressed(*a) ? InputSample::kPressed : 0) |
                            (DevicesReleased(*a) ? InputSample::kReleased : 0)));
    }
    for (const std::string& name : names.Axes) {
        const Axis* a = FindAxis(name);
        sample.Axes.push_back(a ? DevicesAxis(*a) : 0.0f);
    }
    return sample;
}

void Input::SetReplay(const InputSample* sample, const InputNames* names)
{
    m_ReplaySample = names ? sample : nullptr;
    m_ReplayNames = sample ? names : nullptr;
}

bool Input::DevicesDown(const Action& a) const
{
    return std::any_of(a.Keys.begin(), a.Keys.end(),
                       [&](Key k) { return m_Keyboard.IsKeyDown(k); }) ||
           std::any_of(a.Buttons.begin(), a.Buttons.end(),
                       [&](GamepadButton b) { return m_Gamepads.IsButtonDown(b); }) ||
           std::any_of(a.MouseButtons.begin(), a.MouseButtons.end(),
                       [&](MouseButton b) { return m_Mouse.IsButtonDown(b); });
}

bool Input::DevicesPressed(const Action& a) const
{
    // Pressed if some input went down now while no other bound input was already held before.
    bool pressedNow = false;
    bool heldBefore = false;
    const auto check = [&](bool down, bool pressed) {
        heldBefore = heldBefore || (down && !pressed);
        pressedNow = pressedNow || pressed;
    };
    for (Key k : a.Keys)
        check(m_Keyboard.IsKeyDown(k), m_Keyboard.WasKeyPressed(k));
    for (GamepadButton b : a.Buttons)
        check(m_Gamepads.IsButtonDown(b), m_Gamepads.WasButtonPressed(b));
    for (MouseButton b : a.MouseButtons)
        check(m_Mouse.IsButtonDown(b), m_Mouse.WasButtonPressed(b));
    return pressedNow && !heldBefore;
}

bool Input::DevicesReleased(const Action& a) const
{
    // Released once its last held input goes up.
    if (DevicesDown(a))
        return false;
    return std::any_of(a.Keys.begin(), a.Keys.end(),
                       [&](Key k) { return m_Keyboard.WasKeyReleased(k); }) ||
           std::any_of(a.Buttons.begin(), a.Buttons.end(),
                       [&](GamepadButton b) { return m_Gamepads.WasButtonReleased(b); }) ||
           std::any_of(a.MouseButtons.begin(), a.MouseButtons.end(),
                       [&](MouseButton b) { return m_Mouse.WasButtonReleased(b); });
}

f32 Input::DevicesAxis(const Axis& a) const
{
    // Digital bindings: any held input per direction counts once, so opposite directions cancel.
    bool negative = false;
    bool positive = false;
    for (const Pair<Key>& pair : a.KeyPairs) {
        negative = negative || m_Keyboard.IsKeyDown(pair.Negative);
        positive = positive || m_Keyboard.IsKeyDown(pair.Positive);
    }
    for (const Pair<GamepadButton>& pair : a.ButtonPairs) {
        negative = negative || m_Gamepads.IsButtonDown(pair.Negative);
        positive = positive || m_Gamepads.IsButtonDown(pair.Positive);
    }
    f32 value = (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    // Analog bindings: the largest magnitude wins (a full key press beats a half-tilted stick).
    for (const Analog& analog : a.Analogs) {
        const f32 v = std::clamp(m_Gamepads.GetAxis(analog.Axis) * analog.Scale, -1.0f, 1.0f);
        if (std::abs(v) > std::abs(value))
            value = v;
    }
    return value;
}

u8 Input::ReplayFlags(std::string_view action) const
{
    const std::vector<std::string>& names = m_ReplayNames->Actions;
    for (usize i = 0; i < names.size() && i < m_ReplaySample->Actions.size(); ++i) {
        if (names[i] == action)
            return m_ReplaySample->Actions[i];
    }
    return 0;
}

f32 Input::ReplayAxis(std::string_view axis) const
{
    const std::vector<std::string>& names = m_ReplayNames->Axes;
    for (usize i = 0; i < names.size() && i < m_ReplaySample->Axes.size(); ++i) {
        if (names[i] == axis)
            return m_ReplaySample->Axes[i];
    }
    return 0.0f;
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
    m_Actions.push_back({std::string(name), {}, {}, {}});
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

#include "Emerald/Input/Input.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "Emerald/Save/Save.h"

namespace Emerald {

namespace {

template <typename T> void AddUnique(std::vector<T>& list, std::span<const T> items)
{
    for (const T& item : items) {
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }
}

template <typename T, typename Item>
std::vector<std::string_view> ActionsUsing(const std::vector<T>& actions, const Item& item,
                                           std::vector<Item> T::* list)
{
    std::vector<std::string_view> names;
    for (const T& a : actions) {
        const std::vector<Item>& items = a.*list;
        if (std::find(items.begin(), items.end(), item) != items.end())
            names.push_back(a.Name);
    }
    return names;
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

bool Input::TryRebindAction(std::string_view action, std::span<const Key> keys)
{
    const Action* a = FindAction(action);
    if (keys.empty() && !(a && (!a->Buttons.empty() || !a->MouseButtons.empty())))
        return false; // the last binding: keep it
    RebindAction(action, keys);
    return true;
}

bool Input::TryRebindAction(std::string_view action, std::span<const GamepadButton> buttons)
{
    const Action* a = FindAction(action);
    if (buttons.empty() && !(a && (!a->Keys.empty() || !a->MouseButtons.empty())))
        return false; // the last binding: keep it
    RebindAction(action, buttons);
    return true;
}

bool Input::TryRebindAction(std::string_view action, std::span<const MouseButton> buttons)
{
    const Action* a = FindAction(action);
    if (buttons.empty() && !(a && (!a->Keys.empty() || !a->Buttons.empty())))
        return false; // the last binding: keep it
    RebindAction(action, buttons);
    return true;
}

bool Input::IsActionBound(std::string_view action) const
{
    const Action* a = FindAction(action);
    return a && (!a->Keys.empty() || !a->Buttons.empty() || !a->MouseButtons.empty());
}

Input::Snapshot Input::SaveSnapshot() const
{
    Snapshot snapshot;
    snapshot.m_Actions = m_Actions;
    snapshot.m_Axes = m_Axes;
    return snapshot;
}

void Input::RestoreSnapshot(const Snapshot& snapshot, std::span<const std::string_view> names)
{
    if (names.empty()) {
        m_Actions = snapshot.m_Actions;
        m_Axes = snapshot.m_Axes;
        return;
    }
    // Each name in place (binding order is kept), or appended if it is new again.
    const auto restore = [&](auto& current, const auto& saved, std::string_view name) {
        const auto was = std::ranges::find_if(saved, [&](const auto& b) { return b.Name == name; });
        const auto now =
            std::ranges::find_if(current, [&](const auto& b) { return b.Name == name; });
        if (was == saved.end()) {
            if (now != current.end())
                current.erase(now);
        } else if (now != current.end()) {
            *now = *was;
        } else {
            current.push_back(*was);
        }
    };
    for (const std::string_view name : names) {
        restore(m_Actions, snapshot.m_Actions, name);
        restore(m_Axes, snapshot.m_Axes, name);
    }
}

void Input::RebindAxis(std::string_view axis, std::span<const AxisPair<Key>> pairs)
{
    GetOrAddAxis(axis).KeyPairs.assign(pairs.begin(), pairs.end());
}

void Input::RebindAxis(std::string_view axis, std::span<const AxisPair<GamepadButton>> pairs)
{
    GetOrAddAxis(axis).ButtonPairs.assign(pairs.begin(), pairs.end());
}

std::span<const AxisPair<Key>> Input::GetAxisKeyPairs(std::string_view axis) const
{
    const Axis* a = FindAxis(axis);
    return a ? std::span<const AxisPair<Key>>(a->KeyPairs) : std::span<const AxisPair<Key>>();
}

std::span<const AxisPair<GamepadButton>> Input::GetAxisButtonPairs(std::string_view axis) const
{
    const Axis* a = FindAxis(axis);
    return a ? std::span<const AxisPair<GamepadButton>>(a->ButtonPairs)
             : std::span<const AxisPair<GamepadButton>>();
}

std::span<const AxisAnalog> Input::GetAxisAnalogs(std::string_view axis) const
{
    const Axis* a = FindAxis(axis);
    return a ? std::span<const AxisAnalog>(a->Analogs) : std::span<const AxisAnalog>();
}

std::vector<std::string_view> Input::FindActionsUsing(Key key) const
{
    return ActionsUsing(m_Actions, key, &Action::Keys);
}

std::vector<std::string_view> Input::FindActionsUsing(GamepadButton button) const
{
    return ActionsUsing(m_Actions, button, &Action::Buttons);
}

std::vector<std::string_view> Input::FindActionsUsing(MouseButton button) const
{
    return ActionsUsing(m_Actions, button, &Action::MouseButtons);
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
    for (const AxisPair<Key>& pair : a.KeyPairs) {
        negative = negative || m_Keyboard.IsKeyDown(pair.Negative);
        positive = positive || m_Keyboard.IsKeyDown(pair.Positive);
    }
    for (const AxisPair<GamepadButton>& pair : a.ButtonPairs) {
        negative = negative || m_Gamepads.IsButtonDown(pair.Negative);
        positive = positive || m_Gamepads.IsButtonDown(pair.Positive);
    }
    f32 value = (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    // Analog bindings: the largest magnitude wins (a full key press beats a half-tilted stick).
    for (const AxisAnalog& analog : a.Analogs) {
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
    assert(SaveData::IsValidKey(name) && "action names are letters, digits and . _ -");
    for (Action& a : m_Actions) {
        if (a.Name == name)
            return a;
    }
    m_Actions.push_back({std::string(name), {}, {}, {}});
    return m_Actions.back();
}

Input::Axis& Input::GetOrAddAxis(std::string_view name)
{
    assert(SaveData::IsValidKey(name) && "axis names are letters, digits and . _ -");
    for (Axis& a : m_Axes) {
        if (a.Name == name)
            return a;
    }
    m_Axes.push_back({std::string(name), {}, {}, {}});
    return m_Axes.back();
}

} // namespace Emerald

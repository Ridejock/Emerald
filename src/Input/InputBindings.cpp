#include "Emerald/Input/InputBindings.h"

#include <array>
#include <charconv>
#include <vector>

#include <SDL3/SDL_keyboard.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// In GamepadButton order.
constexpr std::array<std::string_view, static_cast<usize>(GamepadButton::Count)> kButtonNames = {
    "South",           "East",           "West",         "North",          "Back",
    "Guide",           "Start",          "LeftStick",    "RightStick",     "LeftShoulder",
    "RightShoulder",   "DPadUp",         "DPadDown",     "DPadLeft",       "DPadRight",
    "Misc1",           "RightPaddle1",   "LeftPaddle1",  "RightPaddle2",   "LeftPaddle2",
    "Touchpad",        "Misc2",          "Misc3",        "Misc4",          "Misc5",
    "Misc6",           "LeftTrigger",    "RightTrigger", "LeftStickUp",    "LeftStickDown",
    "LeftStickLeft",   "LeftStickRight", "RightStickUp", "RightStickDown", "RightStickLeft",
    "RightStickRight",
};
// A new SDL button would shift the virtual ones: keep the names in step.
static_assert(static_cast<usize>(GamepadButton::LeftTrigger) == 26);

struct MouseName {
    MouseButton Button;
    std::string_view Name;
};
constexpr std::array<MouseName, 5> kMouseNames = {{
    {MouseButton::Left, "Left"},
    {MouseButton::Middle, "Middle"},
    {MouseButton::Right, "Right"},
    {MouseButton::X1, "X1"},
    {MouseButton::X2, "X2"},
}};

constexpr std::string_view kSeparator = ", ";

// "a, b, c" -> {"a", "b", "c"}. Splitting on ", " (not on ',') keeps SDL's "," key name intact:
// "Space, ,, Z" is Space, comma and Z.
std::vector<std::string_view> Split(std::string_view list)
{
    std::vector<std::string_view> names;
    while (!list.empty()) {
        const usize end = list.find(kSeparator, 1);
        names.push_back(list.substr(0, end));
        if (end == std::string_view::npos)
            break;
        list.remove_prefix(end + kSeparator.size());
    }
    return names;
}

template <typename T, typename NameFn> std::string Join(std::span<const T> items, NameFn name)
{
    std::string out;
    for (const T& item : items) {
        if (!out.empty())
            out += kSeparator;
        out += name(item);
    }
    return out;
}

// Reads one saved list into `out`. All or nothing: false (and `out` empty) if any name is
// unknown, so a list from a newer game or an SDL rename can't leave an action half-bound.
template <typename T, typename FindFn>
bool ReadList(const std::string& list, std::string_view key, std::vector<T>& out, FindFn find)
{
    for (const std::string_view name : Split(list)) {
        if (const std::optional<T> item = find(name)) {
            out.push_back(*item);
        } else {
            EM_CORE_WARN("LoadBindings: {}: unknown name '{}', the default bindings are kept", key,
                         name);
            out.clear();
            return false;
        }
    }
    return true;
}

// "<prefix>.<name>", or empty (and logged) if that can't be a save key.
std::string BaseKey(std::string_view prefix, std::string_view name)
{
    std::string key = std::string(prefix) + "." + std::string(name);
    if (SaveData::IsValidKey(key))
        return key;
    EM_CORE_ERROR("SaveBindings: '{}' is not saved: its name must be letters, digits and . _ -",
                  name);
    return {};
}

// Splits pairs into the two parallel lists they are saved as.
template <typename T, typename NameFn>
void WritePairs(SaveData& data, const std::string& key, std::span<const AxisPair<T>> pairs,
                NameFn name)
{
    std::vector<T> negative, positive;
    for (const AxisPair<T>& pair : pairs) {
        negative.push_back(pair.Negative);
        positive.push_back(pair.Positive);
    }
    data.SetString(key + ".negative", Join(std::span<const T>(negative), name));
    data.SetString(key + ".positive", Join(std::span<const T>(positive), name));
}

// Reads both lists back into pairs; false if one is missing, has unknown names, or they differ
// in length.
template <typename T, typename FindFn>
bool ReadPairs(const SaveData& data, const std::string& key, std::vector<AxisPair<T>>& pairs,
               FindFn find)
{
    std::vector<T> negative, positive;
    if (!ReadList(data.GetString(key + ".negative"), key + ".negative", negative, find) ||
        !ReadList(data.GetString(key + ".positive"), key + ".positive", positive, find))
        return false;
    if (negative.size() != positive.size()) {
        EM_CORE_WARN("LoadAxisBindings: {}: the lists differ in length, the defaults are kept",
                     key);
        return false;
    }
    for (usize i = 0; i < negative.size(); ++i)
        pairs.push_back({negative[i], positive[i]});
    return true;
}

} // namespace

// --- Names ---

std::string GetKeyName(Key key)
{
    // SDL's name, unless it has none or shares it with another key (both Return keys are
    // "Return"): then the number, so every key reads back as itself.
    const auto code = static_cast<SDL_Scancode>(key);
    const char* name = SDL_GetScancodeName(code);
    if (name && *name && SDL_GetScancodeFromName(name) == code)
        return name;
    return "#" + std::to_string(static_cast<u32>(key));
}

std::optional<Key> FindKey(std::string_view name)
{
    if (name.size() > 1 && name.front() == '#') {
        u32 code = 0;
        const char* last = name.data() + name.size();
        const auto [end, error] = std::from_chars(name.data() + 1, last, code);
        if (error != std::errc() || end != last || code == 0 || code >= SDL_SCANCODE_COUNT)
            return std::nullopt;
        return static_cast<Key>(code);
    }
    const SDL_Scancode code = SDL_GetScancodeFromName(std::string(name).c_str());
    if (code == SDL_SCANCODE_UNKNOWN)
        return std::nullopt;
    return static_cast<Key>(code);
}

std::string GetKeyLabel(Key key)
{
    const SDL_Keycode code =
        SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(key), SDL_KMOD_NONE, false);
    const char* label = SDL_GetKeyName(code);
    return label && *label ? std::string(label) : GetKeyName(key);
}

std::string_view GetGamepadButtonName(GamepadButton button)
{
    const usize index = static_cast<usize>(button);
    return index < kButtonNames.size() ? kButtonNames[index] : std::string_view();
}

std::optional<GamepadButton> FindGamepadButton(std::string_view name)
{
    for (usize i = 0; i < kButtonNames.size(); ++i) {
        if (kButtonNames[i] == name)
            return static_cast<GamepadButton>(i);
    }
    return std::nullopt;
}

std::string_view GetMouseButtonName(MouseButton button)
{
    for (const MouseName& m : kMouseNames) {
        if (m.Button == button)
            return m.Name;
    }
    return {};
}

std::optional<MouseButton> FindMouseButton(std::string_view name)
{
    for (const MouseName& m : kMouseNames) {
        if (m.Name == name)
            return m.Button;
    }
    return std::nullopt;
}

// --- Saving and loading ---

bool SaveBindings(const Input& input, SaveData& data, std::span<const std::string_view> actions,
                  std::string_view prefix)
{
    bool ok = true;
    for (const std::string_view action : actions) {
        const std::string key = BaseKey(prefix, action);
        if (key.empty()) {
            ok = false;
            continue;
        }
        data.SetString(key + ".keys", Join(input.GetActionKeys(action), GetKeyName));
        data.SetString(key + ".buttons",
                       Join(input.GetActionButtons(action), GetGamepadButtonName));
        data.SetString(key + ".mouse",
                       Join(input.GetActionMouseButtons(action), GetMouseButtonName));
    }
    return ok;
}

bool LoadBindings(Input& input, const SaveData& data, std::span<const std::string_view> actions,
                  std::string_view prefix)
{
    bool ok = true;
    for (const std::string_view action : actions) {
        const std::string base = std::string(prefix) + "." + std::string(action);
        if (const std::string key = base + ".keys"; data.Has(key)) {
            std::vector<Key> keys;
            if (ReadList(data.GetString(key), key, keys, FindKey))
                input.RebindAction(action, std::span<const Key>(keys));
            else
                ok = false;
        }
        if (const std::string key = base + ".buttons"; data.Has(key)) {
            std::vector<GamepadButton> buttons;
            if (ReadList(data.GetString(key), key, buttons, FindGamepadButton))
                input.RebindAction(action, std::span<const GamepadButton>(buttons));
            else
                ok = false;
        }
        if (const std::string key = base + ".mouse"; data.Has(key)) {
            std::vector<MouseButton> buttons;
            if (ReadList(data.GetString(key), key, buttons, FindMouseButton))
                input.RebindAction(action, std::span<const MouseButton>(buttons));
            else
                ok = false;
        }
    }
    return ok;
}

bool SaveAxisBindings(const Input& input, SaveData& data, std::span<const std::string_view> axes,
                      std::string_view prefix)
{
    bool ok = true;
    for (const std::string_view axis : axes) {
        const std::string key = BaseKey(prefix, axis);
        if (key.empty()) {
            ok = false;
            continue;
        }
        WritePairs(data, key, input.GetAxisKeyPairs(axis), GetKeyName);
        WritePairs(data, key + ".pad", input.GetAxisButtonPairs(axis), GetGamepadButtonName);
    }
    return ok;
}

bool LoadAxisBindings(Input& input, const SaveData& data, std::span<const std::string_view> axes,
                      std::string_view prefix)
{
    bool ok = true;
    for (const std::string_view axis : axes) {
        const std::string base = std::string(prefix) + "." + std::string(axis);
        if (data.Has(base + ".negative") || data.Has(base + ".positive")) {
            std::vector<AxisPair<Key>> pairs;
            if (ReadPairs(data, base, pairs, FindKey))
                input.RebindAxis(axis, std::span<const AxisPair<Key>>(pairs));
            else
                ok = false;
        }
        if (data.Has(base + ".pad.negative") || data.Has(base + ".pad.positive")) {
            std::vector<AxisPair<GamepadButton>> pairs;
            if (ReadPairs(data, base + ".pad", pairs, FindGamepadButton))
                input.RebindAxis(axis, std::span<const AxisPair<GamepadButton>>(pairs));
            else
                ok = false;
        }
    }
    return ok;
}

} // namespace Emerald

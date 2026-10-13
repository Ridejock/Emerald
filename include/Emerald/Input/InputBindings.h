#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "Emerald/Core/Defines.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Save/Save.h"

namespace Emerald {

// Saving rebound controls with the save system (Save.h), e.g. from an options screen:
//
//   constexpr std::array<std::string_view, 2> kRebindable = {"Jump", "Fire"};
//   SaveBindings(input, settings, kRebindable);   // "input.Jump.keys = Space, Z" ...
//   saves.Save("settings", settings);
//
//   BindControls(input);                          // at startup: the defaults first,
//   LoadBindings(input, loaded.Data, kRebindable); // then whatever the player changed
//
// Each action is stored as three values: "<prefix>.<action>.keys", ".buttons" and ".mouse",
// each a ", "-separated list of names (empty = nothing bound). Actions without saved values keep
// their bindings, so actions added in a later version get their defaults, and a list with a name
// that isn't known (a newer game, a renamed SDL key) is skipped whole, keeping the defaults.
//
// Axes (SaveAxisBindings) are stored as parallel lists, one entry per pair:
// "<prefix>.<axis>.negative = A, Left" / ".positive = D, Right", and ".pad.negative" /
// ".pad.positive" for gamepad button pairs. Analog sticks are not saved.

// Stable names for settings files: SDL's scancode names for keys ("Space", "Left Shift", "A",
// with "#<scancode>" for keys SDL has no name, or no unique name, for), and the enum names for
// buttons ("South", "DPadUp", "LeftTrigger"; mouse "Left", "X1"). The Find functions read them
// back.
[[nodiscard]] std::string GetKeyName(Key key);
[[nodiscard]] std::optional<Key> FindKey(std::string_view name);
[[nodiscard]] std::string_view GetGamepadButtonName(GamepadButton button);
[[nodiscard]] std::optional<GamepadButton> FindGamepadButton(std::string_view name);
[[nodiscard]] std::string_view GetMouseButtonName(MouseButton button);
[[nodiscard]] std::optional<MouseButton> FindMouseButton(std::string_view name);

// What is printed on the key with the current keyboard layout, for prompts and options screens:
// Key::W shows "Z" on an AZERTY keyboard (GetKeyName stays "W", the physical key).
[[nodiscard]] std::string GetKeyLabel(Key key);

// Writes the bindings of `actions` into `data` under `prefix` (letters, digits, . _ -). False (and
// logged) if an action's name can't be a save key; the others are still written.
bool SaveBindings(const Input& input, SaveData& data, std::span<const std::string_view> actions,
                  std::string_view prefix = "input");
// Rebinds each of `actions` from `data`, where values were saved. A list with an unknown name is
// not applied (and logged); returns false if there were any.
bool LoadBindings(Input& input, const SaveData& data, std::span<const std::string_view> actions,
                  std::string_view prefix = "input");

// The same for the key and gamepad button pairs of `axes`.
bool SaveAxisBindings(const Input& input, SaveData& data, std::span<const std::string_view> axes,
                      std::string_view prefix = "input");
bool LoadAxisBindings(Input& input, const SaveData& data, std::span<const std::string_view> axes,
                      std::string_view prefix = "input");

} // namespace Emerald

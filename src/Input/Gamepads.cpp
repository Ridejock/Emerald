#include "Emerald/Input/Gamepads.h"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL_error.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// Virtual buttons release a little below the press threshold, so a value hovering right at the
// threshold does not flicker between pressed and released.
constexpr f32 kReleaseMargin = 0.1f;

enum class Family : u8 { Xbox, PlayStation, Nintendo };

Family GetFamily(SDL_GamepadType type)
{
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return Family::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return Family::Nintendo;
    default:
        return Family::Xbox; // Xbox-style labels for Xbox and generic pads
    }
}

// Picks the label for the pad's family.
const char* Pick(SDL_GamepadType type, const char* xbox, const char* playStation,
                 const char* nintendo)
{
    switch (GetFamily(type)) {
    case Family::PlayStation:
        return playStation;
    case Family::Nintendo:
        return nintendo;
    default:
        return xbox;
    }
}

const char* FaceLabel(SDL_GamepadButtonLabel label)
{
    switch (label) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
        return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
        return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
        return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
        return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        return "Triangle";
    default:
        return "?";
    }
}

} // namespace

f32 ApplyDeadzone(f32 value, f32 deadzone)
{
    deadzone = std::clamp(deadzone, 0.0f, 0.99f);
    const f32 magnitude = std::abs(value);
    if (magnitude <= deadzone)
        return 0.0f;
    // Maps deadzone..1 to 0..1.
    const f32 scaled = std::min((magnitude - deadzone) / (1.0f - deadzone), 1.0f);
    return value < 0.0f ? -scaled : scaled;
}

Vec2 ApplyRadialDeadzone(Vec2 stick, f32 deadzone)
{
    deadzone = std::clamp(deadzone, 0.0f, 0.99f);
    const f32 length = Length(stick);
    if (length <= deadzone)
        return {};
    // Same remapping as ApplyDeadzone, on the length; the direction stays as it is.
    const f32 scaled = std::min((length - deadzone) / (1.0f - deadzone), 1.0f);
    return stick * (scaled / length);
}

const char* GetGamepadTypeName(SDL_GamepadType type)
{
    switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
        return "Xbox 360";
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return "Xbox One/Series";
    case SDL_GAMEPAD_TYPE_PS3:
        return "PS3";
    case SDL_GAMEPAD_TYPE_PS4:
        return "PS4";
    case SDL_GAMEPAD_TYPE_PS5:
        return "PS5";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        return "Switch Pro";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        return "Joy-Con (L)";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        return "Joy-Con (R)";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return "Joy-Con pair";
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return "GameCube";
    case SDL_GAMEPAD_TYPE_STANDARD:
        return "Standard";
    default:
        return "Unknown";
    }
}

const char* GetGamepadButtonLabel(GamepadButton button, SDL_GamepadType type)
{
    using B = GamepadButton;
    switch (button) {
    case B::South:
    case B::East:
    case B::West:
    case B::North:
        // SDL knows the face button labels, including Nintendo's swapped A/B and X/Y.
        return FaceLabel(
            SDL_GetGamepadButtonLabelForType(type, static_cast<SDL_GamepadButton>(button)));
    case B::Back:
        return Pick(type, type == SDL_GAMEPAD_TYPE_XBOX360 ? "Back" : "View",
                    type == SDL_GAMEPAD_TYPE_PS5 ? "Create" : "Share", "-");
    case B::Guide:
        return Pick(type, "Xbox", "PS", "Home");
    case B::Start:
        return Pick(type, type == SDL_GAMEPAD_TYPE_XBOX360 ? "Start" : "Menu", "Options", "+");
    case B::LeftStick:
        return Pick(type, "LS", "L3", "LS");
    case B::RightStick:
        return Pick(type, "RS", "R3", "RS");
    case B::LeftShoulder:
        return Pick(type, "LB", "L1", "L");
    case B::RightShoulder:
        return Pick(type, "RB", "R1", "R");
    case B::LeftTrigger:
        return Pick(type, "LT", "L2", "ZL");
    case B::RightTrigger:
        return Pick(type, "RT", "R2", "ZR");
    case B::DPadUp:
        return "D-pad Up";
    case B::DPadDown:
        return "D-pad Down";
    case B::DPadLeft:
        return "D-pad Left";
    case B::DPadRight:
        return "D-pad Right";
    case B::Misc1:
        return Pick(type, "Share", "Mic", "Capture");
    case B::Touchpad:
        return "Touchpad";
    case B::LeftStickUp:
        return "Left Stick Up";
    case B::LeftStickDown:
        return "Left Stick Down";
    case B::LeftStickLeft:
        return "Left Stick Left";
    case B::LeftStickRight:
        return "Left Stick Right";
    case B::RightStickUp:
        return "Right Stick Up";
    case B::RightStickDown:
        return "Right Stick Down";
    case B::RightStickLeft:
        return "Right Stick Left";
    case B::RightStickRight:
        return "Right Stick Right";
    default:
        return "?";
    }
}

Gamepads::~Gamepads()
{
    CloseAll();
}

f32 Gamepads::GetAxis(GamepadAxis axis) const
{
    f32 best = 0.0f;
    for (const Pad& pad : m_Pads) {
        const f32 v = Deadzoned(pad, axis);
        if (std::abs(v) > std::abs(best))
            best = v;
    }
    return best;
}

f32 Gamepads::GetRawAxis(usize pad, GamepadAxis axis) const
{
    return m_Pads[pad].Axes[static_cast<usize>(axis)];
}

f32 Gamepads::GetAxis(usize pad, GamepadAxis axis) const
{
    return Deadzoned(m_Pads[pad], axis);
}

void Gamepads::Rumble(f32 low, f32 high, u32 milliseconds)
{
    const auto toMotor = [](f32 v) {
        return static_cast<u16>(std::clamp(v, 0.0f, 1.0f) * 65535.0f);
    };
    for (const Pad& pad : m_Pads) {
        if (pad.Handle)
            SDL_RumbleGamepad(pad.Handle, toMotor(low), toMotor(high), milliseconds);
    }
}

void Gamepads::Open(SDL_JoystickID id)
{
    if (Find(id))
        return;
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) {
        EM_CORE_WARN("Could not open gamepad {}: {}", id, SDL_GetError());
        return;
    }
    const char* name = SDL_GetGamepadName(handle);
    const SDL_GamepadType type = SDL_GetGamepadType(handle);
    AddPad(id, handle, name ? name : "Gamepad", type);
    EM_CORE_INFO("Gamepad connected: {} ({}), rumble: {}", name ? name : "Gamepad",
                 GetGamepadTypeName(type),
                 SDL_GetBooleanProperty(SDL_GetGamepadProperties(handle),
                                        SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)
                     ? "yes"
                     : "no");
}

void Gamepads::Close(SDL_JoystickID id)
{
    Pad* pad = Find(id);
    if (!pad)
        return;
    if (pad->Handle) {
        EM_CORE_INFO("Gamepad disconnected: {} ({})", pad->Info.Name,
                     GetGamepadTypeName(pad->Info.Type));
        SDL_CloseGamepad(pad->Handle);
    }
    const std::bitset<kButtonCount> held = pad->Buttons;
    std::erase_if(m_Pads, [id](const Pad& p) { return p.Info.Id == id; });
    // Whatever the pad was holding is released now (unless another pad holds it too).
    for (usize i = 0; i < kButtonCount; ++i) {
        if (held[i])
            RefreshButton(i);
    }
}

void Gamepads::CloseAll()
{
    while (!m_Pads.empty())
        Close(m_Pads.back().Info.Id);
}

void Gamepads::AddPad(SDL_JoystickID id, SDL_Gamepad* handle, std::string name,
                      SDL_GamepadType type)
{
    if (Find(id))
        return;
    Pad pad;
    pad.Info = {id, std::move(name), type};
    pad.Handle = handle;
    m_Pads.push_back(std::move(pad));
    if (m_LastUsedType == SDL_GAMEPAD_TYPE_UNKNOWN)
        m_LastUsedType = type;
}

void Gamepads::OnButton(SDL_JoystickID id, GamepadButton button, bool down)
{
    Pad* pad = Find(id);
    if (!pad || Index(button) >= static_cast<usize>(SDL_GAMEPAD_BUTTON_COUNT))
        return; // virtual buttons only come from axes
    if (down)
        m_LastUsedType = pad->Info.Type;
    SetPadButton(*pad, Index(button), down);
}

void Gamepads::OnAxis(SDL_JoystickID id, GamepadAxis axis, f32 value)
{
    Pad* pad = Find(id);
    if (!pad || static_cast<usize>(axis) >= kAxisCount)
        return;
    pad->Axes[static_cast<usize>(axis)] = std::clamp(value, -1.0f, 1.0f);
    if (Deadzoned(*pad, axis) != 0.0f)
        m_LastUsedType = pad->Info.Type;
    UpdateVirtualButtons(*pad);
}

void Gamepads::ReleaseAll()
{
    for (Pad& pad : m_Pads) {
        pad.Buttons.reset();
        pad.Axes = {};
    }
    m_Buttons.ReleaseAll();
}

Gamepads::Pad* Gamepads::Find(SDL_JoystickID id)
{
    for (Pad& pad : m_Pads) {
        if (pad.Info.Id == id)
            return &pad;
    }
    return nullptr;
}

f32 Gamepads::Deadzoned(const Pad& pad, GamepadAxis axis) const
{
    const auto raw = [&](GamepadAxis a) { return pad.Axes[static_cast<usize>(a)]; };
    switch (axis) {
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger:
        return ApplyDeadzone(raw(axis), m_Settings.TriggerDeadzone);
    default:
        break;
    }
    if (!m_Settings.RadialDeadzone)
        return ApplyDeadzone(raw(axis), m_Settings.StickDeadzone);
    // A radial deadzone needs both axes of the stick.
    const bool left = axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY;
    const Vec2 stick = left ? Vec2(raw(GamepadAxis::LeftX), raw(GamepadAxis::LeftY))
                            : Vec2(raw(GamepadAxis::RightX), raw(GamepadAxis::RightY));
    const Vec2 v = ApplyRadialDeadzone(stick, m_Settings.StickDeadzone);
    const bool x = axis == GamepadAxis::LeftX || axis == GamepadAxis::RightX;
    return x ? v.x : v.y;
}

void Gamepads::SetPadButton(Pad& pad, usize button, bool down)
{
    if (pad.Buttons[button] == down)
        return;
    pad.Buttons[button] = down;
    RefreshButton(button);
}

void Gamepads::UpdateVirtualButtons(Pad& pad)
{
    // Down once `value` reaches the threshold, up again once it drops below threshold - margin.
    const f32 threshold = m_Settings.ButtonThreshold;
    const auto update = [&](GamepadButton button, f32 value) {
        const usize i = Index(button);
        const bool down = pad.Buttons[i] ? value > threshold - kReleaseMargin : value >= threshold;
        SetPadButton(pad, i, down);
    };
    const f32 lx = Deadzoned(pad, GamepadAxis::LeftX);
    const f32 ly = Deadzoned(pad, GamepadAxis::LeftY);
    const f32 rx = Deadzoned(pad, GamepadAxis::RightX);
    const f32 ry = Deadzoned(pad, GamepadAxis::RightY);
    update(GamepadButton::LeftTrigger, Deadzoned(pad, GamepadAxis::LeftTrigger));
    update(GamepadButton::RightTrigger, Deadzoned(pad, GamepadAxis::RightTrigger));
    update(GamepadButton::LeftStickUp, -ly); // +Y is down
    update(GamepadButton::LeftStickDown, ly);
    update(GamepadButton::LeftStickLeft, -lx);
    update(GamepadButton::LeftStickRight, lx);
    update(GamepadButton::RightStickUp, -ry);
    update(GamepadButton::RightStickDown, ry);
    update(GamepadButton::RightStickLeft, -rx);
    update(GamepadButton::RightStickRight, rx);
}

void Gamepads::RefreshButton(usize button)
{
    bool down = false;
    for (const Pad& pad : m_Pads)
        down = down || pad.Buttons[button];
    m_Buttons.Set(button, down);
}

} // namespace Emerald

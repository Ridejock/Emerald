#include "Emerald/Input/Gamepads.h"

#include <algorithm>
#include <cmath>
#include <string_view>

#include <SDL3/SDL_error.h>

#include "Emerald/Core/Log.h"
#include "Platform/SdlCast.h"

namespace Emerald {

// Paddle values have to stay in the holes the binding names already use (InputBindings.cpp).
static_assert(static_cast<int>(GamepadButton::RightPaddle1) == SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
static_assert(static_cast<int>(GamepadButton::LeftPaddle2) == SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
static_assert(static_cast<int>(GamepadButton::LeftTrigger) == SDL_GAMEPAD_BUTTON_COUNT);

namespace {

// Virtual buttons release a little below the press threshold, so a value hovering right at the
// threshold does not flicker between pressed and released.
constexpr f32 kReleaseMargin = 0.1f;

enum class Family : u8 { Xbox, PlayStation, Nintendo };

Family GetFamily(GamepadType type)
{
    switch (type) {
    case GamepadType::PS3:
    case GamepadType::PS4:
    case GamepadType::PS5:
        return Family::PlayStation;
    case GamepadType::SwitchPro:
    case GamepadType::JoyConLeft:
    case GamepadType::JoyConRight:
    case GamepadType::JoyConPair:
    case GamepadType::GameCube:
        return Family::Nintendo;
    default:
        return Family::Xbox; // Xbox-style labels for Xbox and generic pads
    }
}

// Picks the label for the pad's family.
const char* Pick(GamepadType type, const char* xbox, const char* playStation,
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

const char* GetGamepadTypeName(GamepadType type)
{
    switch (type) {
    case GamepadType::Xbox360:
        return "Xbox 360";
    case GamepadType::XboxOne:
        return "Xbox One/Series";
    case GamepadType::PS3:
        return "PS3";
    case GamepadType::PS4:
        return "PS4";
    case GamepadType::PS5:
        return "PS5";
    case GamepadType::SwitchPro:
        return "Switch Pro";
    case GamepadType::JoyConLeft:
        return "Joy-Con (L)";
    case GamepadType::JoyConRight:
        return "Joy-Con (R)";
    case GamepadType::JoyConPair:
        return "Joy-Con pair";
    case GamepadType::GameCube:
        return "GameCube";
    case GamepadType::Standard:
        return "Standard";
    case GamepadType::Unknown:
    default:
        return "Unknown";
    }
}

const char* GetGamepadButtonLabel(GamepadButton button, GamepadType type)
{
    using B = GamepadButton;
    switch (button) {
    case B::South:
    case B::East:
    case B::West:
    case B::North:
        // SDL knows the face button labels, including Nintendo's swapped A/B and X/Y.
        return FaceLabel(SDL_GetGamepadButtonLabelForType(Sdl(type), Sdl(button)));
    case B::Back:
        return Pick(type, type == GamepadType::Xbox360 ? "Back" : "View",
                    type == GamepadType::PS5 ? "Create" : "Share", "-");
    case B::Guide:
        return Pick(type, "Xbox", "PS", "Home");
    case B::Start:
        return Pick(type, type == GamepadType::Xbox360 ? "Start" : "Menu", "Options", "+");
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
    case B::RightPaddle1:
        return Pick(type, "P1", "R4", "SR");
    case B::LeftPaddle1:
        return Pick(type, "P3", "L4", "SL");
    case B::RightPaddle2:
        return Pick(type, "P2", "R5", "SL");
    case B::LeftPaddle2:
        return Pick(type, "P4", "L5", "SR");
    case B::Touchpad:
        return "Touchpad";
    case B::Misc2:
    case B::Misc3:
    case B::Misc4:
    case B::Misc5:
    case B::Misc6:
        return "Misc";
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

bool IsSteamDeckGamepad(std::string_view name, u16 vendor, u16 product)
{
    // Valve Jupiter, the Deck's built-in controller (SDL's controller list).
    constexpr u16 kValve = 0x28DE;
    constexpr u16 kSteamDeck = 0x1205;
    if (vendor == kValve && product == kSteamDeck)
        return true;
    return name.find("Steam Deck") != std::string_view::npos;
}

const char* GetSteamDeckButtonLabel(GamepadButton button)
{
    using B = GamepadButton;
    switch (button) {
    case B::South:
    case B::East:
    case B::West:
    case B::North:
        // Printed on the Deck, and the same positions SDL's STANDARD type already uses.
        return GetGamepadButtonLabel(button, GamepadType::XboxOne);
    case B::Back:
        return "View";
    case B::Guide:
        return "Steam";
    case B::Start:
        return "Menu";
    case B::LeftShoulder:
        return "L1";
    case B::RightShoulder:
        return "R1";
    case B::LeftTrigger:
        return "L2";
    case B::RightTrigger:
        return "R2";
    case B::Misc1:
        return "QAM";
    case B::RightPaddle1:
        return "R4";
    case B::LeftPaddle1:
        return "L4";
    case B::RightPaddle2:
        return "R5";
    case B::LeftPaddle2:
        return "L5";
    case B::Touchpad:
        return "Left Pad";
    case B::Misc2:
        return "Right Pad";
    default:
        return GetGamepadButtonLabel(button, GamepadType::XboxOne);
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
            SDL_RumbleGamepad(Sdl(pad.Handle), toMotor(low), toMotor(high), milliseconds);
    }
}

void Gamepads::Open(GamepadId id)
{
    if (Find(id))
        return;
    SDL_Gamepad* handle = SDL_OpenGamepad(static_cast<SDL_JoystickID>(id));
    if (!handle) {
        EM_CORE_WARN("Could not open gamepad {}: {}", id, SDL_GetError());
        return;
    }
    const char* name = SDL_GetGamepadName(handle);
    const GamepadType type = FromSdl(SDL_GetGamepadType(handle));
    const std::string padName = name ? name : "Gamepad";
    const bool deck = IsSteamDeckGamepad(padName, static_cast<u16>(SDL_GetGamepadVendor(handle)),
                                         static_cast<u16>(SDL_GetGamepadProduct(handle)));
    AddPad(id, FromSdl(handle), padName, type);
    EM_CORE_INFO("Gamepad connected: {} ({}), rumble: {}", padName,
                 deck ? "Steam Deck" : GetGamepadTypeName(type),
                 SDL_GetBooleanProperty(SDL_GetGamepadProperties(handle),
                                        SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)
                     ? "yes"
                     : "no");
}

void Gamepads::Close(GamepadId id)
{
    Pad* pad = Find(id);
    if (!pad)
        return;
    if (pad->Handle) {
        EM_CORE_INFO("Gamepad disconnected: {} ({})", pad->Info.Name,
                     GetGamepadTypeName(pad->Info.Type));
        SDL_CloseGamepad(Sdl(pad->Handle));
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

void Gamepads::AddSyntheticPad(u32 id, std::string name, GamepadType type)
{
    AddPad(id, nullptr, std::move(name), type);
}

void Gamepads::AddPad(GamepadId id, GamepadDevice* handle, std::string name, GamepadType type)
{
    if (Find(id))
        return;
    Pad pad;
    pad.Info = {id, std::move(name), type};
    pad.Handle = handle;
    m_Pads.push_back(std::move(pad));
    if (m_LastUsedType == GamepadType::Unknown)
        Remember(m_Pads.back());
}

void Gamepads::OnButton(GamepadId id, GamepadButton button, bool down)
{
    Pad* pad = Find(id);
    if (!pad || Index(button) >= static_cast<usize>(GamepadButton::LeftTrigger))
        return; // virtual buttons only come from axes
    if (down)
        Remember(*pad);
    SetPadButton(*pad, Index(button), down);
}

void Gamepads::OnAxis(GamepadId id, GamepadAxis axis, f32 value)
{
    Pad* pad = Find(id);
    if (!pad || static_cast<usize>(axis) >= kAxisCount)
        return;
    pad->Axes[static_cast<usize>(axis)] = std::clamp(value, -1.0f, 1.0f);
    if (Deadzoned(*pad, axis) != 0.0f)
        Remember(*pad);
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

const char* Gamepads::GetButtonLabel(GamepadButton button) const
{
    if (m_LastUsedSteamDeck)
        return GetSteamDeckButtonLabel(button);
    return GetGamepadButtonLabel(button, m_LastUsedType);
}

bool Gamepads::PadIsSteamDeck(const Pad& pad) const
{
    u16 vendor = 0;
    u16 product = 0;
    if (pad.Handle) {
        vendor = static_cast<u16>(SDL_GetGamepadVendor(Sdl(pad.Handle)));
        product = static_cast<u16>(SDL_GetGamepadProduct(Sdl(pad.Handle)));
    }
    return IsSteamDeckGamepad(pad.Info.Name, vendor, product);
}

void Gamepads::Remember(const Pad& pad)
{
    m_LastUsedType = pad.Info.Type;
    m_LastUsedSteamDeck = PadIsSteamDeck(pad);
}

Gamepads::Pad* Gamepads::Find(GamepadId id)
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

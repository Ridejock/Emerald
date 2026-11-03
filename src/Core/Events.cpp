#include "Platform/SdlCast.h"

namespace Emerald {
namespace {

// The public enums are plain numbers so a game never includes the backend headers. These lock
// the numbers to that backend, so a saved binding and a translated event stay the same value.
void AssertBackendValues()
{
    static_assert(static_cast<int>(Key::Unknown) == SDL_SCANCODE_UNKNOWN);
    static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A);
    static_assert(static_cast<int>(Key::Z) == SDL_SCANCODE_Z);
    static_assert(static_cast<int>(Key::Num0) == SDL_SCANCODE_0);
    static_assert(static_cast<int>(Key::Num1) == SDL_SCANCODE_1);
    static_assert(static_cast<int>(Key::Up) == SDL_SCANCODE_UP);
    static_assert(static_cast<int>(Key::Down) == SDL_SCANCODE_DOWN);
    static_assert(static_cast<int>(Key::Left) == SDL_SCANCODE_LEFT);
    static_assert(static_cast<int>(Key::Right) == SDL_SCANCODE_RIGHT);
    static_assert(static_cast<int>(Key::Space) == SDL_SCANCODE_SPACE);
    static_assert(static_cast<int>(Key::Enter) == SDL_SCANCODE_RETURN);
    static_assert(static_cast<int>(Key::Escape) == SDL_SCANCODE_ESCAPE);
    static_assert(static_cast<int>(Key::F1) == SDL_SCANCODE_F1);
    static_assert(static_cast<int>(Key::F12) == SDL_SCANCODE_F12);
    static_assert(static_cast<int>(Key::Minus) == SDL_SCANCODE_MINUS);
    static_assert(static_cast<int>(Key::Apostrophe) == SDL_SCANCODE_APOSTROPHE);
    static_assert(static_cast<int>(Key::Semicolon) == SDL_SCANCODE_SEMICOLON);
    static_assert(static_cast<int>(Key::Comma) == SDL_SCANCODE_COMMA);
    static_assert(static_cast<int>(Key::KpEnter) == SDL_SCANCODE_KP_ENTER);
    static_assert(kKeyCount == SDL_SCANCODE_COUNT);

    static_assert(static_cast<int>(MouseButton::Left) == SDL_BUTTON_LEFT);
    static_assert(static_cast<int>(MouseButton::Middle) == SDL_BUTTON_MIDDLE);
    static_assert(static_cast<int>(MouseButton::Right) == SDL_BUTTON_RIGHT);
    static_assert(static_cast<int>(MouseButton::X1) == SDL_BUTTON_X1);
    static_assert(static_cast<int>(MouseButton::X2) == SDL_BUTTON_X2);

    static_assert(static_cast<int>(GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH);
    static_assert(static_cast<int>(GamepadButton::Misc6) == SDL_GAMEPAD_BUTTON_MISC6);
    static_assert(static_cast<int>(GamepadButton::LeftTrigger) == SDL_GAMEPAD_BUTTON_COUNT);
    static_assert(static_cast<int>(GamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX);
    static_assert(static_cast<int>(GamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    static_assert(static_cast<int>(GamepadType::Unknown) == SDL_GAMEPAD_TYPE_UNKNOWN);
    static_assert(static_cast<int>(GamepadType::Standard) == SDL_GAMEPAD_TYPE_STANDARD);
    static_assert(static_cast<int>(GamepadType::Xbox360) == SDL_GAMEPAD_TYPE_XBOX360);
    static_assert(static_cast<int>(GamepadType::XboxOne) == SDL_GAMEPAD_TYPE_XBOXONE);
    static_assert(static_cast<int>(GamepadType::PS5) == SDL_GAMEPAD_TYPE_PS5);
    static_assert(static_cast<int>(GamepadType::SwitchPro) == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO);
    static_assert(static_cast<int>(GamepadType::GameCube) == SDL_GAMEPAD_TYPE_GAMECUBE);

    static_assert(static_cast<unsigned>(KeyMod::LShift) == SDL_KMOD_LSHIFT);
    static_assert(static_cast<unsigned>(KeyMod::RShift) == SDL_KMOD_RSHIFT);
    static_assert(static_cast<unsigned>(KeyMod::LCtrl) == SDL_KMOD_LCTRL);
    static_assert(static_cast<unsigned>(KeyMod::RAlt) == SDL_KMOD_RALT);
    static_assert(static_cast<unsigned>(KeyMod::Shift) == SDL_KMOD_SHIFT);
    static_assert(static_cast<unsigned>(KeyMod::Ctrl) == SDL_KMOD_CTRL);
    static_assert(static_cast<unsigned>(KeyMod::Alt) == SDL_KMOD_ALT);
    static_assert(static_cast<unsigned>(KeyMod::Gui) == SDL_KMOD_GUI);

    static_assert(static_cast<u32>(ShaderFormat::Private) == SDL_GPU_SHADERFORMAT_PRIVATE);
    static_assert(static_cast<u32>(ShaderFormat::Spirv) == SDL_GPU_SHADERFORMAT_SPIRV);
    static_assert(static_cast<u32>(ShaderFormat::Dxbc) == SDL_GPU_SHADERFORMAT_DXBC);
    static_assert(static_cast<u32>(ShaderFormat::Dxil) == SDL_GPU_SHADERFORMAT_DXIL);
    static_assert(static_cast<u32>(ShaderFormat::Msl) == SDL_GPU_SHADERFORMAT_MSL);
    static_assert(static_cast<u32>(ShaderFormat::Metallib) == SDL_GPU_SHADERFORMAT_METALLIB);

    static_assert(static_cast<int>(GpuTextureFormat::Invalid) == SDL_GPU_TEXTUREFORMAT_INVALID);
    static_assert(static_cast<int>(GpuTextureFormat::R8G8B8A8Unorm) ==
                  SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    static_assert(static_cast<int>(GpuTextureFormat::B8G8R8A8Unorm) ==
                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
    static_assert(static_cast<int>(GpuTextureFormat::R16G16B16A16Float) ==
                  SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
    static_assert(static_cast<int>(GpuTextureFormat::R8G8B8A8UnormSrgb) ==
                  SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
    static_assert(static_cast<int>(GpuTextureFormat::B8G8R8A8UnormSrgb) ==
                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB);

    static_assert(static_cast<u32>(GpuBufferUsage::Vertex) == SDL_GPU_BUFFERUSAGE_VERTEX);
    static_assert(static_cast<u32>(GpuBufferUsage::Index) == SDL_GPU_BUFFERUSAGE_INDEX);
    static_assert(static_cast<int>(VertexElementFormat::Float2) == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2);
    static_assert(static_cast<int>(VertexElementFormat::Float3) == SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3);
    static_assert(static_cast<int>(VertexElementFormat::UByte4Norm) ==
                  SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM);
    static_assert(static_cast<int>(VertexInputRate::Vertex) == SDL_GPU_VERTEXINPUTRATE_VERTEX);
    static_assert(static_cast<int>(PrimitiveType::TriangleList) == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);
    static_assert(static_cast<int>(PrimitiveType::LineList) == SDL_GPU_PRIMITIVETYPE_LINELIST);
    static_assert(sizeof(GamepadId) == sizeof(SDL_JoystickID));
}

const bool kValuesChecked = (AssertBackendValues(), true);

} // namespace

std::optional<Event> TranslateEvent(const SDL_Event& sdl)
{
    (void)kValuesChecked;
    Event event;
    switch (sdl.type) {
    case SDL_EVENT_QUIT:
        event.Type = EventType::Quit;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        event.Type = EventType::WindowCloseRequested;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
        event.Type = EventType::WindowResized;
        event.WindowSize = {sdl.window.data1, sdl.window.data2};
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        event.Type = EventType::WindowFocusGained;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        event.Type = EventType::WindowFocusLost;
        break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        event.Type = EventType::WindowMouseEntered;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        event.Type = EventType::WindowMouseLeft;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        event.Type = EventType::WindowFullscreenChanged;
        event.Fullscreen = {true};
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        event.Type = EventType::WindowFullscreenChanged;
        event.Fullscreen = {false};
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (sdl.key.scancode < 0 || static_cast<u32>(sdl.key.scancode) >= kKeyCount)
            return std::nullopt;
        event.Type = sdl.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
        event.Key = {static_cast<Key>(sdl.key.scancode), static_cast<KeyMod>(sdl.key.mod),
                     sdl.key.down, sdl.key.repeat};
        break;
    case SDL_EVENT_TEXT_INPUT:
        if (sdl.text.text == nullptr)
            return std::nullopt;
        event.Type = EventType::TextInput;
        event.Text = {sdl.text.text};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        event.Type = sdl.button.down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        event.MouseButton = {static_cast<MouseButton>(sdl.button.button),
                             {sdl.button.x, sdl.button.y}, sdl.button.clicks, sdl.button.down};
        break;
    case SDL_EVENT_MOUSE_MOTION:
        event.Type = EventType::MouseMoved;
        event.MouseMotion = {{sdl.motion.x, sdl.motion.y}, {sdl.motion.xrel, sdl.motion.yrel}};
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        const f32 flip = sdl.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        event.Type = EventType::MouseWheel;
        event.Wheel = {Vec2(sdl.wheel.x, sdl.wheel.y) * flip};
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        event.Type = EventType::GamepadAdded;
        event.Gamepad = {static_cast<GamepadId>(sdl.gdevice.which)};
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        event.Type = EventType::GamepadRemoved;
        event.Gamepad = {static_cast<GamepadId>(sdl.gdevice.which)};
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        event.Type = sdl.gbutton.down ? EventType::GamepadButtonDown : EventType::GamepadButtonUp;
        event.GamepadButton = {static_cast<GamepadId>(sdl.gbutton.which),
                               static_cast<GamepadButton>(sdl.gbutton.button), sdl.gbutton.down};
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        event.Type = EventType::GamepadAxisMoved;
        event.GamepadAxis = {static_cast<GamepadId>(sdl.gaxis.which),
                             static_cast<GamepadAxis>(sdl.gaxis.axis),
                             static_cast<f32>(sdl.gaxis.value) / 32767.0f};
        break;
    default:
        return std::nullopt;
    }
    return event;
}

} // namespace Emerald

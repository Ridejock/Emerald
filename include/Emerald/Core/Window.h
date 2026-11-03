#pragma once

#include <string>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

struct NativeWindow;

struct WindowSpec {
    std::string Title = "Emerald";
    u32 Width = 1280;
    u32 Height = 720;
    bool Resizable = true;
    bool VSync = true; // applied by the Renderer through the swapchain present mode
    // Start in borderless fullscreen (see Window::SetFullscreen). On Linux this is a desktop
    // fullscreen with no display-mode switch, which is what gamescope on a Steam Deck expects.
    bool Fullscreen = false;
};

struct Image;

// Owns the OS window. Rendering into it is done by Emerald::Renderer.
class Window {
public:
    explicit Window(const WindowSpec& spec);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool IsValid() const { return m_Window != nullptr; }
    // The platform window. Games do not need it; the engine uses it to create the renderer.
    [[nodiscard]] NativeWindow* GetNativeWindow() const { return m_Window; }
    // The OS cursor. Hidden cursors stay hidden until this is called with true.
    bool SetCursorVisible(bool visible);
    [[nodiscard]] bool IsCursorVisible() const;
    [[nodiscard]] const WindowSpec& GetSpec() const { return m_Spec; }

    // Current client-area size in window coordinates (the units of mouse positions). With display
    // scaling (e.g. 150% on Windows) this can be smaller than the size in pixels.
    [[nodiscard]] Vec2i GetSize() const;
    // Current client-area size in pixels: the size of the swapchain texture you render into.
    [[nodiscard]] Vec2i GetSizeInPixels() const;

    // Borderless fullscreen on the window's display (the desktop resolution: no mode switch, so
    // alt-tabbing is instant). False = back to the normal window. Returns false on failure.
    bool SetFullscreen(bool fullscreen);
    [[nodiscard]] bool IsFullscreen() const;
    // The icon in the title bar / taskbar (RGBA image, e.g. 64 x 64). On Windows the .exe's own
    // icon comes from its resource file instead; this one is used while the game runs.
    bool SetIcon(const Image& image);

    // OS text input (IME and composed characters) for this window. Key events keep arriving
    // either way. The committed strings are collected by Application.
    void SetTextInputEnabled(bool enabled);
    [[nodiscard]] bool IsTextInputEnabled() const { return m_TextInput; }

private:
    WindowSpec m_Spec;
    NativeWindow* m_Window = nullptr;
    bool m_TextInput = false;
};

} // namespace Emerald

#pragma once

#include <string>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"

struct SDL_Window;

namespace Emerald {

struct WindowSpec {
    std::string Title = "Emerald";
    u32 Width = 1280;
    u32 Height = 720;
    bool Resizable = true;
    bool VSync = true;       // applied by the Renderer through the swapchain present mode
    bool Fullscreen = false; // start in borderless fullscreen (see Window::SetFullscreen)
};

struct Image;

// Owns an SDL3 window. Rendering into it is done by Emerald::Renderer (SDL GPU).
class Window {
public:
    explicit Window(const WindowSpec& spec);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool IsValid() const { return m_Window != nullptr; }
    [[nodiscard]] SDL_Window* GetNativeWindow() const { return m_Window; }
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

private:
    WindowSpec m_Spec;
    SDL_Window* m_Window = nullptr;
};

} // namespace Emerald

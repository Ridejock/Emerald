#pragma once

#include <string>

#include "Emerald/Core/Defines.h"

struct SDL_Window;

namespace Emerald {

struct WindowSpec {
    std::string Title = "Emerald";
    u32 Width = 1280;
    u32 Height = 720;
    bool Resizable = true;
    bool VSync = true; // applied by the Renderer through the swapchain present mode
};

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

private:
    WindowSpec m_Spec;
    SDL_Window* m_Window = nullptr;
};

} // namespace Emerald

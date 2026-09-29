#pragma once

#include <cstdint>
#include <string>

#include "Emerald/Core/Defines.h"

struct SDL_Window;
struct SDL_Renderer;

namespace Emerald {

struct WindowSpec {
    std::string Title = "Emerald";
    u32 Width = 1280;
    u32 Height = 720;
    bool Resizable = true;
    bool VSync = true;
};

// Owns an SDL3 window and its 2D renderer.
class Window {
public:
    explicit Window(const WindowSpec& spec);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool IsValid() const { return m_Window && m_Renderer; }
    [[nodiscard]] SDL_Window* GetNativeWindow() const { return m_Window; }
    [[nodiscard]] SDL_Renderer* GetRenderer() const { return m_Renderer; }
    [[nodiscard]] const WindowSpec& GetSpec() const { return m_Spec; }

private:
    WindowSpec m_Spec;
    SDL_Window* m_Window = nullptr;
    SDL_Renderer* m_Renderer = nullptr;
};

} // namespace Emerald

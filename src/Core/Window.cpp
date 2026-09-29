#include "Emerald/Core/Window.h"

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

Window::Window(const WindowSpec& spec) : m_Spec(spec)
{
    SDL_WindowFlags flags = 0;
    if (spec.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    m_Window = SDL_CreateWindow(spec.Title.c_str(), static_cast<int>(spec.Width),
                                static_cast<int>(spec.Height), flags);
    if (!m_Window) {
        EM_CORE_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    m_Renderer = SDL_CreateRenderer(m_Window, nullptr);
    if (!m_Renderer) {
        EM_CORE_ERROR("SDL_CreateRenderer failed: {}", SDL_GetError());
        return;
    }

    if (spec.VSync && !SDL_SetRenderVSync(m_Renderer, 1))
        EM_CORE_WARN("VSync not available: {}", SDL_GetError());

    EM_CORE_INFO("Window '{}' created ({}x{}, renderer: {})", spec.Title, spec.Width, spec.Height,
                 SDL_GetRendererName(m_Renderer));
}

Window::~Window()
{
    if (m_Renderer)
        SDL_DestroyRenderer(m_Renderer);
    if (m_Window)
        SDL_DestroyWindow(m_Window);
}

} // namespace Emerald

#include "Emerald/Core/Window.h"

#include <SDL3/SDL.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

Window::Window(const WindowSpec& spec) : m_Spec(spec)
{
    SDL_WindowFlags flags = 0;
    if (spec.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    m_Window = SDL_CreateWindow(spec.Title.c_str(), static_cast<i32>(spec.Width),
                                static_cast<i32>(spec.Height), flags);
    if (!m_Window) {
        EM_CORE_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    EM_CORE_INFO("Window '{}' created ({}x{})", spec.Title, spec.Width, spec.Height);
}

Window::~Window()
{
    if (m_Window)
        SDL_DestroyWindow(m_Window);
}

} // namespace Emerald

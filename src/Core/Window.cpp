#include "Emerald/Core/Window.h"

#include <SDL3/SDL.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

Window::Window(const WindowSpec& spec) : m_Spec(spec)
{
    SDL_WindowFlags flags = 0;
    if (spec.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (spec.Fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN; // borderless: no fullscreen mode is set

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

Vec2i Window::GetSize() const
{
    Vec2i size;
    SDL_GetWindowSize(m_Window, &size.x, &size.y);
    return size;
}

Vec2i Window::GetSizeInPixels() const
{
    Vec2i size;
    SDL_GetWindowSizeInPixels(m_Window, &size.x, &size.y);
    return size;
}

bool Window::SetFullscreen(bool fullscreen)
{
    // A null mode means "fullscreen desktop" (borderless) in SDL3.
    if (!SDL_SetWindowFullscreenMode(m_Window, nullptr) ||
        !SDL_SetWindowFullscreen(m_Window, fullscreen)) {
        EM_CORE_WARN("Could not {} fullscreen: {}", fullscreen ? "enter" : "leave", SDL_GetError());
        return false;
    }
    EM_CORE_INFO("Window {}", fullscreen ? "fullscreen (borderless)" : "windowed");
    return true;
}

bool Window::IsFullscreen() const
{
    return (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool Window::SetIcon(const Image& image)
{
    if (image.Width <= 0 || image.Height <= 0 ||
        image.Pixels.size() !=
            static_cast<usize>(image.Width) * static_cast<usize>(image.Height) * 4)
        return false;
    // The surface only borrows the pixels; SDL copies them for the icon.
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(image.Width, image.Height, SDL_PIXELFORMAT_RGBA32,
                              const_cast<u8*>(image.Pixels.data()), image.Width * 4);
    if (!surface) {
        EM_CORE_WARN("Window icon: {}", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SetWindowIcon(m_Window, surface);
    SDL_DestroySurface(surface);
    if (!ok)
        EM_CORE_WARN("SDL_SetWindowIcon failed: {}", SDL_GetError());
    return ok;
}

} // namespace Emerald

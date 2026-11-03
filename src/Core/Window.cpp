#include "Emerald/Core/Window.h"

#include <SDL3/SDL.h>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"
#include "Platform/SdlCast.h"

namespace Emerald {

Window::Window(const WindowSpec& spec) : m_Spec(spec)
{
    SDL_WindowFlags flags = 0;
    if (spec.Resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (spec.Fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN; // borderless: no fullscreen mode is set

    m_Window = FromSdl(SDL_CreateWindow(spec.Title.c_str(), static_cast<i32>(spec.Width),
                                       static_cast<i32>(spec.Height), flags));
    if (!m_Window) {
        EM_CORE_ERROR("SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    EM_CORE_INFO("Window '{}' created ({}x{})", spec.Title, spec.Width, spec.Height);
}

Window::~Window()
{
    if (m_Window)
        SDL_DestroyWindow(Sdl(m_Window));
}

Vec2i Window::GetSize() const
{
    Vec2i size;
    SDL_GetWindowSize(Sdl(m_Window), &size.x, &size.y);
    return size;
}

Vec2i Window::GetSizeInPixels() const
{
    Vec2i size;
    SDL_GetWindowSizeInPixels(Sdl(m_Window), &size.x, &size.y);
    return size;
}

bool Window::SetFullscreen(bool fullscreen)
{
    // A null mode is borderless desktop fullscreen: the display mode stays put. On Wayland, and
    // on gamescope (Steam Deck Gaming Mode), a mode switch is the wrong tool.
    if (!SDL_SetWindowFullscreenMode(Sdl(m_Window), nullptr) ||
        !SDL_SetWindowFullscreen(Sdl(m_Window), fullscreen)) {
        EM_CORE_WARN("Could not {} fullscreen: {}", fullscreen ? "enter" : "leave", SDL_GetError());
        return false;
    }
    EM_CORE_INFO("Window {} ({})", fullscreen ? "fullscreen (borderless)" : "windowed",
                 SDL_GetCurrentVideoDriver());
    return true;
}

bool Window::IsFullscreen() const
{
    return (SDL_GetWindowFlags(Sdl(m_Window)) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::SetTextInputEnabled(bool enabled)
{
    if (m_Window == nullptr || enabled == m_TextInput)
        return;
    if (enabled)
        SDL_StartTextInput(Sdl(m_Window));
    else
        SDL_StopTextInput(Sdl(m_Window));
    m_TextInput = enabled;
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
    const bool ok = SDL_SetWindowIcon(Sdl(m_Window), surface);
    SDL_DestroySurface(surface);
    if (!ok)
        EM_CORE_WARN("SDL_SetWindowIcon failed: {}", SDL_GetError());
    return ok;
}

bool Window::SetCursorVisible(bool visible)
{
    return visible ? SDL_ShowCursor() : SDL_HideCursor();
}

bool Window::IsCursorVisible() const
{
    return SDL_CursorVisible();
}

} // namespace Emerald

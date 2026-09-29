#include "Emerald/Input/Keyboard.h"

namespace Emerald {

void Keyboard::OnKeyDown(SDL_Scancode scancode)
{
    const usize i = static_cast<usize>(scancode);
    if (i >= SDL_SCANCODE_COUNT || m_Down[i])
        return;
    m_Down[i] = true;
    m_FrameEdges.Pressed[i] = true;
    m_StepEdges.Pressed[i] = true;
}

void Keyboard::OnKeyUp(SDL_Scancode scancode)
{
    const usize i = static_cast<usize>(scancode);
    if (i >= SDL_SCANCODE_COUNT || !m_Down[i])
        return;
    m_Down[i] = false;
    m_FrameEdges.Released[i] = true;
    m_StepEdges.Released[i] = true;
}

void Keyboard::ReleaseAll()
{
    m_FrameEdges.Released |= m_Down;
    m_StepEdges.Released |= m_Down;
    m_Down.reset();
}

void Keyboard::BeginFrame()
{
    m_FrameEdges = {};
}

void Keyboard::EndFixedStep()
{
    m_StepEdges = {};
    m_InFixedStep = false;
}

} // namespace Emerald

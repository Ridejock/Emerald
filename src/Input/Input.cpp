#include "Emerald/Input/Input.h"

namespace Emerald {

void Input::OnKeyDown(SDL_Scancode scancode)
{
    const usize i = static_cast<usize>(scancode);
    if (i >= SDL_SCANCODE_COUNT || m_Down[i])
        return;
    m_Down[i] = true;
    m_FrameEdges.Pressed[i] = true;
    m_StepEdges.Pressed[i] = true;
}

void Input::OnKeyUp(SDL_Scancode scancode)
{
    const usize i = static_cast<usize>(scancode);
    if (i >= SDL_SCANCODE_COUNT || !m_Down[i])
        return;
    m_Down[i] = false;
    m_FrameEdges.Released[i] = true;
    m_StepEdges.Released[i] = true;
}

void Input::ReleaseAll()
{
    m_FrameEdges.Released |= m_Down;
    m_StepEdges.Released |= m_Down;
    m_Down.reset();
}

void Input::BeginFrame()
{
    m_FrameEdges = {};
}

void Input::EndFixedStep()
{
    m_StepEdges = {};
    m_InFixedStep = false;
}

} // namespace Emerald

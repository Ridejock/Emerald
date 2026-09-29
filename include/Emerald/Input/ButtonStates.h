#pragma once

#include <bitset>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Down state plus pressed/released edges for `Count` buttons (keys, gamepad buttons). Shared by
// Keyboard and Gamepads so both follow the same rules:
//
// "Was pressed/released" means "since the last frame" in OnUpdate/OnRender, and "since the last
// fixed step" in OnFixedUpdate. The difference matters when the render rate is higher than the
// fixed rate (144 Hz vs 120 Hz): some frames run no fixed step at all, and a tap in such a frame
// must still reach the next fixed step - exactly once.
template <usize Count> class ButtonStates {
public:
    [[nodiscard]] bool IsDown(usize i) const { return i < Count && m_Down[i]; }
    [[nodiscard]] bool WasPressed(usize i) const { return i < Count && Edges().Pressed[i]; }
    [[nodiscard]] bool WasReleased(usize i) const { return i < Count && Edges().Released[i]; }

    // Records an edge when the state changes; setting the same state again does nothing.
    void Set(usize i, bool down)
    {
        if (i >= Count || m_Down[i] == down)
            return;
        m_Down[i] = down;
        if (down) {
            m_FrameEdges.Pressed[i] = true;
            m_StepEdges.Pressed[i] = true;
        } else {
            m_FrameEdges.Released[i] = true;
            m_StepEdges.Released[i] = true;
        }
    }

    // Releases everything that is held (with release edges).
    void ReleaseAll()
    {
        m_FrameEdges.Released |= m_Down;
        m_StepEdges.Released |= m_Down;
        m_Down.reset();
    }

    // Starts a new frame: forgets the per-frame edges (pending fixed-step edges are kept).
    void BeginFrame() { m_FrameEdges = {}; }
    // Brackets one OnFixedUpdate: queries return the fixed-step edges in between, and the edges
    // are consumed by EndFixedStep.
    void BeginFixedStep() { m_InFixedStep = true; }
    void EndFixedStep()
    {
        m_StepEdges = {};
        m_InFixedStep = false;
    }

private:
    using Bits = std::bitset<Count>;
    struct EdgeSet {
        Bits Pressed;
        Bits Released;
    };

    [[nodiscard]] const EdgeSet& Edges() const
    {
        return m_InFixedStep ? m_StepEdges : m_FrameEdges;
    }

    Bits m_Down;
    EdgeSet m_FrameEdges; // since BeginFrame
    EdgeSet m_StepEdges;  // since the last EndFixedStep
    bool m_InFixedStep = false;
};

} // namespace Emerald

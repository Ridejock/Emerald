#pragma once

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Turns variable frame times into a whole number of fixed-size simulation steps.
//
//   const u32 steps = timestep.Advance(frameNanoseconds);
//   for (u32 i = 0; i < steps; ++i)
//       Simulate(timestep.GetStepSeconds());
//
// Time accumulates across frames, so on average exactly `rate` steps run per second no matter
// the frame rate (at 144 fps and 120 Hz most frames run one step and some run none). Leftover
// time carries over; GetAlpha() says how far the current time is into the next step (0..1), for
// optional interpolation when drawing.
//
// If the simulation cannot keep up (a slow frame, a debugger breakpoint, dragging the window) the
// steps per frame are capped at maxSteps and the excess time is dropped: the game slows down
// briefly instead of running ever more steps per frame to catch up ("spiral of death").
// Time is kept in integer nanoseconds so the accumulator never drifts.
class FixedTimestep {
public:
    // rateHz must be > 0 and maxSteps >= 1; invalid values fall back to 120 Hz / 1 step.
    explicit FixedTimestep(f64 rateHz = 120.0, u32 maxSteps = 8)
        : m_StepNs(static_cast<u64>(1e9 / (rateHz > 0.0 ? rateHz : 120.0) + 0.5)),
          m_MaxSteps(maxSteps > 0 ? maxSteps : 1)
    {
    }

    // Adds a frame's elapsed time; returns how many fixed steps to run for it.
    [[nodiscard]] u32 Advance(u64 elapsedNs)
    {
        m_AccumulatorNs += elapsedNs;
        u32 steps = 0;
        while (m_AccumulatorNs >= m_StepNs && steps < m_MaxSteps) {
            m_AccumulatorNs -= m_StepNs;
            ++steps;
        }
        // Hit the cap: forget the time we could not simulate (keep less than one step).
        if (m_AccumulatorNs >= m_StepNs)
            m_AccumulatorNs %= m_StepNs;
        return steps;
    }

    [[nodiscard]] u64 GetStepNanoseconds() const { return m_StepNs; }
    [[nodiscard]] f32 GetStepSeconds() const { return static_cast<f32>(m_StepNs) / 1e9f; }
    // Fraction of a step that is accumulated but not simulated yet, in [0, 1).
    [[nodiscard]] f32 GetAlpha() const
    {
        return static_cast<f32>(static_cast<f64>(m_AccumulatorNs) / static_cast<f64>(m_StepNs));
    }

private:
    u64 m_StepNs;
    u32 m_MaxSteps;
    u64 m_AccumulatorNs = 0;
};

} // namespace Emerald

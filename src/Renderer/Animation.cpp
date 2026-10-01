#include "Emerald/Renderer/Animation.h"

#include <algorithm>

namespace Emerald {

namespace {
// Lower limit for frame durations, so a zero or negative one cannot stall Update.
constexpr f32 kMinFrameDuration = 1e-4f;

f32 DurationOf(const AnimationFrame& frame)
{
    return std::max(frame.Duration, kMinFrameDuration);
}
} // namespace

f32 Animation::GetCycleDuration() const
{
    f32 total = 0.0f;
    for (usize i = 0; i < Frames.size(); ++i) {
        const f32 d = DurationOf(Frames[i]);
        // Ping-pong plays the inner frames twice per cycle.
        const bool inner = i > 0 && i + 1 < Frames.size();
        total += Mode == AnimationMode::PingPong && inner ? 2.0f * d : d;
    }
    return total;
}

void Animator::Play(const Animation& animation, bool restart)
{
    if (m_Animation == &animation && !restart) {
        if (!m_Finished)
            m_Playing = true;
        return;
    }
    m_Animation = &animation;
    m_Frame = 0;
    m_FrameTime = 0.0f;
    m_Loops = 0;
    m_Backwards = false;
    m_Finished = false;
    m_Playing = !animation.Frames.empty();
}

void Animator::Update(f32 dt)
{
    if (!m_Playing || !m_Animation || m_Animation->Frames.empty())
        return;
    m_FrameTime += dt * m_Speed;
    // Several frames (even cycles) may pass in one long step.
    while (m_Playing && m_FrameTime >= DurationOf(m_Animation->Frames[m_Frame])) {
        m_FrameTime -= DurationOf(m_Animation->Frames[m_Frame]);
        Advance();
    }
}

void Animator::Advance()
{
    const Animation& a = *m_Animation;
    const u32 last = static_cast<u32>(a.Frames.size()) - 1;
    bool cycleDone = false;
    switch (a.Mode) {
    case AnimationMode::Once:
        if (m_Frame < last) {
            ++m_Frame;
            return;
        }
        // Stay on the last frame.
        m_Playing = false;
        m_Finished = true;
        m_FrameTime = 0.0f;
        if (OnFinished)
            OnFinished(a);
        return;
    case AnimationMode::Loop:
        cycleDone = m_Frame == last;
        m_Frame = cycleDone ? 0 : m_Frame + 1;
        break;
    case AnimationMode::PingPong:
        if (last == 0) {
            cycleDone = true;
        } else if (!m_Backwards) {
            ++m_Frame;
            m_Backwards = m_Frame == last;
        } else {
            --m_Frame;
            cycleDone = m_Frame == 0;
            m_Backwards = !cycleDone;
        }
        break;
    }
    if (cycleDone) {
        ++m_Loops;
        if (OnLoop)
            OnLoop(a);
    }
}

Sprite Animator::GetSprite() const
{
    if (!m_Animation || m_Animation->Frames.empty())
        return {};
    return m_Animation->Frames[m_Frame].Image;
}

} // namespace Emerald

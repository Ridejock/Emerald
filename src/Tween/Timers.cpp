#include "Emerald/Tween/Timers.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Emerald {

TimerId Timers::After(f32 seconds, std::function<void()> callback)
{
    return Add(std::max(seconds, 0.0f), false, std::move(callback));
}

TimerId Timers::Every(f32 interval, std::function<void()> callback)
{
    return Add(std::max(interval, kMinInterval), true, std::move(callback));
}

TimerId Timers::Add(f32 seconds, bool repeat, std::function<void()> callback)
{
    assert(callback);
    auto timer = std::make_unique<Timer>();
    timer->Id = TimerId{m_NextId++};
    timer->Interval = seconds;
    timer->TimeLeft = seconds;
    timer->Repeat = repeat;
    timer->Callback = std::move(callback);
    const TimerId id = timer->Id;
    m_Timers.push_back(std::move(timer));
    return id;
}

bool Timers::Cancel(TimerId id)
{
    if (!IsActive(id))
        return false;
    for (const std::unique_ptr<Timer>& timer : m_Timers)
        if (timer->Id == id)
            timer->Done = true;
    if (!m_Updating)
        RemoveDone(); // during Update, Update removes it at the end
    return true;
}

void Timers::Clear()
{
    for (const std::unique_ptr<Timer>& timer : m_Timers)
        timer->Done = true;
    if (!m_Updating)
        RemoveDone();
}

bool Timers::IsActive(TimerId id) const
{
    const Timer* timer = Find(id);
    return timer && !timer->Done;
}

f32 Timers::GetTimeLeft(TimerId id) const
{
    const Timer* timer = Find(id);
    return timer && !timer->Done ? timer->TimeLeft : 0.0f;
}

usize Timers::GetCount() const
{
    return static_cast<usize>(std::count_if(m_Timers.begin(), m_Timers.end(),
                                            [](const auto& timer) { return !timer->Done; }));
}

void Timers::Update(f32 dt)
{
    m_Updating = true;
    // Only the timers that exist now; ones added by callbacks start on the next Update.
    const usize count = m_Timers.size();
    for (usize i = 0; i < count; ++i) {
        Timer& timer = *m_Timers[i];
        if (timer.Done)
            continue;
        timer.TimeLeft -= dt;
        // Fire once per deadline passed; the overshoot stays in TimeLeft for the next one.
        while (!timer.Done && timer.TimeLeft <= 0.0f) {
            if (timer.Repeat)
                timer.TimeLeft += timer.Interval;
            else
                timer.Done = true; // before the callback, so it fires exactly once
            timer.Callback();      // may cancel this timer (Done) and stop the loop
        }
    }
    m_Updating = false;
    RemoveDone();
}

const Timers::Timer* Timers::Find(TimerId id) const
{
    for (const std::unique_ptr<Timer>& timer : m_Timers)
        if (timer->Id == id)
            return timer.get();
    return nullptr;
}

void Timers::RemoveDone()
{
    std::erase_if(m_Timers, [](const std::unique_ptr<Timer>& timer) { return timer->Done; });
}

} // namespace Emerald

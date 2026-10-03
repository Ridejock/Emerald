#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Names one timer; never reused, so a stale id is harmless (like TweenId). 0 names nothing.
struct TimerId {
    u32 Value = 0;

    explicit operator bool() const { return Value != 0; }
    bool operator==(const TimerId&) const = default;
};

// One-shot and repeating timers, advanced with the game's update dt:
//
//   m_Timers.After(2.0f, [this] { ShowHint(); });            // once, in 2 seconds
//   m_Blink = m_Timers.Every(0.5f, [this] { m_On = !m_On; }); // every half second
//   m_Timers.Cancel(m_Blink);
//   m_Timers.Update(dt); // every frame
//
// Frame-rate independent: time past a deadline carries over, so Every(0.1) fires ten times in a
// second whether that second came as 60 frames or 7 uneven ones (a long frame fires it several
// times). Callbacks may add or cancel timers, including their own; new ones start on the next
// Update. Whatever a callback captures must outlive the timer (cancel it otherwise).
class Timers {
public:
    // The shortest Every interval; shorter ones are raised to it (so a frame can't loop forever).
    static constexpr f32 kMinInterval = 0.001f;

    TimerId After(f32 seconds, std::function<void()> callback);
    TimerId Every(f32 interval, std::function<void()> callback); // until cancelled

    // Returns false if it was not running (already fired, cancelled, or never existed).
    bool Cancel(TimerId id);
    void Clear();

    [[nodiscard]] bool IsActive(TimerId id) const;
    // Seconds until it next fires (0 if it is not active).
    [[nodiscard]] f32 GetTimeLeft(TimerId id) const;
    [[nodiscard]] usize GetCount() const;

    void Update(f32 dt);

private:
    struct Timer {
        TimerId Id;
        f32 Interval = 0.0f;
        f32 TimeLeft = 0.0f; // until it fires next
        bool Repeat = false;
        bool Done = false; // fired (one-shot) or cancelled; removed at the end of Update
        std::function<void()> Callback;
    };

    TimerId Add(f32 seconds, bool repeat, std::function<void()> callback);
    [[nodiscard]] const Timer* Find(TimerId id) const;
    void RemoveDone();

    // Boxed, so a timer stays put while its callback adds new ones (the vector may reallocate).
    std::vector<std::unique_ptr<Timer>> m_Timers;
    u32 m_NextId = 1;
    bool m_Updating = false;
};

} // namespace Emerald

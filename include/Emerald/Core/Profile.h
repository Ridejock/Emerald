#pragma once

// CPU scopes for the in-game profiler (Profiler.h) and, in a profile build, for Tracy.
//
//   void UpdateEnemies(World& world)
//   {
//       EM_PROFILE_SCOPE("UpdateEnemies"); // from here to the end of the block
//       ...
//   }
//
// Cost:
//   * EMERALD_PROFILER=0 and EMERALD_PROFILE=0: the macros compile to nothing.
//   * Recording off (the default until F3 / --profile): EM_PROFILE_SCOPE is one relaxed atomic
//     load and a not-taken branch. No timer, no allocation, no lock.
//   * Recording on: a timestamp and a push on this thread's scope stack, aggregated at frame end.
//   * EMERALD_PROFILE=1 (the `profile` preset): the same scope is also a Tracy zone. Tracy is
//     on-demand, so it stays quiet until a viewer connects. The name has to be a string literal
//     for Tracy; the in-game profiler only needs static storage (a literal or __func__).
//
// The macro is a declaration. Put it inside a block, not as the only statement of an unbraced if.
// The engine marks every frame and zones the main loop (wait, events, assets, fixed steps, update,
// audio, render), the entity systems and the platformer step.

#include "Emerald/Core/Defines.h"

#ifndef EMERALD_PROFILER
#define EMERALD_PROFILER 0
#endif

#ifndef EMERALD_PROFILE
#define EMERALD_PROFILE 0
#endif

#if EMERALD_PROFILE
#include <tracy/Tracy.hpp>
#endif

#if EMERALD_PROFILER
#include "Emerald/Core/Profiler.h"
#endif

#define EM_PROFILE_CONCAT_INNER(a, b) a##b
#define EM_PROFILE_CONCAT(a, b) EM_PROFILE_CONCAT_INNER(a, b)

#if EMERALD_PROFILER

namespace Emerald::detail {

// RAII guard. Inactive (one byte, nothing to undo) when recording is off or no frame is open.
class ProfileScope {
public:
    explicit ProfileScope(const char* name) noexcept
    {
        if (!ProfilerRecording())
            return;
        m_Active = Profiler::Get().BeginScope(name);
    }
    ~ProfileScope()
    {
        if (m_Active)
            Profiler::Get().EndScope();
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    bool m_Active = false;
};

} // namespace Emerald::detail

#endif

#if EMERALD_PROFILER && EMERALD_PROFILE
#define EM_PROFILE_SCOPE(name)                                                                     \
    ::Emerald::detail::ProfileScope EM_PROFILE_CONCAT(emProfileScope, __LINE__)(name);             \
    ZoneScopedN(name)
#define EM_PROFILE_FUNCTION()                                                                      \
    ::Emerald::detail::ProfileScope EM_PROFILE_CONCAT(emProfileFunction, __LINE__)(__func__);      \
    ZoneScoped
#define EM_PROFILE_FRAME() FrameMark
#define EM_PROFILE_THREAD(name) tracy::SetThreadName(name)
#elif EMERALD_PROFILER
#define EM_PROFILE_SCOPE(name)                                                                     \
    ::Emerald::detail::ProfileScope EM_PROFILE_CONCAT(emProfileScope, __LINE__)(name)
#define EM_PROFILE_FUNCTION()                                                                      \
    ::Emerald::detail::ProfileScope EM_PROFILE_CONCAT(emProfileFunction, __LINE__)(__func__)
#define EM_PROFILE_FRAME() ((void)0)
#define EM_PROFILE_THREAD(name) ((void)0)
#elif EMERALD_PROFILE
#define EM_PROFILE_SCOPE(name) ZoneScopedN(name)
#define EM_PROFILE_FUNCTION() ZoneScoped
#define EM_PROFILE_FRAME() FrameMark
#define EM_PROFILE_THREAD(name) tracy::SetThreadName(name)
#else
#define EM_PROFILE_SCOPE(name) ((void)0)
#define EM_PROFILE_FUNCTION() ((void)0)
#define EM_PROFILE_FRAME() ((void)0)
#define EM_PROFILE_THREAD(name) ((void)0)
#endif

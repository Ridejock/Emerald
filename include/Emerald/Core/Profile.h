#pragma once

// Profiling with Tracy (https://github.com/wolfpld/tracy). Build with the `profile` preset (or
// -DEMERALD_PROFILE=ON), run the game and connect the Tracy viewer of the same version. With
// profiling off the macros compile to nothing, so they can stay in shipping code:
//
//   void UpdateEnemies(World& world)
//   {
//       EM_PROFILE_SCOPE("UpdateEnemies"); // a zone from here to the end of the block
//       ...
//   }
//
// The engine marks every frame (EM_PROFILE_FRAME) and zones its main loop parts (events, assets,
// fixed steps, update, render), the entity systems and the platformer step.

#if EMERALD_PROFILE
#include <tracy/Tracy.hpp>

#define EM_PROFILE_FRAME() FrameMark             // the end of a frame
#define EM_PROFILE_SCOPE(name) ZoneScopedN(name) // a named zone until the end of the scope
#define EM_PROFILE_FUNCTION() ZoneScoped         // a zone named after the function
#define EM_PROFILE_THREAD(name) tracy::SetThreadName(name) // names the calling thread
#else
#define EM_PROFILE_FRAME() ((void)0)
#define EM_PROFILE_SCOPE(name) ((void)0)
#define EM_PROFILE_FUNCTION() ((void)0)
#define EM_PROFILE_THREAD(name) ((void)0)
#endif

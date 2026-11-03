#pragma once

#include <SDL3/SDL_audio.h>

#include "Emerald/Audio/Sound.h"

namespace Emerald {

// The backend spec for kMixSampleRate / kMixChannels. Copied by the audio calls that take it.
[[nodiscard]] inline SDL_AudioSpec MixSpec()
{
    return {SDL_AUDIO_F32, kMixChannels, kMixSampleRate};
}

} // namespace Emerald

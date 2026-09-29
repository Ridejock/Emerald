#include "Emerald/Audio/Audio.h"

#include <SDL3/SDL_error.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

bool Audio::Init()
{
    // Ask for the mix format; SDL converts to whatever the device really uses.
    m_Device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kMixSpec);
    if (m_Device == 0) {
        EM_CORE_WARN("No audio output: {}", SDL_GetError());
        return false;
    }
    for (SDL_AudioStream*& voice : m_Voices) {
        voice = SDL_CreateAudioStream(&kMixSpec, &kMixSpec);
        if (voice && !SDL_BindAudioStream(m_Device, voice)) {
            SDL_DestroyAudioStream(voice);
            voice = nullptr;
        }
    }
    SDL_AudioSpec deviceSpec{};
    SDL_GetAudioDeviceFormat(m_Device, &deviceSpec, nullptr);
    EM_CORE_INFO("Audio output: {} ({} Hz, {} channels)", SDL_GetAudioDeviceName(m_Device),
                 deviceSpec.freq, deviceSpec.channels);
    return true;
}

void Audio::Shutdown()
{
    for (SDL_AudioStream*& voice : m_Voices) {
        SDL_DestroyAudioStream(voice); // also unbinds it; null is fine
        voice = nullptr;
    }
    if (m_Device != 0) {
        SDL_CloseAudioDevice(m_Device);
        m_Device = 0;
    }
}

void Audio::Play(const Sound& sound, f32 volume)
{
    if (m_Device == 0 || sound.Samples.empty())
        return;
    // A free voice is one that has played everything it was given.
    SDL_AudioStream* voice = nullptr;
    for (SDL_AudioStream* v : m_Voices) {
        if (v && SDL_GetAudioStreamQueued(v) == 0) {
            voice = v;
            break;
        }
    }
    if (!voice) {
        voice = m_Voices[m_NextSteal];
        m_NextSteal = (m_NextSteal + 1) % kVoiceCount;
        if (!voice)
            return;
        SDL_ClearAudioStream(voice);
    }
    SDL_SetAudioStreamGain(voice, volume);
    SDL_PutAudioStreamData(voice, sound.Samples.data(),
                           static_cast<int>(sound.Samples.size() * sizeof(f32)));
}

void Audio::StopAll()
{
    for (SDL_AudioStream* voice : m_Voices) {
        if (voice)
            SDL_ClearAudioStream(voice);
    }
}

void Audio::SetMasterVolume(f32 volume)
{
    if (m_Device != 0)
        SDL_SetAudioDeviceGain(m_Device, volume);
}

} // namespace Emerald

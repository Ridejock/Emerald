#pragma once

#include <array>

#include <SDL3/SDL_audio.h>

#include "Emerald/Audio/Sound.h"
#include "Emerald/Core/Defines.h"

namespace Emerald {

// Minimal sound playback: fire-and-forget sounds on the default output device. Each playing
// sound uses one of a few voices (SDL audio streams bound to the device; SDL mixes them). Get it
// with Application::GetAudio():
//
//   std::optional<Sound> boom = LoadSound("assets/boom.mp3");   // once
//   if (boom) GetAudio().Play(*boom, 0.8f);                      // any time
//
// Without an audio device (or if SDL audio fails) everything still works, silently.
class Audio {
public:
    static constexpr usize kVoiceCount = 16;

    Audio() = default;
    ~Audio() { Shutdown(); }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Called by the Application after SDL_INIT_AUDIO. Returns false if there is no device.
    bool Init();
    void Shutdown();
    [[nodiscard]] bool IsAvailable() const { return m_Device != 0; }

    // Starts playing a sound (its samples are copied, so the Sound may go away afterwards).
    // When all voices are busy, the oldest started sound is cut off.
    void Play(const Sound& sound, f32 volume = 1.0f);
    void StopAll();
    // Master volume, 0..1 (more amplifies and may clip).
    void SetMasterVolume(f32 volume);

private:
    SDL_AudioDeviceID m_Device = 0;
    std::array<SDL_AudioStream*, kVoiceCount> m_Voices{};
    usize m_NextSteal = 0; // round-robin voice to cut off when all are busy
};

} // namespace Emerald

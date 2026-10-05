#pragma once

#include <filesystem>

#include <SDL3/SDL_audio.h>

#include "Emerald/Audio/Mixer.h"
#include "Emerald/Audio/Sound.h"
#include "Emerald/Core/Defines.h"

namespace Emerald {

// Sound playback on the default output device. Get it with Application::GetAudio():
//
//   std::optional<Sound> boom = LoadSound("assets/boom.mp3");      // or Synth.h
//   GetAudio().Play(*boom, {.Volume = 0.8f, .Pan = -0.3f});         // fire and forget
//   VoiceHandle engine = GetAudio().Play(hum, {.Loop = true});      // keep the handle...
//   GetAudio().SetVolume(engine, 0.5f);                             // ...to change it later
//   GetAudio().Stop(engine);                                        // fades out (10 ms)
//
// Threads: SDL calls our callback on its audio thread to mix the next block. The callback runs
// with the audio stream locked, and every method here locks the same stream first, so the game
// thread and the audio thread never touch the mixer at the same time. Call these from one game
// thread (the main thread). Locks are held only for a few microseconds (no loading or decoding
// while locked), so playback never waits for the game.
//
// Without an audio device everything still works, silently: Play returns an invalid handle.
class Audio {
public:
    Audio() = default;
    ~Audio() { Shutdown(); }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Called by the Application after SDL_INIT_AUDIO. Returns false if there is no device.
    bool Init();
    void Shutdown();
    [[nodiscard]] bool IsAvailable() const { return m_Stream != nullptr; }

    VoiceHandle Play(const Sound& sound, const PlayOptions& options = {});
    void Stop(VoiceHandle voice, f32 fadeMs = Mixer::kDefaultFadeMs);
    void StopAll(f32 fadeMs = Mixer::kDefaultFadeMs);
    void SetVolume(VoiceHandle voice, f32 volume);
    [[nodiscard]] bool IsPlaying(VoiceHandle voice) const;
    [[nodiscard]] usize GetPlayingCount() const;

    // Master volume 0..1 (applied before the soft limiter) and mute; both fade smoothly.
    void SetMasterVolume(f32 volume);
    [[nodiscard]] f32 GetMasterVolume() const { return m_Mixer.GetMasterVolume(); }
    void SetMuted(bool muted);
    [[nodiscard]] bool IsMuted() const { return m_Mixer.IsMuted(); }

    void SetGroupVolume(AudioGroup group, f32 volume);
    [[nodiscard]] f32 GetGroupVolume(AudioGroup group) const
    {
        return m_Mixer.GetGroupVolume(group);
    }
    void SetGroupMuted(AudioGroup group, bool muted);
    [[nodiscard]] bool IsGroupMuted(AudioGroup group) const { return m_Mixer.IsGroupMuted(group); }

    void SetListener(const Vec2& position);
    void SetVoicePosition(VoiceHandle voice, const Vec2& position);

    // Streaming music (.mp3 / .ogg); see Music.h. Crossfade swaps tracks over fadeMs.
    bool PlayMusic(const std::filesystem::path& path, f32 fadeMs = MusicPlayer::kDefaultFadeMs);
    bool CrossfadeMusic(const std::filesystem::path& path,
                        f32 fadeMs = MusicPlayer::kDefaultFadeMs);
    void StopMusic(f32 fadeMs = MusicPlayer::kDefaultFadeMs);
    void SetMusicVolume(f32 volume);
    [[nodiscard]] f32 GetMusicVolume() const;
    [[nodiscard]] bool IsMusicPlaying() const;

    // Called by the Application once per frame: frees the samples of finished sounds and pumps
    // music decode here on the game thread, never on the audio thread.
    void Update();

private:
    // Locks the audio stream for the lifetime of the object (no-op without a device).
    class Lock {
    public:
        explicit Lock(SDL_AudioStream* stream) : m_Stream(stream)
        {
            if (m_Stream)
                SDL_LockAudioStream(m_Stream);
        }
        ~Lock()
        {
            if (m_Stream)
                SDL_UnlockAudioStream(m_Stream);
        }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;

    private:
        SDL_AudioStream* m_Stream;
    };

    static void SDLCALL Callback(void* userdata, SDL_AudioStream* stream, int additionalBytes,
                                 int totalBytes);

    SDL_AudioStream* m_Stream = nullptr; // device stream: we push mixed audio into it
    Mixer m_Mixer;
};

} // namespace Emerald

#include "Emerald/Audio/Audio.h"

#include <array>

#include <SDL3/SDL_error.h>

#include "Audio/MixFormat.h"
#include "Emerald/Core/Log.h"
#include "Platform/SdlCast.h"

namespace Emerald {

struct AudioCallback {
    static void SDLCALL Run(void* userdata, SDL_AudioStream* stream, int additionalBytes,
                            int totalBytes);
};

void SDLCALL AudioCallback::Run(void* userdata, SDL_AudioStream* stream, int additionalBytes,
                                int /*totalBytes*/)
{
    // Runs on the audio thread with the stream locked. Mixes in blocks on the stack: no
    // allocations here.
    auto* audio = static_cast<Audio*>(userdata);
    constexpr usize kBlockFrames = 512;
    std::array<f32, kBlockFrames * 2> block;
    usize frames = static_cast<usize>(additionalBytes) / (sizeof(f32) * 2);
    while (frames > 0) {
        const usize n = frames < kBlockFrames ? frames : kBlockFrames;
        audio->m_Mixer.Mix(block.data(), n);
        SDL_PutAudioStreamData(stream, block.data(), static_cast<int>(n * sizeof(f32) * 2));
        frames -= n;
    }
}

Audio::Lock::Lock(AudioStream* stream) : m_Stream(stream)
{
    if (m_Stream)
        SDL_LockAudioStream(Sdl(m_Stream));
}

Audio::Lock::~Lock()
{
    if (m_Stream)
        SDL_UnlockAudioStream(Sdl(m_Stream));
}

bool Audio::Init()
{
    // One stream in the mix format on the default device; SDL converts it to whatever the
    // device really uses and calls Callback whenever it needs more data.
    const SDL_AudioSpec spec = MixSpec();
    m_Stream = FromSdl(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                                 &AudioCallback::Run, this));
    if (!m_Stream) {
        EM_CORE_WARN("No audio output: {}", SDL_GetError());
        return false;
    }
    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(Sdl(m_Stream));
    SDL_AudioSpec deviceSpec{};
    SDL_GetAudioDeviceFormat(device, &deviceSpec, nullptr);
    EM_CORE_INFO("Audio output: {} ({} Hz, {} channels)", SDL_GetAudioDeviceName(device),
                 deviceSpec.freq, deviceSpec.channels);
    SDL_ResumeAudioStreamDevice(Sdl(m_Stream)); // devices opened this way start paused
    return true;
}

void Audio::Shutdown()
{
    if (m_Stream) {
        SDL_DestroyAudioStream(Sdl(m_Stream)); // also closes the device and stops the callback
        m_Stream = nullptr;
    }
}

VoiceHandle Audio::Play(const Sound& sound, const PlayOptions& options)
{
    if (!m_Stream)
        return {};
    Lock lock(m_Stream);
    return m_Mixer.Play(sound, options);
}

void Audio::Stop(VoiceHandle voice, f32 fadeMs)
{
    Lock lock(m_Stream);
    m_Mixer.Stop(voice, fadeMs);
}

void Audio::StopAll(f32 fadeMs)
{
    Lock lock(m_Stream);
    m_Mixer.StopAll(fadeMs);
}

void Audio::SetVolume(VoiceHandle voice, f32 volume)
{
    Lock lock(m_Stream);
    m_Mixer.SetVolume(voice, volume);
}

bool Audio::IsPlaying(VoiceHandle voice) const
{
    Lock lock(m_Stream);
    return m_Mixer.IsPlaying(voice);
}

usize Audio::GetPlayingCount() const
{
    Lock lock(m_Stream);
    return m_Mixer.GetPlayingCount();
}

void Audio::SetMasterVolume(f32 volume)
{
    Lock lock(m_Stream);
    m_Mixer.SetMasterVolume(volume);
}

void Audio::SetMuted(bool muted)
{
    Lock lock(m_Stream);
    m_Mixer.SetMuted(muted);
}

void Audio::Update()
{
    Lock lock(m_Stream);
    m_Mixer.ReleaseFinished();
}

void Audio::SetGroupVolume(AudioGroup group, f32 volume)
{
    Lock lock(m_Stream);
    m_Mixer.SetGroupVolume(group, volume);
}

void Audio::SetGroupMuted(AudioGroup group, bool muted)
{
    Lock lock(m_Stream);
    m_Mixer.SetGroupMuted(group, muted);
}

void Audio::SetListener(const Vec2& position)
{
    Lock lock(m_Stream);
    m_Mixer.SetListener(position);
}

void Audio::SetVoicePosition(VoiceHandle voice, const Vec2& position)
{
    Lock lock(m_Stream);
    m_Mixer.SetVoicePosition(voice, position);
}

bool Audio::PlayMusic(const std::filesystem::path& path, f32 fadeMs)
{
    Lock lock(m_Stream);
    return m_Mixer.GetMusic().Play(path, fadeMs);
}

bool Audio::CrossfadeMusic(const std::filesystem::path& path, f32 fadeMs)
{
    Lock lock(m_Stream);
    return m_Mixer.GetMusic().Crossfade(path, fadeMs);
}

void Audio::StopMusic(f32 fadeMs)
{
    Lock lock(m_Stream);
    m_Mixer.GetMusic().Stop(fadeMs);
}

void Audio::SetMusicVolume(f32 volume)
{
    Lock lock(m_Stream);
    m_Mixer.GetMusic().SetVolume(volume);
}

f32 Audio::GetMusicVolume() const
{
    Lock lock(m_Stream);
    return m_Mixer.GetMusic().GetVolume();
}

bool Audio::IsMusicPlaying() const
{
    Lock lock(m_Stream);
    return m_Mixer.GetMusic().IsPlaying();
}

} // namespace Emerald

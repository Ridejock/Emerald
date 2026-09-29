#include "Emerald/Audio/Audio.h"

#include <array>

#include <SDL3/SDL_error.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

bool Audio::Init()
{
    // One stream in the mix format on the default device; SDL converts it to whatever the
    // device really uses and calls Callback whenever it needs more data.
    m_Stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kMixSpec, Callback, this);
    if (!m_Stream) {
        EM_CORE_WARN("No audio output: {}", SDL_GetError());
        return false;
    }
    const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(m_Stream);
    SDL_AudioSpec deviceSpec{};
    SDL_GetAudioDeviceFormat(device, &deviceSpec, nullptr);
    EM_CORE_INFO("Audio output: {} ({} Hz, {} channels)", SDL_GetAudioDeviceName(device),
                 deviceSpec.freq, deviceSpec.channels);
    SDL_ResumeAudioStreamDevice(m_Stream); // devices opened this way start paused
    return true;
}

void Audio::Shutdown()
{
    if (m_Stream) {
        SDL_DestroyAudioStream(m_Stream); // also closes the device and stops the callback
        m_Stream = nullptr;
    }
}

void SDLCALL Audio::Callback(void* userdata, SDL_AudioStream* stream, int additionalBytes,
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

} // namespace Emerald

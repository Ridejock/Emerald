#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// The mix format: every Sound is converted to it when loaded, so playing one is just a copy.
inline constexpr SDL_AudioSpec kMixSpec{SDL_AUDIO_F32, 2, 48000};

// A decoded sound in the mix format: interleaved stereo f32 samples (left, right, left, ...).
// The samples are shared and never change, so copying a Sound is cheap, and a sound that is
// still playing keeps its samples alive even if the game drops its Sound.
class Sound {
public:
    Sound() = default;
    explicit Sound(std::vector<f32> stereoSamples)
        : m_Samples(std::make_shared<const std::vector<f32>>(std::move(stereoSamples)))
    {
    }

    [[nodiscard]] std::span<const f32> GetSamples() const
    {
        return m_Samples ? std::span<const f32>(*m_Samples) : std::span<const f32>();
    }
    [[nodiscard]] bool IsEmpty() const { return GetSamples().empty(); }
    [[nodiscard]] usize GetFrameCount() const
    {
        return GetSamples().size() / static_cast<usize>(kMixSpec.channels);
    }
    [[nodiscard]] f32 GetDurationSeconds() const
    {
        return static_cast<f32>(GetFrameCount()) / static_cast<f32>(kMixSpec.freq);
    }
    // The shared samples, for the mixer.
    [[nodiscard]] const std::shared_ptr<const std::vector<f32>>& GetData() const
    {
        return m_Samples;
    }

private:
    std::shared_ptr<const std::vector<f32>> m_Samples;
};

// Loads a .wav (SDL_LoadWAV) or .mp3 (dr_mp3) file, picked by the extension (case-insensitive).
// Returns nothing (and logs why) if the file is missing, broken or of another type.
[[nodiscard]] std::optional<Sound> LoadSound(const std::filesystem::path& path);
[[nodiscard]] std::optional<Sound> LoadWav(const std::filesystem::path& path);
[[nodiscard]] std::optional<Sound> LoadMp3(const std::filesystem::path& path);
// Decodes an MP3 that is already in memory (e.g. embedded in the executable).
[[nodiscard]] std::optional<Sound> LoadMp3FromMemory(const void* data, usize size);
// Converts interleaved f32 samples in any rate/channel count to a Sound, e.g. for sounds that
// are generated in code (see also Synth.h).
[[nodiscard]] std::optional<Sound> MakeSound(std::span<const f32> samples, i32 channels,
                                             i32 sampleRate);

} // namespace Emerald

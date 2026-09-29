#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <SDL3/SDL_audio.h>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// The mix format: every Sound is converted to it when loaded, so playing one is just a copy.
inline constexpr SDL_AudioSpec kMixSpec{SDL_AUDIO_F32, 2, 48000};

// A decoded sound in the mix format: interleaved stereo f32 samples (left, right, left, ...).
struct Sound {
    std::vector<f32> Samples;

    [[nodiscard]] usize GetFrameCount() const
    {
        return Samples.size() / static_cast<usize>(kMixSpec.channels);
    }
    [[nodiscard]] f32 GetDurationSeconds() const
    {
        return static_cast<f32>(GetFrameCount()) / static_cast<f32>(kMixSpec.freq);
    }
};

// Loads a .wav (SDL_LoadWAV) or .mp3 (dr_mp3) file, picked by the extension (case-insensitive).
// Returns nothing (and logs why) if the file is missing, broken or of another type.
[[nodiscard]] std::optional<Sound> LoadSound(const std::filesystem::path& path);
[[nodiscard]] std::optional<Sound> LoadWav(const std::filesystem::path& path);
[[nodiscard]] std::optional<Sound> LoadMp3(const std::filesystem::path& path);
// Decodes an MP3 that is already in memory (e.g. embedded in the executable).
[[nodiscard]] std::optional<Sound> LoadMp3FromMemory(const void* data, usize size);
// Converts interleaved f32 samples in any rate/channel count to a Sound, e.g. for sounds that
// are generated in code.
[[nodiscard]] std::optional<Sound> MakeSound(std::span<const f32> samples, i32 channels,
                                             i32 sampleRate);

} // namespace Emerald

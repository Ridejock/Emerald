#include "Emerald/Audio/Sound.h"

#include <cctype>
#include <string>
#include <utility>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <dr_mp3.h>

#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// Logging is optional here, so the loaders also work in tests without the logger.
template <typename... Args> void LogError(spdlog::format_string_t<Args...> format, Args&&... args)
{
    if (Log::Core())
        Log::Core()->error(format, std::forward<Args>(args)...);
}

// Paths go to SDL as UTF-8, which works for non-ASCII names on every platform.
std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

// Converts any SDL audio data to the mix format with SDL_ConvertAudioSamples.
std::optional<Sound> Convert(const SDL_AudioSpec& spec, const void* data, usize bytes)
{
    Uint8* converted = nullptr;
    int convertedBytes = 0;
    if (!SDL_ConvertAudioSamples(&spec, static_cast<const Uint8*>(data), static_cast<int>(bytes),
                                 &kMixSpec, &converted, &convertedBytes)) {
        LogError("Audio conversion failed: {}", SDL_GetError());
        return std::nullopt;
    }
    std::vector<f32> samples(static_cast<usize>(convertedBytes) / sizeof(f32));
    SDL_memcpy(samples.data(), converted, samples.size() * sizeof(f32));
    SDL_free(converted);
    return Sound(std::move(samples));
}

} // namespace

std::optional<Sound> LoadSound(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".wav")
        return LoadWav(path);
    if (ext == ".mp3")
        return LoadMp3(path);
    LogError("LoadSound: '{}' is not a .wav or .mp3 file", path.string());
    return std::nullopt;
}

std::optional<Sound> LoadWav(const std::filesystem::path& path)
{
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 bytes = 0;
    if (!SDL_LoadWAV(ToUtf8(path).c_str(), &spec, &data, &bytes)) {
        LogError("Failed to load '{}': {}", path.string(), SDL_GetError());
        return std::nullopt;
    }
    std::optional<Sound> sound = Convert(spec, data, bytes);
    SDL_free(data);
    return sound;
}

std::optional<Sound> LoadMp3(const std::filesystem::path& path)
{
    usize size = 0;
    void* file = SDL_LoadFile(ToUtf8(path).c_str(), &size);
    if (!file) {
        LogError("Failed to load '{}': {}", path.string(), SDL_GetError());
        return std::nullopt;
    }
    std::optional<Sound> sound = LoadMp3FromMemory(file, size);
    SDL_free(file);
    if (!sound)
        LogError("Failed to decode MP3 '{}'", path.string());
    return sound;
}

std::optional<Sound> LoadMp3FromMemory(const void* data, usize size)
{
    // dr_mp3 decodes the whole file to f32 at the file's own rate and channel count.
    drmp3_config info{};
    drmp3_uint64 frames = 0;
    f32* samples = drmp3_open_memory_and_read_pcm_frames_f32(data, size, &info, &frames, nullptr);
    if (!samples || frames == 0) {
        drmp3_free(samples, nullptr);
        LogError("MP3 decoding failed");
        return std::nullopt;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, static_cast<int>(info.channels),
                             static_cast<int>(info.sampleRate)};
    std::optional<Sound> sound =
        Convert(spec, samples, static_cast<usize>(frames) * info.channels * sizeof(f32));
    drmp3_free(samples, nullptr);
    return sound;
}

std::optional<Sound> MakeSound(std::span<const f32> samples, i32 channels, i32 sampleRate)
{
    const SDL_AudioSpec spec{SDL_AUDIO_F32, channels, sampleRate};
    return Convert(spec, samples.data(), samples.size_bytes());
}

} // namespace Emerald

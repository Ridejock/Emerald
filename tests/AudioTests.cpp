// Tests for sound loading: MP3 decoding (dr_mp3, from an embedded file), WAV loading, conversion
// to the mix format, and LoadSound's choice by extension. No audio device is needed.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include <Emerald/Audio/Sound.h>

#include "Test.h"
#include "TinyMp3.h"

using namespace Emerald;

namespace {

// Sign changes of the left channel in [first, last) frames: a sine of f Hz has 2 * f per second.
i32 CountZeroCrossings(const Sound& sound, usize first, usize last)
{
    i32 count = 0;
    for (usize i = first + 1; i < last; ++i) {
        const f32 a = sound.GetSamples()[(i - 1) * 2];
        const f32 b = sound.GetSamples()[i * 2];
        if ((a < 0.0f) != (b < 0.0f))
            ++count;
    }
    return count;
}

f32 Peak(const Sound& sound)
{
    f32 peak = 0.0f;
    for (f32 s : sound.GetSamples())
        peak = std::max(peak, std::abs(s));
    return peak;
}

// Writes a mono 16-bit PCM WAV file.
void WriteWav(const std::filesystem::path& path, const std::vector<i16>& samples, u32 rate)
{
    const auto u32le = [](std::ofstream& f, u32 v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    };
    const auto u16le = [](std::ofstream& f, u16 v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    };
    const u32 dataBytes = static_cast<u32>(samples.size() * sizeof(i16));
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    u32le(f, 36 + dataBytes);
    f.write("WAVEfmt ", 8);
    u32le(f, 16);       // fmt chunk size
    u16le(f, 1);        // PCM
    u16le(f, 1);        // mono
    u32le(f, rate);     // sample rate
    u32le(f, rate * 2); // bytes per second
    u16le(f, 2);        // bytes per frame
    u16le(f, 16);       // bits per sample
    f.write("data", 4);
    u32le(f, dataBytes);
    f.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
}

} // namespace

TEST(Mp3DecodesToMixFormat)
{
    const std::optional<Sound> sound = LoadMp3FromMemory(kTinyMp3, sizeof(kTinyMp3));
    CHECK(sound.has_value());
    if (!sound)
        return;
    // 0.1 s (MP3 frames add some padding), now stereo at 48 kHz.
    CHECK(sound->GetDurationSeconds() > 0.09f && sound->GetDurationSeconds() < 0.16f);
    CHECK(sound->GetSamples().size() % 2 == 0);
    // Amplitude 1/8, and the mono source is the same on both channels.
    const f32 peak = Peak(*sound);
    CHECK(peak > 0.1f && peak < 0.15f);
    const usize mid = sound->GetFrameCount() / 2;
    CHECK_NEAR(sound->GetSamples()[mid * 2], sound->GetSamples()[mid * 2 + 1]);
    // Still 440 Hz after resampling 22050 -> 48000: 3000 frames = 62.5 ms = 55 crossings.
    const i32 crossings = CountZeroCrossings(*sound, 1000, 4000);
    CHECK(crossings >= 50 && crossings <= 60);
}

TEST(Mp3RejectsGarbage)
{
    const unsigned char garbage[64] = {1, 2, 3};
    CHECK(!LoadMp3FromMemory(garbage, sizeof(garbage)).has_value());
}

TEST(LoadSoundPicksByExtension)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "EmeraldAudioTests";
    std::filesystem::create_directories(dir);

    // WAV: 0.1 s of a 440 Hz sine, mono 16-bit at 8 kHz. Upper-case extension on purpose.
    std::vector<i16> pcm(800);
    for (usize i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<i16>(
            16000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<f64>(i) / 8000.0));
    const std::filesystem::path wav = dir / "Tone.WAV";
    WriteWav(wav, pcm, 8000);
    const std::optional<Sound> tone = LoadSound(wav);
    CHECK(tone.has_value());
    if (tone) {
        CHECK_NEAR(tone->GetDurationSeconds(), 0.1f); // 4800 frames at 48 kHz
        const i32 crossings = CountZeroCrossings(*tone, 500, 3500);
        CHECK(crossings >= 50 && crossings <= 60);
    }

    // MP3 through a file.
    const std::filesystem::path mp3 = dir / "tiny.mp3";
    {
        std::ofstream f(mp3, std::ios::binary);
        f.write(reinterpret_cast<const char*>(kTinyMp3), sizeof(kTinyMp3));
    }
    CHECK(LoadSound(mp3).has_value());

    CHECK(!LoadSound(dir / "music.ogg").has_value()); // unsupported type
    CHECK(!LoadSound(dir / "missing.mp3").has_value());
    CHECK(!LoadSound(dir / "missing.wav").has_value());
    std::filesystem::remove_all(dir);
}

TEST(MakeSoundConverts)
{
    const std::vector<f32> mono(24000, 0.25f); // 1 s at 24 kHz
    const std::optional<Sound> sound = MakeSound(mono, 1, 24000);
    CHECK(sound.has_value());
    if (sound) {
        CHECK(sound->GetFrameCount() > 47900 && sound->GetFrameCount() <= 48000);
        CHECK_NEAR(sound->GetSamples()[1000], 0.25f);
    }
}

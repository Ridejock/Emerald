// Audio extras (#14): spatial falloff/pan, mixer groups, music streaming + crossfade.
// No audio device: Mixer::Mix and MusicStream from embedded TinyMp3 / TinyOgg.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include <Emerald/Audio/Mixer.h>
#include <Emerald/Audio/Music.h>
#include <Emerald/Audio/Spatial.h>
#include <Emerald/Audio/Synth.h>

#include "Test.h"
#include "TinyMp3.h"
#include "TinyOgg.h"

using namespace Emerald;

namespace {

Sound Blip()
{
    return Synth::ToSound(Synth::Generate(
        {.Shape = Synth::Wave::Sine, .Seconds = 0.05f, .StartHz = 880.0f, .Volume = 0.5f}));
}

std::vector<f32> Render(Mixer& mixer, usize frames)
{
    std::vector<f32> out(frames * 2);
    mixer.Mix(out.data(), frames);
    return out;
}

f32 Peak(std::span<const f32> s)
{
    f32 p = 0.0f;
    for (f32 x : s)
        p = std::max(p, std::abs(x));
    return p;
}

} // namespace

TEST(SpatialAttenuationAndPan)
{
    const SpatialParams sound{
        .Position = {100.0f, 0.0f}, .MinDistance = 50.0f, .MaxDistance = 200.0f};
    CHECK(SpatialAttenuation({100.0f, 0.0f}, sound) == 1.0f); // on top of it
    CHECK(SpatialAttenuation({150.0f, 0.0f}, sound) == 1.0f); // inside min (distance 50)
    // Halfway through the fade band: distance 125 -> 1 - 75/150 = 0.5.
    CHECK_NEAR_EPS(SpatialAttenuation({225.0f, 0.0f}, sound), 0.5f, 1e-5f);
    CHECK(SpatialAttenuation({300.0f, 0.0f}, sound) == 0.0f);
    CHECK(SpatialPan({0.0f, 0.0f}, sound) == 0.5f); // 100/200
    CHECK(SpatialPan({100.0f, 0.0f}, sound) == 0.0f);
    f32 l = 0, r = 0;
    SpatialGains(-1.0f, l, r);
    CHECK(l == 1.0f && r == 0.0f);
    SpatialGains(0.0f, l, r);
    CHECK(l == 1.0f && r == 1.0f);
}

TEST(MixerGroupsMuteAndVolume)
{
    Mixer mixer;
    // A long constant tone so level compares are not racing the end of the sound.
    const Sound s = Sound(std::vector<f32>(48000 * 2, 0.5f));
    const VoiceHandle onlySfx =
        mixer.Play(s, {.Volume = 1.0f, .Loop = true, .FadeInMs = 0.0f, .Group = AudioGroup::Sfx});
    CHECK(onlySfx.IsValid());
    mixer.SetGroupMuted(AudioGroup::Sfx, true);
    (void)Render(mixer, 2000); // finish the ~20 ms mute ramp
    CHECK(Peak(Render(mixer, 100)) < 0.01f);
    mixer.SetGroupMuted(AudioGroup::Sfx, false);
    mixer.SetGroupVolume(AudioGroup::Sfx, 0.25f);
    (void)Render(mixer, 2000);
    const f32 quiet = Peak(Render(mixer, 100));
    mixer.SetGroupVolume(AudioGroup::Sfx, 1.0f);
    (void)Render(mixer, 2000);
    const f32 loud = Peak(Render(mixer, 100));
    CHECK(loud > quiet * 2.0f);
    CHECK(mixer.GetGroupVolume(AudioGroup::Music) == 1.0f);
}

TEST(MixerSpatialFollowsListener)
{
    Mixer mixer;
    mixer.SetListener({0.0f, 0.0f});
    const Sound s = Blip();
    // Far to the right: quiet and panned right.
    const VoiceHandle v = mixer.Play(s, {.Volume = 1.0f,
                                         .Loop = true,
                                         .FadeInMs = 0.0f,
                                         .Position = Vec2(500.0f, 0.0f),
                                         .MinDistance = 50.0f,
                                         .MaxDistance = 400.0f});
    CHECK(v.IsValid());
    const auto far = Render(mixer, 100);
    f32 farL = 0, farR = 0;
    for (usize i = 0; i < far.size(); i += 2) {
        farL = std::max(farL, std::abs(far[i]));
        farR = std::max(farR, std::abs(far[i + 1]));
    }
    CHECK(farL == 0.0f && farR == 0.0f); // beyond MaxDistance

    mixer.SetVoicePosition(v, {100.0f, 0.0f});
    (void)Render(mixer, 1000); // ramp
    const auto near = Render(mixer, 100);
    CHECK(Peak(near) > 0.05f);
    // Listener jumps next to it: still audible.
    mixer.SetListener({100.0f, 0.0f});
    (void)Render(mixer, 1000);
    CHECK(Peak(Render(mixer, 100)) > 0.05f);
}

TEST(MusicStreamMp3AndOggConstantMemory)
{
    MusicStream mp3;
    CHECK(mp3.OpenMemory(std::span(kTinyMp3, sizeof(kTinyMp3)), "mp3"));
    CHECK(mp3.IsOpen());
    CHECK(MusicStream::GetRingBytes() == MusicStream::kRingFrames * 2 * sizeof(f32));
    // Drain and refill many times: ring size stays fixed (Pump never grows it).
    f32 buf[512 * 2];
    for (int i = 0; i < 40; ++i) {
        mp3.Pump();
        CHECK(mp3.GetBufferedFrames() <= MusicStream::kRingFrames);
        (void)mp3.Read(buf, 512);
    }
    mp3.Close();

    MusicStream ogg;
    CHECK(ogg.OpenMemory(std::span(kTinyOgg, sizeof(kTinyOgg)), "ogg"));
    ogg.SetLoop(false);
    usize total = 0;
    for (int i = 0; i < 20; ++i) {
        ogg.Pump();
        total += ogg.Read(buf, 512);
    }
    CHECK(total > 0);
}

TEST(MusicCrossfade)
{
    Mixer mixer;
    CHECK(mixer.GetMusic().PlayMemory(std::span(kTinyMp3, sizeof(kTinyMp3)), "mp3", 50.0f));
    CHECK(mixer.GetMusic().IsPlaying());
    // Let it become audible.
    CHECK(Peak(Render(mixer, 5000)) > 0.001f);
    CHECK(mixer.GetMusic().CrossfadeMemory(std::span(kTinyOgg, sizeof(kTinyOgg)), "ogg", 50.0f));
    // During/after crossfade something is still playing.
    CHECK(mixer.GetMusic().IsPlaying());
    CHECK(Peak(Render(mixer, 8000)) > 0.0f);
    mixer.GetMusic().Stop(0.0f);
    (void)Render(mixer, 100);
    CHECK(!mixer.GetMusic().IsPlaying());
}

TEST(MusicStreamOpensFiles)
{
    // Write the embedded ogg to a temp file and open by path (covers the file path).
    const auto dir = std::filesystem::temp_directory_path() / "emerald_music_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "t.ogg";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(kTinyOgg), sizeof(kTinyOgg));
    }
    MusicStream stream;
    CHECK(stream.Open(path));
    stream.Pump();
    CHECK(stream.GetBufferedFrames() > 0);
    stream.Close();
    std::filesystem::remove_all(dir);
}

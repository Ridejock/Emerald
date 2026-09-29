// Tests for the Mixer (handles, fades, looping, pitch, pan, master/mute, limiter) and the Synth
// helpers. The mixer is driven directly with Mix(), so no audio device is needed.

#include <algorithm>
#include <cmath>
#include <vector>

#include <Emerald/Audio/Mixer.h>
#include <Emerald/Audio/Synth.h>

#include "Test.h"

using namespace Emerald;

namespace {

constexpr usize kRate = 48000;

// A constant-level stereo sound, `frames` long.
Sound Constant(f32 level, usize frames)
{
    return Sound(std::vector<f32>(frames * 2, level));
}

// Mixes `frames` frames and returns them (interleaved stereo).
std::vector<f32> Render(Mixer& mixer, usize frames)
{
    std::vector<f32> out(frames * 2);
    mixer.Mix(out.data(), frames);
    return out;
}

f32 Peak(std::span<const f32> samples)
{
    f32 peak = 0.0f;
    for (f32 s : samples)
        peak = std::max(peak, std::abs(s));
    return peak;
}

bool AllFinite(std::span<const f32> samples)
{
    return std::all_of(samples.begin(), samples.end(), [](f32 s) { return std::isfinite(s); });
}

// Largest difference between neighboring left samples: big jumps are audible clicks.
f32 LargestStep(const std::vector<f32>& stereo)
{
    f32 largest = 0.0f;
    for (usize i = 2; i < stereo.size(); i += 2)
        largest = std::max(largest, std::abs(stereo[i] - stereo[i - 2]));
    return largest;
}

} // namespace

TEST(MixerHandles)
{
    Mixer mixer;
    CHECK(!mixer.IsPlaying({})); // default handle is invalid
    const Sound sound = Constant(0.5f, 100);
    const VoiceHandle a = mixer.Play(sound);
    CHECK(a.IsValid() && mixer.IsPlaying(a));
    (void)Render(mixer, 200); // the sound ends
    CHECK(!mixer.IsPlaying(a));

    // The voice is reused with a new generation: the old handle stays stale and harmless.
    const VoiceHandle b = mixer.Play(sound);
    CHECK(b.Index == a.Index && b.Generation != a.Generation);
    mixer.Stop(a, 0.0f);
    mixer.SetVolume(a, 0.0f);
    CHECK(mixer.IsPlaying(b));
    CHECK(mixer.GetPlayingCount() == 1);
    CHECK(!mixer.Play(Sound{}).IsValid()); // empty sounds do not play

    // Samples of finished voices are released on request (on the game thread).
    (void)Render(mixer, 200);
    const Sound shared = Constant(0.1f, 10);
    const VoiceHandle c = mixer.Play(shared);
    (void)Render(mixer, 20);
    CHECK(!mixer.IsPlaying(c) && shared.GetData().use_count() == 2);
    mixer.ReleaseFinished();
    CHECK(shared.GetData().use_count() == 1);
}

TEST(MixerFadesAndStops)
{
    Mixer mixer;
    const VoiceHandle v = mixer.Play(Constant(0.5f, kRate), {.FadeInMs = 2.0f});
    std::vector<f32> out = Render(mixer, 480); // 10 ms
    CHECK(std::abs(out[0]) < 0.01f);           // starts from silence...
    CHECK_NEAR(out[479 * 2], 0.5f);            // ...reaches the volume after 2 ms
    CHECK(LargestStep(out) < 0.01f);           // with no click

    mixer.SetVolume(v, 0.25f); // 10 ms ramp; the sound itself is at 0.5
    out = Render(mixer, 960);
    CHECK_NEAR(out[959 * 2], 0.125f);
    CHECK(LargestStep(out) < 0.01f);

    mixer.Stop(v, 20.0f);      // 20 ms = 960 frames
    CHECK(mixer.IsPlaying(v)); // still fading
    out = Render(mixer, 480);
    CHECK(mixer.IsPlaying(v) && out[479 * 2] > 0.05f && out[479 * 2] < 0.075f); // halfway
    CHECK(LargestStep(out) < 0.01f);
    out = Render(mixer, 600);
    CHECK(!mixer.IsPlaying(v));
    CHECK(Peak(std::span<const f32>(out).subspan(1000)) == 0.0f); // silent afterwards
}

TEST(MixerLoopsPitchAndPan)
{
    Mixer mixer;
    const Sound sound = Constant(0.5f, 100);
    const VoiceHandle loop = mixer.Play(sound, {.Loop = true, .FadeInMs = 0.0f});
    const std::vector<f32> out = Render(mixer, 1000); // ten times the sound's length
    CHECK(mixer.IsPlaying(loop));
    CHECK(std::all_of(out.begin(), out.end(), [](f32 s) { return std::abs(s - 0.5f) < 1e-5f; }));
    mixer.Stop(loop, 0.0f);
    CHECK(!mixer.IsPlaying(loop));

    // Pitch 2 plays twice as fast: 100 frames are done after 50.
    const VoiceHandle fast = mixer.Play(sound, {.Pitch = 2.0f, .FadeInMs = 0.0f});
    (void)Render(mixer, 49);
    CHECK(mixer.IsPlaying(fast));
    (void)Render(mixer, 2);
    CHECK(!mixer.IsPlaying(fast));

    // Pan fully left: the right channel is silent, the left one untouched.
    (void)mixer.Play(sound, {.Pan = -1.0f, .FadeInMs = 0.0f});
    const std::vector<f32> left = Render(mixer, 10);
    CHECK_NEAR(left[4], 0.5f);
    CHECK(left[5] == 0.0f);
}

TEST(MixerMasterMuteAndLimiter)
{
    // The limiter leaves normal levels alone and never exceeds 1.
    CHECK(SoftLimit(0.5f) == 0.5f && SoftLimit(-0.8f) == -0.8f);
    CHECK(SoftLimit(1.0f) > 0.9f && SoftLimit(1.0f) < 1.0f);
    CHECK(SoftLimit(100.0f) <= 1.0f && SoftLimit(-100.0f) >= -1.0f);
    CHECK(SoftLimit(0.81f) > SoftLimit(0.8f)); // still rising past the knee

    Mixer mixer;
    // 32 loud sounds at once: squashed below 1 instead of clipping at 16.
    const Sound loud = Constant(0.5f, kRate);
    for (usize i = 0; i < Mixer::kVoiceCount; ++i)
        (void)mixer.Play(loud, {.FadeInMs = 0.0f});
    std::vector<f32> out = Render(mixer, 1000);
    CHECK(Peak(out) <= 1.0f && Peak(out) > 0.95f);
    CHECK(AllFinite(out));
    // A 33rd sound replaces the oldest one; the count stays at the limit.
    CHECK(mixer.Play(loud).IsValid() && mixer.GetPlayingCount() == Mixer::kVoiceCount);

    mixer.StopAll(0.0f);
    (void)mixer.Play(loud, {.FadeInMs = 0.0f});
    mixer.SetMasterVolume(0.5f); // ramps over 20 ms
    CHECK(mixer.GetMasterVolume() == 0.5f);
    out = Render(mixer, 2000);
    CHECK_NEAR(out[1999 * 2], 0.25f);
    CHECK(LargestStep(out) < 0.01f);
    mixer.SetMuted(true);
    out = Render(mixer, 2000);
    CHECK(mixer.IsMuted() && out[1999 * 2] == 0.0f);
    CHECK(LargestStep(out) < 0.01f);
}

TEST(SynthOutput)
{
    using namespace Emerald::Synth;
    const Wave shapes[] = {Wave::Sine, Wave::Square, Wave::Triangle, Wave::Saw, Wave::Noise};
    for (Wave shape : shapes) {
        std::vector<f32> s = Generate({.Shape = shape,
                                       .Seconds = 0.25f,
                                       .StartHz = 200.0f,
                                       .EndHz = 1000.0f,
                                       .Volume = 0.8f});
        CHECK(s.size() == kRate / 4);
        CHECK(AllFinite(s));
        CHECK(Peak(s) <= 0.8f + 1e-5f && Peak(s) > 0.5f);
        ApplyAdsr(s, {});
        LowPass(s, 2000.0f);
        ApplyDecay(s, 0.05f);
        CHECK(AllFinite(s) && Peak(s) <= 0.8f);
        CHECK(std::abs(s.back()) < 0.01f); // envelope and decay end near silence
    }

    // Square at 1 kHz: 2 sign changes per cycle.
    const std::vector<f32> square =
        Generate({.Shape = Wave::Square, .Seconds = 0.1f, .StartHz = 1000.0f});
    i32 changes = 0;
    for (usize i = 1; i < square.size(); ++i)
        changes += (square[i] < 0.0f) != (square[i - 1] < 0.0f) ? 1 : 0;
    CHECK(changes >= 198 && changes <= 201);

    // A lowpass takes the edge off: neighbors get much closer.
    std::vector<f32> noise = Generate({.Shape = Wave::Noise, .Seconds = 0.1f, .StartHz = 20000.0f});
    std::vector<f32> dull = noise;
    LowPass(dull, 300.0f);
    CHECK(Peak(dull) < Peak(noise));

    std::vector<f32> layered = Silence(0.1f);
    MixInto(layered, square, 2.0f); // 2 * 0.5 = 1.0
    const Sound sound = ToSound(layered);
    CHECK(sound.GetFrameCount() == layered.size());
    CHECK(Peak(sound.GetSamples()) <= 1.0f);
    CHECK(sound.GetSamples()[0] == sound.GetSamples()[1]); // mono on both sides
}

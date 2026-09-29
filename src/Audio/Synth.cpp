#include "Emerald/Audio/Synth.h"

#include <algorithm>
#include <cmath>

#include "Emerald/Math/Common.h"

namespace Emerald::Synth {

namespace {

constexpr f32 kRate = static_cast<f32>(kMixSpec.freq);

usize FrameCount(f32 seconds)
{
    return static_cast<usize>(std::max(seconds, 0.0f) * kRate);
}

// Small, fast pseudo-random numbers (xorshift32) in -1..1; the same seed gives the same noise.
f32 NextNoise(u32& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<f32>(state) / 2147483648.0f - 1.0f;
}

} // namespace

std::vector<f32> Generate(const Tone& tone, u32 seed)
{
    std::vector<f32> out(FrameCount(tone.Seconds));
    const f32 startHz = std::max(tone.StartHz, 1.0f);
    const f32 endHz = tone.EndHz > 0.0f ? tone.EndHz : startHz;
    u32 noise = seed != 0 ? seed : 1;
    f32 held = NextNoise(noise);
    f32 phase = 0.0f; // 0..1 within the current cycle

    for (usize i = 0; i < out.size(); ++i) {
        f32 value = 0.0f;
        switch (tone.Shape) {
        case Wave::Sine:
            value = std::sin(TwoPi * phase);
            break;
        case Wave::Square:
            value = phase < tone.Duty ? 1.0f : -1.0f;
            break;
        case Wave::Triangle:
            value = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
            break;
        case Wave::Saw:
            value = 2.0f * phase - 1.0f;
            break;
        case Wave::Noise:
            value = held;
            break;
        }
        out[i] = value * tone.Volume;

        // Exponential sweep: equal musical steps per second (sounds even, unlike linear Hz).
        const f32 t = static_cast<f32>(i) / static_cast<f32>(out.size());
        f32 hz = startHz * std::pow(endHz / startHz, t);
        if (tone.VibratoHz > 0.0f) {
            const f32 seconds = static_cast<f32>(i) / kRate;
            hz *= 1.0f + tone.VibratoDepth * std::sin(TwoPi * tone.VibratoHz * seconds);
        }
        phase += std::max(hz, 0.0f) / kRate;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            held = NextNoise(noise); // noise: a new random level every cycle
        }
    }
    return out;
}

void ApplyDecay(std::span<f32> samples, f32 halfLife)
{
    // Multiplying by the same factor every sample gives an exponential curve.
    const f32 factor = std::pow(0.5f, 1.0f / (std::max(halfLife, 0.0001f) * kRate));
    f32 gain = 1.0f;
    for (f32& s : samples) {
        s *= gain;
        gain *= factor;
    }
}

void ApplyAdsr(std::span<f32> samples, const Adsr& e)
{
    const f32 total = static_cast<f32>(samples.size()) / kRate;
    const f32 releaseStart = std::max(total - e.Release, 0.0f);
    for (usize i = 0; i < samples.size(); ++i) {
        const f32 t = static_cast<f32>(i) / kRate;
        f32 level = e.Sustain;
        if (t < e.Attack)
            level = t / e.Attack;
        else if (t < e.Attack + e.Decay)
            level = 1.0f + (e.Sustain - 1.0f) * (t - e.Attack) / e.Decay;
        if (t >= releaseStart && e.Release > 0.0f)
            level *= std::max(1.0f - (t - releaseStart) / e.Release, 0.0f);
        samples[i] *= level;
    }
}

void LowPass(std::span<f32> samples, f32 cutoffHz)
{
    // y += a * (x - y): each output moves a fraction `a` towards the input. Fast changes (high
    // frequencies) are smoothed away; `a` comes from the cutoff frequency.
    const f32 a = 1.0f - std::exp(-TwoPi * std::max(cutoffHz, 1.0f) / kRate);
    f32 y = 0.0f;
    for (f32& s : samples) {
        y += a * (s - y);
        s = y;
    }
}

void MixInto(std::span<f32> target, std::span<const f32> source, f32 gain)
{
    const usize n = std::min(target.size(), source.size());
    for (usize i = 0; i < n; ++i)
        target[i] += source[i] * gain;
}

std::vector<f32> Silence(f32 seconds)
{
    return std::vector<f32>(FrameCount(seconds), 0.0f);
}

Sound ToSound(std::span<const f32> mono)
{
    std::vector<f32> stereo(mono.size() * 2);
    for (usize i = 0; i < mono.size(); ++i)
        stereo[i * 2] = stereo[i * 2 + 1] = std::clamp(mono[i], -1.0f, 1.0f);
    return Sound(std::move(stereo));
}

} // namespace Emerald::Synth

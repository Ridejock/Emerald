#include "Emerald/Audio/Mixer.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

namespace {

constexpr f32 kMasterRampMs = 20.0f;
constexpr f32 kLimiterKnee = 0.8f;

} // namespace

f32 SoftLimit(f32 x)
{
    const f32 magnitude = std::abs(x);
    if (magnitude <= kLimiterKnee)
        return x;
    // tanh continues the straight line smoothly (same slope at the knee) and flattens out at 1.
    constexpr f32 headroom = 1.0f - kLimiterKnee;
    const f32 limited = kLimiterKnee + headroom * std::tanh((magnitude - kLimiterKnee) / headroom);
    return x < 0.0f ? -limited : limited;
}

VoiceHandle Mixer::Play(const Sound& sound, const PlayOptions& options)
{
    if (sound.IsEmpty())
        return {};
    // A free voice, or else the oldest one (looping sounds only if everything loops).
    Voice* voice = nullptr;
    for (Voice& v : m_Voices) {
        if (!v.Playing) {
            voice = &v;
            break;
        }
    }
    if (!voice) {
        for (Voice& v : m_Voices) {
            const bool better = !voice || (voice->Loop && !v.Loop) ||
                                (voice->Loop == v.Loop && v.StartOrder < voice->StartOrder);
            if (better)
                voice = &v;
        }
    }

    const u32 generation = voice->Generation + 1 == 0 ? 1 : voice->Generation + 1;
    *voice = Voice{};
    voice->Samples = sound.GetData();
    voice->Pitch = std::max(options.Pitch, 0.01f);
    // Balance pan: the far side gets quieter, the center plays both sides at full volume.
    const f32 pan = std::clamp(options.Pan, -1.0f, 1.0f);
    voice->Left = std::min(1.0f, 1.0f - pan);
    voice->Right = std::min(1.0f, 1.0f + pan);
    voice->Loop = options.Loop;
    voice->Generation = generation;
    voice->StartOrder = ++m_PlayCount;
    voice->Playing = true;
    RampTo(voice->Gain, std::max(options.Volume, 0.0f), options.FadeInMs); // from 0
    return {static_cast<u32>(voice - m_Voices.data()), generation};
}

void Mixer::Stop(VoiceHandle voice, f32 fadeMs)
{
    if (Voice* v = Find(voice)) {
        v->Stopping = true;
        RampTo(v->Gain, 0.0f, fadeMs);
        if (v->Gain.Value <= 0.0f)
            v->Playing = false;
    }
}

void Mixer::StopAll(f32 fadeMs)
{
    for (u32 i = 0; i < kVoiceCount; ++i)
        Stop({i, m_Voices[i].Generation}, fadeMs);
}

void Mixer::SetVolume(VoiceHandle voice, f32 volume, f32 rampMs)
{
    Voice* v = Find(voice);
    if (v && !v->Stopping)
        RampTo(v->Gain, std::max(volume, 0.0f), rampMs);
}

bool Mixer::IsPlaying(VoiceHandle voice) const
{
    return Find(voice) != nullptr;
}

usize Mixer::GetPlayingCount() const
{
    return static_cast<usize>(
        std::count_if(m_Voices.begin(), m_Voices.end(), [](const Voice& v) { return v.Playing; }));
}

void Mixer::SetMasterVolume(f32 volume)
{
    m_MasterVolume = std::max(volume, 0.0f);
    UpdateMasterTarget();
}

void Mixer::SetMuted(bool muted)
{
    m_Muted = muted;
    UpdateMasterTarget();
}

void Mixer::Mix(f32* out, usize frames)
{
    std::fill(out, out + frames * 2, 0.0f);

    for (Voice& v : m_Voices) {
        if (!v.Playing)
            continue;
        const std::vector<f32>& samples = *v.Samples;
        const usize count = samples.size() / 2; // frames in the sound
        for (usize i = 0; i < frames; ++i) {
            // Linear interpolation between the two frames around the (fractional) position.
            const usize a = static_cast<usize>(v.Position);
            const f32 t = static_cast<f32>(v.Position - static_cast<f64>(a));
            usize b = a + 1;
            if (b >= count)
                b = v.Loop ? 0 : a; // at the very end: hold the last frame
            const f32 gain = v.Gain.Next();
            out[i * 2] += (samples[a * 2] + (samples[b * 2] - samples[a * 2]) * t) * gain * v.Left;
            out[i * 2 + 1] += (samples[a * 2 + 1] + (samples[b * 2 + 1] - samples[a * 2 + 1]) * t) *
                              gain * v.Right;

            v.Position += static_cast<f64>(v.Pitch);
            if (v.Position >= static_cast<f64>(count)) {
                if (v.Loop) {
                    v.Position = std::fmod(v.Position, static_cast<f64>(count));
                } else {
                    v.Playing = false;
                    break;
                }
            }
            if (v.Stopping && v.Gain.Value <= 0.0f) {
                v.Playing = false;
                break;
            }
        }
    }

    // Master volume (ramped, so mute and slider moves do not click), then the limiter.
    for (usize i = 0; i < frames; ++i) {
        const f32 master = m_Master.Next();
        out[i * 2] = SoftLimit(out[i * 2] * master);
        out[i * 2 + 1] = SoftLimit(out[i * 2 + 1] * master);
    }
}

void Mixer::ReleaseFinished()
{
    for (Voice& v : m_Voices) {
        if (!v.Playing)
            v.Samples.reset();
    }
}

Mixer::Voice* Mixer::Find(VoiceHandle voice)
{
    if (!voice.IsValid() || voice.Index >= kVoiceCount)
        return nullptr;
    Voice& v = m_Voices[voice.Index];
    return v.Playing && v.Generation == voice.Generation ? &v : nullptr;
}

const Mixer::Voice* Mixer::Find(VoiceHandle voice) const
{
    return const_cast<Mixer*>(this)->Find(voice);
}

void Mixer::RampTo(Ramp& ramp, f32 target, f32 ms) const
{
    ramp.Target = target;
    const f32 samples = ms * static_cast<f32>(m_SampleRate) / 1000.0f;
    if (samples < 1.0f) {
        ramp.Value = target;
        ramp.Step = 0.0f;
    } else {
        ramp.Step = std::abs(target - ramp.Value) / samples;
    }
}

void Mixer::UpdateMasterTarget()
{
    RampTo(m_Master, m_Muted ? 0.0f : m_MasterVolume, kMasterRampMs);
}

} // namespace Emerald

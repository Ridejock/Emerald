#include "Emerald/Audio/Mixer.h"

#include <algorithm>
#include <cmath>

namespace Emerald {

Mixer::Mixer(i32 sampleRate) : m_SampleRate(sampleRate)
{
    m_Music.SetSampleRate(sampleRate);
}

Mixer::~Mixer() = default;

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
    voice->Group = options.Group;
    voice->BaseVolume = std::max(options.Volume, 0.0f);
    voice->Loop = options.Loop;
    voice->Generation = generation;
    voice->StartOrder = ++m_PlayCount;
    voice->Playing = true;
    if (options.Position) {
        voice->Spatial = true;
        voice->SpatialWorld = {.Position = *options.Position,
                               .MinDistance = options.MinDistance,
                               .MaxDistance = options.MaxDistance};
        ApplySpatial(*voice, options.FadeInMs);
    } else {
        const f32 pan = std::clamp(options.Pan, -1.0f, 1.0f);
        voice->Left = std::min(1.0f, 1.0f - pan);
        voice->Right = std::min(1.0f, 1.0f + pan);
        RampTo(voice->Gain, voice->BaseVolume, options.FadeInMs);
    }
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
    if (!v || v->Stopping)
        return;
    v->BaseVolume = std::max(volume, 0.0f);
    if (v->Spatial)
        ApplySpatial(*v, rampMs);
    else
        RampTo(v->Gain, v->BaseVolume, rampMs);
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

    // Per-sample group gains so mute/volume ramps stay click-free with the master.
    std::array<f32, static_cast<usize>(AudioGroup::Count)> groupGain{};
    for (usize i = 0; i < frames; ++i) {
        for (usize g = 0; g < groupGain.size(); ++g)
            groupGain[g] = m_Groups[g].Gain.Next();

        for (Voice& v : m_Voices) {
            if (!v.Playing)
                continue;
            const std::vector<f32>& samples = *v.Samples;
            const usize count = samples.size() / 2;
            if (static_cast<usize>(v.Position) >= count) {
                if (v.Loop)
                    v.Position = std::fmod(v.Position, static_cast<f64>(count));
                else {
                    v.Playing = false;
                    continue;
                }
            }
            const usize a = static_cast<usize>(v.Position);
            const f32 t = static_cast<f32>(v.Position - static_cast<f64>(a));
            usize b = a + 1;
            if (b >= count)
                b = v.Loop ? 0 : a;
            const f32 gain = v.Gain.Next() * groupGain[static_cast<usize>(v.Group)];
            out[i * 2] += (samples[a * 2] + (samples[b * 2] - samples[a * 2]) * t) * gain * v.Left;
            out[i * 2 + 1] += (samples[a * 2 + 1] + (samples[b * 2 + 1] - samples[a * 2 + 1]) * t) *
                              gain * v.Right;

            v.Position += static_cast<f64>(v.Pitch);
            if (v.Stopping && v.Gain.Value <= 0.0f)
                v.Playing = false;
        }
    }

    m_Music.MixAdd(out, frames, m_Groups[static_cast<usize>(AudioGroup::Music)].Gain.Value);

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
    m_Music.Update();
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

void Mixer::SetGroupVolume(AudioGroup group, f32 volume)
{
    if (group >= AudioGroup::Count)
        return;
    Group& g = m_Groups[static_cast<usize>(group)];
    g.Volume = std::max(volume, 0.0f);
    UpdateGroupTarget(g);
}

f32 Mixer::GetGroupVolume(AudioGroup group) const
{
    if (group >= AudioGroup::Count)
        return 0.0f;
    return m_Groups[static_cast<usize>(group)].Volume;
}

void Mixer::SetGroupMuted(AudioGroup group, bool muted)
{
    if (group >= AudioGroup::Count)
        return;
    Group& g = m_Groups[static_cast<usize>(group)];
    g.Muted = muted;
    UpdateGroupTarget(g);
}

bool Mixer::IsGroupMuted(AudioGroup group) const
{
    if (group >= AudioGroup::Count)
        return true;
    return m_Groups[static_cast<usize>(group)].Muted;
}

void Mixer::UpdateGroupTarget(Group& group)
{
    RampTo(group.Gain, group.Muted ? 0.0f : group.Volume, kMasterRampMs);
}

void Mixer::SetListener(const Vec2& position)
{
    m_Listener = position;
    for (Voice& v : m_Voices) {
        if (v.Playing && v.Spatial && !v.Stopping)
            ApplySpatial(v, kDefaultFadeMs);
    }
}

void Mixer::SetVoicePosition(VoiceHandle voice, const Vec2& position)
{
    Voice* v = Find(voice);
    if (!v || !v->Spatial)
        return;
    v->SpatialWorld.Position = position;
    if (!v->Stopping)
        ApplySpatial(*v, kDefaultFadeMs);
}

void Mixer::ApplySpatial(Voice& voice, f32 rampMs)
{
    const f32 atten = SpatialAttenuation(m_Listener, voice.SpatialWorld);
    const f32 pan = SpatialPan(m_Listener, voice.SpatialWorld);
    SpatialGains(pan, voice.Left, voice.Right);
    RampTo(voice.Gain, voice.BaseVolume * atten, rampMs);
}

} // namespace Emerald

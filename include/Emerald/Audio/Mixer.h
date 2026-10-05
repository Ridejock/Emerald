#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "Emerald/Audio/Music.h"
#include "Emerald/Audio/Sound.h"
#include "Emerald/Audio/Spatial.h"
#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"

namespace Emerald {

// Refers to one playing sound. Voices are reused, so a handle also stores the voice's
// generation: once its sound has ended (or the voice was reused), the handle is stale and every
// call with it is a safe no-op. A default-constructed handle is never valid.
struct VoiceHandle {
    u32 Index = 0;
    u32 Generation = 0; // 0 = invalid
    [[nodiscard]] bool IsValid() const { return Generation != 0; }
};

// Bus for voice volumes: Music (streams + any voice tagged Music), Sfx, Ui.
enum class AudioGroup : u8 { Sfx = 0, Music = 1, Ui = 2, Count = 3 };

struct PlayOptions {
    f32 Volume = 1.0f;
    f32 Pan = 0.0f;      // -1 = left only, 0 = center, +1 = right only (ignored if Position set)
    f32 Pitch = 1.0f;    // playback speed: 2 = one octave up (and half as long)
    bool Loop = false;   // loops until stopped
    f32 FadeInMs = 2.0f; // a short ramp avoids a click at the start
    AudioGroup Group = AudioGroup::Sfx;
    // If set, pan and volume follow the listener (Spatial.h); Volume is the level at MinDistance.
    std::optional<Vec2> Position{}; // nullopt = not spatial
    f32 MinDistance = 64.0f;
    f32 MaxDistance = 512.0f;
};

// Soft limiter used on the master output: unchanged up to 0.8, then eases towards (never past)
// 1.0, so many loud sounds at once get squashed instead of clipping harshly.
[[nodiscard]] f32 SoftLimit(f32 x);

// Mixes up to kVoiceCount sounds into the mix format (stereo f32). This class does the math only;
// Audio owns one, feeds it to the audio device and makes it thread-safe. Mix() never allocates or
// frees memory (it runs on the audio thread): voices that finished keep their samples until
// ReleaseFinished() or the next Play() into that voice, which run on the game thread.
//
// Every volume change is ramped per sample (start, stop, SetVolume, master, mute), so nothing
// clicks.
class Mixer {
public:
    static constexpr usize kVoiceCount = 32;
    static constexpr f32 kDefaultFadeMs = 10.0f;

    explicit Mixer(i32 sampleRate = kMixSpec.freq);
    ~Mixer();

    // Starts a sound. When all voices are busy, the oldest non-looping sound is cut off.
    VoiceHandle Play(const Sound& sound, const PlayOptions& options = {});
    // Fades out and stops a sound (fadeMs = 0 stops at once).
    void Stop(VoiceHandle voice, f32 fadeMs = kDefaultFadeMs);
    void StopAll(f32 fadeMs = kDefaultFadeMs);
    void SetVolume(VoiceHandle voice, f32 volume, f32 rampMs = kDefaultFadeMs);
    // True until the sound has ended (or finished fading out).
    [[nodiscard]] bool IsPlaying(VoiceHandle voice) const;
    [[nodiscard]] usize GetPlayingCount() const;

    void SetMasterVolume(f32 volume);
    [[nodiscard]] f32 GetMasterVolume() const { return m_MasterVolume; }
    void SetMuted(bool muted);
    [[nodiscard]] bool IsMuted() const { return m_Muted; }

    // Per-group volume (0..) and mute; both ramp like the master. Applied on top of each voice's
    // own volume, and to MusicPlayer when Mix runs.
    void SetGroupVolume(AudioGroup group, f32 volume);
    [[nodiscard]] f32 GetGroupVolume(AudioGroup group) const;
    void SetGroupMuted(AudioGroup group, bool muted);
    [[nodiscard]] bool IsGroupMuted(AudioGroup group) const;

    // Listener for spatial voices (usually the camera). Moving it updates pans/volumes.
    void SetListener(const Vec2& position);
    [[nodiscard]] const Vec2& GetListener() const { return m_Listener; }
    // Move a spatial voice in the world (no-op if it was not started with Position).
    void SetVoicePosition(VoiceHandle voice, const Vec2& position);

    // Streaming music (crossfade, constant-memory decode). Mixed into the Music group.
    [[nodiscard]] MusicPlayer& GetMusic() { return m_Music; }
    [[nodiscard]] const MusicPlayer& GetMusic() const { return m_Music; }

    // Audio thread: writes `frames` stereo frames (2 * frames floats) to `out`.
    void Mix(f32* out, usize frames);
    // Game thread: lets go of the samples of voices that have finished; pumps music decode.
    void ReleaseFinished();

private:
    // A value that moves towards Target by at most Step per sample.
    struct Ramp {
        f32 Value = 0.0f;
        f32 Target = 0.0f;
        f32 Step = 0.0f;
        f32 Next()
        {
            if (Value < Target)
                Value = Value + Step < Target ? Value + Step : Target;
            else if (Value > Target)
                Value = Value - Step > Target ? Value - Step : Target;
            return Value;
        }
    };
    struct Voice {
        std::shared_ptr<const std::vector<f32>> Samples;
        f64 Position = 0.0; // in frames; fractional when the pitch is not 1
        f32 Pitch = 1.0f;
        f32 Left = 1.0f; // pan gains
        f32 Right = 1.0f;
        Ramp Gain;
        u64 StartOrder = 0; // for picking the oldest voice to cut off
        u32 Generation = 0;
        AudioGroup Group = AudioGroup::Sfx;
        f32 BaseVolume = 1.0f; // PlayOptions::Volume; spatial multiplies this
        bool Spatial = false;
        SpatialParams SpatialWorld{};
        bool Loop = false;
        bool Playing = false;
        bool Stopping = false; // fading out; ends when Gain reaches 0
    };
    struct Group {
        Ramp Gain{1.0f, 1.0f, 0.0f};
        f32 Volume = 1.0f;
        bool Muted = false;
    };

    void ApplySpatial(Voice& voice, f32 rampMs);
    void UpdateGroupTarget(Group& group);

    [[nodiscard]] Voice* Find(VoiceHandle voice);
    [[nodiscard]] const Voice* Find(VoiceHandle voice) const;
    // Sets a ramp's target, reached after `ms` milliseconds.
    void RampTo(Ramp& ramp, f32 target, f32 ms) const;
    void UpdateMasterTarget();

    std::array<Voice, kVoiceCount> m_Voices{};
    std::array<Group, static_cast<usize>(AudioGroup::Count)> m_Groups{};
    MusicPlayer m_Music;
    Vec2 m_Listener{};
    i32 m_SampleRate;
    u64 m_PlayCount = 0;
    Ramp m_Master{1.0f, 1.0f, 0.0f};
    f32 m_MasterVolume = 1.0f;
    bool m_Muted = false;
};

} // namespace Emerald

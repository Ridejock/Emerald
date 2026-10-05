#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// A compressed music file decoded a little at a time into a fixed ring of mix-format stereo
// frames (constant memory: the ring is ~0.25 s, the compressed file stays on disk / in the
// memory view). Supports .mp3 (dr_mp3) and .ogg (stb_vorbis). Pump() fills the ring on the game
// thread; Read() pulls for the mixer (zeros on underrun).
class MusicStream {
public:
    static constexpr usize kRingFrames = 12000; // 0.25 s at 48 kHz

    MusicStream();
    ~MusicStream();
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;

    // Opens a .mp3 / .ogg on disk. False (logged) on failure.
    [[nodiscard]] bool Open(const std::filesystem::path& path);
    // Same formats from a memory blob (tests: TinyMp3 / TinyOgg). The bytes must outlive the
    // stream (or be copied first - this keeps only a view).
    [[nodiscard]] bool OpenMemory(std::span<const u8> data, std::string_view formatHint);
    void Close();
    [[nodiscard]] bool IsOpen() const { return m_Decoder != nullptr; }

    void SetLoop(bool loop) { m_Loop = loop; }
    [[nodiscard]] bool IsLooping() const { return m_Loop; }

    // Decode into the ring until it is mostly full. Call from the game thread (Update).
    void Pump();
    // Mix-format stereo frames into `out` (interleaved). Returns frames written (may be less at
    // the natural end when not looping).
    usize Read(f32* out, usize frames);
    // Frames currently waiting in the ring.
    [[nodiscard]] usize GetBufferedFrames() const;
    // Peak bytes held for PCM (the ring); does not grow with the file.
    [[nodiscard]] static usize GetRingBytes() { return kRingFrames * 2 * sizeof(f32); }

private:
    struct Decoder; // mp3 or ogg + resampler

    bool FinishOpen(std::unique_ptr<Decoder> decoder);

    std::unique_ptr<Decoder> m_Decoder;
    std::vector<f32> m_Ring; // stereo interleaved, size kRingFrames * 2
    usize m_Read = 0;        // frame index into the ring
    usize m_Write = 0;
    usize m_Filled = 0; // frames buffered
    bool m_Loop = true;
    bool m_Ended = false;
};

// Two streams for gapless-ish crossfades: Play starts (or replaces) the current track; Crossfade
// brings a new one in while the old one fades out. MixAdd writes into an existing stereo buffer
// (the mixer's, before master/limit). Update/Pump fills both rings.
class MusicPlayer {
public:
    static constexpr f32 kDefaultFadeMs = 1000.0f;

    MusicPlayer();
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    // Starts `path` fading in; stops anything already playing (with the same fade).
    bool Play(const std::filesystem::path& path, f32 fadeMs = kDefaultFadeMs);
    // Fades the current track out and `path` in over `fadeMs`.
    bool Crossfade(const std::filesystem::path& path, f32 fadeMs = kDefaultFadeMs);
    void Stop(f32 fadeMs = kDefaultFadeMs);

    // From memory (tests). formatHint is "mp3" or "ogg".
    bool PlayMemory(std::span<const u8> data, std::string_view formatHint,
                    f32 fadeMs = kDefaultFadeMs);
    bool CrossfadeMemory(std::span<const u8> data, std::string_view formatHint,
                         f32 fadeMs = kDefaultFadeMs);

    void SetVolume(f32 volume); // 0..1, ramped
    [[nodiscard]] f32 GetVolume() const { return m_Volume; }
    [[nodiscard]] bool IsPlaying() const;

    void Update(); // Pump open streams
    // Adds this player's output * groupGain into `out` (already zeroed or holding other voices).
    void MixAdd(f32* out, usize frames, f32 groupGain);

    // Sample rate used for fade ramps (matches the mixer).
    void SetSampleRate(i32 rate) { m_SampleRate = rate; }

private:
    struct Slot {
        MusicStream Stream;
        f32 Gain = 0.0f;
        f32 GainTarget = 0.0f;
        f32 GainStep = 0.0f;
        bool Active = false;
    };

    bool StartSlot(usize index, const std::filesystem::path& path, f32 fadeMs, bool fromMemory,
                   std::span<const u8> data, std::string_view hint);
    void RampTo(Slot& slot, f32 target, f32 ms) const;

    Slot m_Slots[2]{};
    usize m_Current = 0; // the slot Play/Crossfade last started
    f32 m_Volume = 1.0f;
    f32 m_VolumeTarget = 1.0f;
    f32 m_VolumeStep = 0.0f;
    i32 m_SampleRate = 48000;
};

} // namespace Emerald

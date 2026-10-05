#include "Emerald/Audio/Music.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>
#include <dr_mp3.h>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include "Emerald/Audio/Sound.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

MusicStream::MusicStream() = default;
MusicStream::~MusicStream()
{
    Close();
}

MusicPlayer::MusicPlayer() = default;
MusicPlayer::~MusicPlayer() = default;

namespace {

template <typename... Args> void LogError(spdlog::format_string_t<Args...> format, Args&&... args)
{
    if (Log::Core())
        Log::Core()->error(format, std::forward<Args>(args)...);
}

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

} // namespace

// --- Decoder: mp3 or ogg -> mix-format stereo via SDL_AudioStream ----------------

struct MusicStream::Decoder {
    enum class Kind : u8 { Mp3, Ogg };

    Kind Type = Kind::Mp3;
    drmp3 Mp3{};
    stb_vorbis* Ogg = nullptr;
    SDL_AudioStream* Resampler = nullptr;
    std::vector<f32> Scratch; // source-format frames before resample
    i32 SrcRate = 0;
    i32 SrcChannels = 0;
    bool FromMemory = false;
    std::vector<u8> MemoryCopy; // when OpenMemory: own the bytes

    ~Decoder() { Close(); }

    void Close()
    {
        if (Resampler) {
            SDL_DestroyAudioStream(Resampler);
            Resampler = nullptr;
        }
        if (Type == Kind::Mp3)
            drmp3_uninit(&Mp3);
        if (Ogg) {
            stb_vorbis_close(Ogg);
            Ogg = nullptr;
        }
        Scratch.clear();
        MemoryCopy.clear();
    }

    bool InitResampler()
    {
        const SDL_AudioSpec src{SDL_AUDIO_F32, SrcChannels, SrcRate};
        Resampler = SDL_CreateAudioStream(&src, &kMixSpec);
        if (!Resampler) {
            LogError("MusicStream: SDL_CreateAudioStream failed: {}", SDL_GetError());
            return false;
        }
        Scratch.resize(static_cast<usize>(SrcChannels) * 2048);
        return true;
    }

    // Decode up to `wantMixFrames` mix-format stereo frames into `dst` (interleaved).
    usize Decode(f32* dst, usize wantMixFrames)
    {
        if (!Resampler || wantMixFrames == 0)
            return 0;
        usize written = 0;
        while (written < wantMixFrames) {
            // Pull anything already converted.
            const int avail = SDL_GetAudioStreamAvailable(Resampler);
            if (avail >= static_cast<int>(sizeof(f32) * 2)) {
                const usize frames = std::min(wantMixFrames - written,
                                              static_cast<usize>(avail) / (sizeof(f32) * 2));
                const int got = SDL_GetAudioStreamData(Resampler, dst + written * 2,
                                                       static_cast<int>(frames * sizeof(f32) * 2));
                if (got > 0)
                    written += static_cast<usize>(got) / (sizeof(f32) * 2);
                else
                    break;
                continue;
            }
            // Need more source PCM.
            const usize srcFrames = Scratch.size() / static_cast<usize>(SrcChannels);
            u64 got = 0;
            if (Type == Kind::Mp3) {
                got = drmp3_read_pcm_frames_f32(&Mp3, srcFrames, Scratch.data());
            } else {
                const int n = stb_vorbis_get_samples_float_interleaved(
                    Ogg, SrcChannels, Scratch.data(), static_cast<int>(srcFrames * SrcChannels));
                got = n > 0 ? static_cast<u64>(n) : 0;
            }
            if (got == 0)
                break; // natural end (caller may rewind)
            if (!SDL_PutAudioStreamData(Resampler, Scratch.data(),
                                        static_cast<int>(got * SrcChannels * sizeof(f32)))) {
                LogError("MusicStream: SDL_PutAudioStreamData failed: {}", SDL_GetError());
                break;
            }
        }
        return written;
    }

    bool Rewind()
    {
        if (Type == Kind::Mp3)
            return drmp3_seek_to_pcm_frame(&Mp3, 0) == DRMP3_TRUE;
        return stb_vorbis_seek_start(Ogg) != 0;
    }
};

bool MusicStream::FinishOpen(std::unique_ptr<Decoder> decoder)
{
    Close();
    if (!decoder || !decoder->InitResampler())
        return false;
    m_Decoder = std::move(decoder);
    m_Ring.assign(kRingFrames * 2, 0.0f);
    m_Read = m_Write = m_Filled = 0;
    m_Ended = false;
    Pump();
    return true;
}

bool MusicStream::Open(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto decoder = std::make_unique<Decoder>();
    const std::string utf8 = ToUtf8(path);
    if (ext == ".mp3") {
        decoder->Type = Decoder::Kind::Mp3;
        if (!drmp3_init_file(&decoder->Mp3, utf8.c_str(), nullptr)) {
            LogError("MusicStream: failed to open MP3 '{}'", path.string());
            return false;
        }
        decoder->SrcRate = static_cast<i32>(decoder->Mp3.sampleRate);
        decoder->SrcChannels = static_cast<i32>(decoder->Mp3.channels);
    } else if (ext == ".ogg") {
        decoder->Type = Decoder::Kind::Ogg;
        int error = 0;
        decoder->Ogg = stb_vorbis_open_filename(utf8.c_str(), &error, nullptr);
        if (!decoder->Ogg) {
            LogError("MusicStream: failed to open OGG '{}' (error {})", path.string(), error);
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(decoder->Ogg);
        decoder->SrcRate = info.sample_rate;
        decoder->SrcChannels = info.channels;
    } else {
        LogError("MusicStream: '{}' is not a .mp3 or .ogg file", path.string());
        return false;
    }
    return FinishOpen(std::move(decoder));
}

bool MusicStream::OpenMemory(std::span<const u8> data, std::string_view formatHint)
{
    auto decoder = std::make_unique<Decoder>();
    decoder->FromMemory = true;
    decoder->MemoryCopy.assign(data.begin(), data.end());
    if (formatHint == "mp3" || formatHint == ".mp3") {
        decoder->Type = Decoder::Kind::Mp3;
        if (!drmp3_init_memory(&decoder->Mp3, decoder->MemoryCopy.data(),
                               decoder->MemoryCopy.size(), nullptr)) {
            LogError("MusicStream: failed to open MP3 from memory");
            return false;
        }
        decoder->SrcRate = static_cast<i32>(decoder->Mp3.sampleRate);
        decoder->SrcChannels = static_cast<i32>(decoder->Mp3.channels);
    } else if (formatHint == "ogg" || formatHint == ".ogg") {
        decoder->Type = Decoder::Kind::Ogg;
        int error = 0;
        decoder->Ogg =
            stb_vorbis_open_memory(decoder->MemoryCopy.data(),
                                   static_cast<int>(decoder->MemoryCopy.size()), &error, nullptr);
        if (!decoder->Ogg) {
            LogError("MusicStream: failed to open OGG from memory (error {})", error);
            return false;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(decoder->Ogg);
        decoder->SrcRate = info.sample_rate;
        decoder->SrcChannels = info.channels;
    } else {
        LogError("MusicStream: unknown format '{}'", formatHint);
        return false;
    }
    return FinishOpen(std::move(decoder));
}

void MusicStream::Close()
{
    m_Decoder.reset();
    m_Ring.clear();
    m_Read = m_Write = m_Filled = 0;
    m_Ended = false;
}

usize MusicStream::GetBufferedFrames() const
{
    return m_Filled;
}

void MusicStream::Pump()
{
    if (!m_Decoder || m_Ended)
        return;
    while (m_Filled < kRingFrames) {
        const usize space = kRingFrames - m_Filled;
        // Decode into a small stack buffer then copy into the ring (may wrap).
        f32 temp[2048 * 2];
        const usize want = std::min(space, static_cast<usize>(2048));
        usize got = m_Decoder->Decode(temp, want);
        if (got == 0) {
            if (m_Loop && m_Decoder->Rewind()) {
                SDL_ClearAudioStream(m_Decoder->Resampler);
                continue;
            }
            m_Ended = true;
            break;
        }
        for (usize i = 0; i < got; ++i) {
            m_Ring[m_Write * 2] = temp[i * 2];
            m_Ring[m_Write * 2 + 1] = temp[i * 2 + 1];
            m_Write = (m_Write + 1) % kRingFrames;
        }
        m_Filled += got;
    }
}

usize MusicStream::Read(f32* out, usize frames)
{
    const usize take = std::min(frames, m_Filled);
    for (usize i = 0; i < take; ++i) {
        out[i * 2] = m_Ring[m_Read * 2];
        out[i * 2 + 1] = m_Ring[m_Read * 2 + 1];
        m_Read = (m_Read + 1) % kRingFrames;
    }
    m_Filled -= take;
    for (usize i = take; i < frames; ++i) {
        out[i * 2] = 0.0f;
        out[i * 2 + 1] = 0.0f;
    }
    return take;
}

// --- MusicPlayer ----------------------------------------------------------------

void MusicPlayer::RampTo(Slot& slot, f32 target, f32 ms) const
{
    slot.GainTarget = target;
    const f32 samples = ms * static_cast<f32>(m_SampleRate) / 1000.0f;
    if (samples < 1.0f) {
        slot.Gain = target;
        slot.GainStep = 0.0f;
    } else {
        slot.GainStep = std::abs(target - slot.Gain) / samples;
    }
}

bool MusicPlayer::StartSlot(usize index, const std::filesystem::path& path, f32 fadeMs,
                            bool fromMemory, std::span<const u8> data, std::string_view hint)
{
    Slot& slot = m_Slots[index];
    slot.Stream.Close();
    const bool ok = fromMemory ? slot.Stream.OpenMemory(data, hint) : slot.Stream.Open(path);
    if (!ok) {
        slot.Active = false;
        return false;
    }
    slot.Stream.SetLoop(true);
    slot.Active = true;
    slot.Gain = 0.0f;
    RampTo(slot, 1.0f, fadeMs);
    m_Current = index;
    return true;
}

bool MusicPlayer::Play(const std::filesystem::path& path, f32 fadeMs)
{
    Stop(fadeMs);
    const usize slot = m_Current == 0 ? 1 : 0; // prefer the free one
    return StartSlot(slot, path, fadeMs, false, {}, {});
}

bool MusicPlayer::Crossfade(const std::filesystem::path& path, f32 fadeMs)
{
    if (m_Slots[m_Current].Active)
        RampTo(m_Slots[m_Current], 0.0f, fadeMs);
    const usize next = 1 - m_Current;
    return StartSlot(next, path, fadeMs, false, {}, {});
}

bool MusicPlayer::PlayMemory(std::span<const u8> data, std::string_view formatHint, f32 fadeMs)
{
    Stop(fadeMs);
    const usize slot = m_Current == 0 ? 1 : 0;
    return StartSlot(slot, {}, fadeMs, true, data, formatHint);
}

bool MusicPlayer::CrossfadeMemory(std::span<const u8> data, std::string_view formatHint, f32 fadeMs)
{
    if (m_Slots[m_Current].Active)
        RampTo(m_Slots[m_Current], 0.0f, fadeMs);
    const usize next = 1 - m_Current;
    return StartSlot(next, {}, fadeMs, true, data, formatHint);
}

void MusicPlayer::Stop(f32 fadeMs)
{
    for (Slot& slot : m_Slots) {
        if (slot.Active)
            RampTo(slot, 0.0f, fadeMs);
    }
}

void MusicPlayer::SetVolume(f32 volume)
{
    m_VolumeTarget = std::max(volume, 0.0f);
    const f32 samples = 20.0f * static_cast<f32>(m_SampleRate) / 1000.0f;
    m_VolumeStep = samples < 1.0f ? 0.0f : std::abs(m_VolumeTarget - m_Volume) / samples;
    if (samples < 1.0f)
        m_Volume = m_VolumeTarget;
}

bool MusicPlayer::IsPlaying() const
{
    return std::any_of(std::begin(m_Slots), std::end(m_Slots),
                       [](const Slot& s) { return s.Active; });
}

void MusicPlayer::Update()
{
    for (Slot& slot : m_Slots) {
        if (slot.Active)
            slot.Stream.Pump();
    }
}

void MusicPlayer::MixAdd(f32* out, usize frames, f32 groupGain)
{
    for (usize offset = 0; offset < frames;) {
        const usize n = std::min(frames - offset, static_cast<usize>(512));
        // Snapshot each active slot's PCM for this block.
        f32 slotBuf[2][512 * 2]{};
        bool active[2] = {m_Slots[0].Active, m_Slots[1].Active};
        for (usize s = 0; s < 2; ++s) {
            if (active[s])
                m_Slots[s].Stream.Read(slotBuf[s], n);
        }
        for (usize i = 0; i < n; ++i) {
            auto step = [](f32& value, f32 target, f32 step) {
                if (value < target)
                    value = std::min(value + step, target);
                else if (value > target)
                    value = std::max(value - step, target);
            };
            step(m_Volume, m_VolumeTarget, m_VolumeStep);
            for (usize s = 0; s < 2; ++s) {
                if (!active[s])
                    continue;
                Slot& slot = m_Slots[s];
                step(slot.Gain, slot.GainTarget, slot.GainStep);
                const f32 g = slot.Gain * m_Volume * groupGain;
                out[(offset + i) * 2] += slotBuf[s][i * 2] * g;
                out[(offset + i) * 2 + 1] += slotBuf[s][i * 2 + 1] * g;
            }
        }
        for (usize s = 0; s < 2; ++s) {
            if (m_Slots[s].Active && m_Slots[s].GainTarget <= 0.0f && m_Slots[s].Gain <= 0.0f) {
                m_Slots[s].Stream.Close();
                m_Slots[s].Active = false;
            }
        }
        offset += n;
    }
}

} // namespace Emerald

#pragma once

#include <span>
#include <vector>

#include "Emerald/Audio/Sound.h"
#include "Emerald/Core/Defines.h"

// Tiny procedural synth for retro sound effects. Build a mono buffer, shape it, turn it into a
// Sound:
//
//   using namespace Emerald::Synth;
//   std::vector<f32> zap = Generate({.Shape = Wave::Square, .Seconds = 0.15f,
//                                    .StartHz = 1200.0f, .EndHz = 300.0f});   // falling pitch
//   ApplyDecay(zap, 0.05f);                                                  // quick fade
//   Emerald::Sound sound = ToSound(zap);
//
// Everything runs at the mix rate (kMixSpec.freq), mono until ToSound.
namespace Emerald::Synth {

enum class Wave : u8 {
    Sine,
    Square, // pulse wave; Tone::Duty sets the width (0.5 = square)
    Triangle,
    Saw,
    Noise, // white noise, held for one "cycle": the frequency sets how bright it sounds
};

struct Tone {
    Wave Shape = Wave::Square;
    f32 Seconds = 0.2f;
    f32 StartHz = 440.0f;
    f32 EndHz = 0.0f;  // pitch sweep target (exponential); 0 = no sweep
    f32 Duty = 0.5f;   // Square only
    f32 Volume = 0.5f; // peak level, 0..1
};

// Attack/decay/release in seconds, sustain as a level. The note is held until Release seconds
// before the end of the buffer.
struct Adsr {
    f32 Attack = 0.005f;
    f32 Decay = 0.1f;
    f32 Sustain = 0.6f;
    f32 Release = 0.1f;
};

[[nodiscard]] std::vector<f32> Generate(const Tone& tone, u32 seed = 1);
// Multiplies by an exponential decay: the level halves every `halfLife` seconds.
void ApplyDecay(std::span<f32> samples, f32 halfLife);
void ApplyAdsr(std::span<f32> samples, const Adsr& envelope);
// One-pole lowpass: removes frequencies above about `cutoffHz` (softer, duller sound).
void LowPass(std::span<f32> samples, f32 cutoffHz);
// Adds `source * gain` onto `target` (layering; extra source samples are ignored).
void MixInto(std::span<f32> target, std::span<const f32> source, f32 gain = 1.0f);
// Silence of the given length (e.g. to build a longer sound from pieces with MixInto).
[[nodiscard]] std::vector<f32> Silence(f32 seconds);
// Mono -> stereo Sound in the mix format, clamped to -1..1.
[[nodiscard]] Sound ToSound(std::span<const f32> mono);

} // namespace Emerald::Synth

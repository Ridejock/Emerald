#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Emerald/Core/Defines.h"
#include "Emerald/Particles/ParticleSystem.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

// A particle effect file: a ParticleEmitterConfig plus how it is usually played (a burst size and
// the blend mode). Effects are JSON files loaded through the asset manager, so editing one on disk
// (or in the ParticleEditor, Editor/ParticleEditor.h) updates the game while it runs:
//
//   AssetHandle<ParticleEffect> sparks = GetAssets().Load<ParticleEffect>("particles/sparks.json");
//   particles.Emit(sparks->GetConfig(), position, sparks->Burst);
//   particles.EmitContinuous(smoke->GetConfig(), m_Chimney, top, dt);
//   particles.Draw(r, {.Blend = sparks->Blend});
//
// The file (every key optional; angles in degrees, colors as [r, g, b, a] or [r, g, b]):
//
//   { "startColor": [1, 0.8, 0.4, 1], "endColor": [1, 0.3, 0, 0], "shape": "circle",
//     "radius": 4, "lineHalfExtent": [0, 0], "speed": [80, 200], "angle": [0, 360],
//     "lifetime": [0.3, 0.6], "drag": 1.5, "gravity": [0, 120], "startSize": 3, "endSize": 0,
//     "rate": 0, "burst": 40, "blend": "additive" }
//
// The config lives on the heap: the assets keep objects in a std::variant, and Vec4's 16-byte
// alignment inside one makes MSVC warn (C4324). Copies are deep, and assigning one effect to
// another keeps GetConfig()'s address, so a hot reload doesn't leave references dangling.
class ParticleEffect {
public:
    ParticleEffect() : m_Config(std::make_unique<ParticleEmitterConfig>()) {}
    explicit ParticleEffect(const ParticleEmitterConfig& config, u32 burst = 0,
                            BlendMode blend = BlendMode::Additive)
        : Burst(burst), Blend(blend), m_Config(std::make_unique<ParticleEmitterConfig>(config))
    {
    }
    ParticleEffect(const ParticleEffect& other)
        : Burst(other.Burst), Blend(other.Blend), Revision(other.Revision),
          m_Config(std::make_unique<ParticleEmitterConfig>(*other.m_Config))
    {
    }
    ParticleEffect& operator=(const ParticleEffect& other)
    {
        *m_Config = *other.m_Config; // in place: the address stays
        Burst = other.Burst;
        Blend = other.Blend;
        Revision = other.Revision;
        return *this;
    }
    ~ParticleEffect() = default;

    [[nodiscard]] ParticleEmitterConfig& GetConfig() { return *m_Config; }
    [[nodiscard]] const ParticleEmitterConfig& GetConfig() const { return *m_Config; }

    u32 Burst = 0; // particles per one-shot Emit
    BlendMode Blend = BlendMode::Additive;
    // A new number every time a file is parsed, so a copy can tell the asset was reloaded.
    u32 Revision = 0;

private:
    std::unique_ptr<ParticleEmitterConfig> m_Config;
};

// JSON in and out (the format above). Parse returns nothing for broken JSON or values of the
// wrong kind (logged, naming `source`); unknown keys are ignored, missing ones keep the defaults.
[[nodiscard]] std::optional<ParticleEffect> ParseParticleEffect(std::string_view json,
                                                                std::string_view source = "(text)");
[[nodiscard]] std::optional<ParticleEffect> LoadParticleEffect(const std::filesystem::path& file);
// Every field, one per line, numbers as short as they round-trip ("0.1", not "0.100000001").
[[nodiscard]] std::string ParticleEffectToJson(const ParticleEffect& effect);
bool SaveParticleEffect(const std::filesystem::path& file, const ParticleEffect& effect); // logs

} // namespace Emerald

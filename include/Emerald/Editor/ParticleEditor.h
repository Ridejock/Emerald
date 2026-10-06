#pragma once

// The particle editor (ImGui builds): a window that edits every field of a ParticleEffect
// (ParticleEffect.h), saves it to its JSON file and reverts to what is on disk. The caller owns
// the effect and the preview, so the same window can tune any effect:
//
//   // OnImGui:
//   const ParticleEditorResult edit = m_Editor.Show(m_Working, m_File);
//   if (edit.Burst)
//       m_Particles.Emit(m_Working.GetConfig(), m_Center, m_Working.Burst);
//
// Saving writes the file, so the asset manager's hot reload passes the change on to every
// handle of that effect. Without ImGui, Show does nothing and returns an empty result.

#include <filesystem>
#include <string>

#include "Emerald/Core/Defines.h"
#include "Emerald/Particles/ParticleEffect.h"

namespace Emerald {

struct ParticleEditorResult {
    bool Changed = false; // a field changed this frame (or Revert / Defaults)
    bool Burst = false;   // the Burst button
    bool Clear = false;   // the Clear button (remove the preview's particles)
};

#if EMERALD_WITH_IMGUI

class ParticleEditor {
public:
    ParticleEditorResult Show(ParticleEffect& effect, const std::filesystem::path& file,
                              const char* title = "Particle editor");

    // True when the effect differs from the file as last saved or loaded here.
    [[nodiscard]] bool HasUnsavedChanges(const ParticleEffect& effect) const;

private:
    std::filesystem::path m_File; // what m_Saved belongs to
    std::string m_Saved;          // that file's contents as JSON (compared to spot changes)
    u32 m_Revision = 0;           // the effect's Revision when m_Saved was taken
    std::string m_Status;
};

#else

class ParticleEditor {
public:
    ParticleEditorResult Show(ParticleEffect&, const std::filesystem::path&, const char* = nullptr)
    {
        return {};
    }
    [[nodiscard]] bool HasUnsavedChanges(const ParticleEffect&) const { return false; }
};

#endif

} // namespace Emerald

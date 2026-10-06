// The particle editor window (ImGui builds only).

#include "Emerald/Editor/ParticleEditor.h"

#include <imgui.h>

#include "EditorWindow.h"
#include "Emerald/Core/Log.h"

namespace Emerald {

namespace {

// [min, max] as one row of two drag fields.
bool Range(const char* label, FloatRange& range, f32 speed, f32 min = 0.0f, f32 max = 0.0f)
{
    return ImGui::DragFloatRange2(label, &range.Min, &range.Max, speed, min, max, "%.2f", "%.2f",
                                  max > min ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
}

} // namespace

bool ParticleEditor::HasUnsavedChanges(const ParticleEffect& effect) const
{
    return ParticleEffectToJson(effect) != m_Saved;
}

ParticleEditorResult ParticleEditor::Show(ParticleEffect& effect, const std::filesystem::path& file,
                                          const char* title)
{
    // A new file, or the effect was reloaded from disk: that is the saved state now.
    if (file != m_File || effect.Revision != m_Revision) {
        m_File = file;
        m_Revision = effect.Revision;
        m_Saved = ParticleEffectToJson(effect);
        m_Status.clear();
    }

    ParticleEditorResult result;
    PlaceNextEditorWindow(ImVec2(380.0f, 560.0f), 20.0f);
    if (!ImGui::Begin(title)) {
        ImGui::End();
        return result;
    }
    const bool unsaved = HasUnsavedChanges(effect);
    ImGui::Text("%s%s", file.filename().string().c_str(), unsaved ? " *" : "");
    if (ImGui::Button("Save")) {
        if (SaveParticleEffect(file, effect)) {
            m_Saved = ParticleEffectToJson(effect);
            m_Status = "Saved";
        } else {
            m_Status = "Saving failed (see the log)";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        if (std::optional<ParticleEffect> loaded = LoadParticleEffect(file)) {
            effect = *loaded;
            m_Revision = effect.Revision;
            m_Saved = ParticleEffectToJson(effect);
            m_Status = "Reverted to the file";
            result.Changed = true;
        } else {
            m_Status = "Couldn't read the file (see the log)";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        effect.GetConfig() = {};
        effect.Burst = 0;
        effect.Blend = BlendMode::Additive;
        result.Changed = true;
    }
    ImGui::SameLine();
    result.Burst = ImGui::Button("Burst");
    ImGui::SameLine();
    result.Clear = ImGui::Button("Clear");
    if (!m_Status.empty())
        ImGui::TextDisabled("%s", m_Status.c_str());
    ImGui::Separator();

    ParticleEmitterConfig& c = effect.GetConfig();
    bool changed = false;
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 10.0f); // room for the names
    ImGui::SeparatorText("Color and size over life");
    changed |= ImGui::ColorEdit4("Start color", &c.StartColor.x);
    changed |= ImGui::ColorEdit4("End color", &c.EndColor.x);
    changed |= ImGui::DragFloat("Start size", &c.StartSize, 0.05f, 0.0f, 256.0f);
    changed |= ImGui::DragFloat("End size", &c.EndSize, 0.05f, 0.0f, 256.0f);

    ImGui::SeparatorText("Spawn");
    static constexpr const char* kShapes[] = {"Point", "Circle", "Line"};
    i32 shape = static_cast<i32>(c.Shape);
    if (ImGui::Combo("Shape", &shape, kShapes, 3)) {
        c.Shape = static_cast<EmitterShape>(shape);
        changed = true;
    }
    // Only the shape's own size is shown (the others are kept, unused).
    if (c.Shape == EmitterShape::Circle)
        changed |= ImGui::DragFloat("Radius", &c.Radius, 0.2f, 0.0f, 1000.0f);
    if (c.Shape == EmitterShape::Line)
        changed |= ImGui::DragFloat2("Line half extent", &c.LineHalfExtent.x, 0.2f);
    changed |= Range("Speed", c.Speed, 1.0f);
    // Degrees on screen, radians in the config.
    FloatRange degrees{ToDegrees(c.Angle.Min), ToDegrees(c.Angle.Max)};
    if (Range("Angle (degrees)", degrees, 1.0f, -360.0f, 360.0f)) {
        c.Angle = {ToRadians(degrees.Min), ToRadians(degrees.Max)};
        changed = true;
    }
    changed |= Range("Lifetime (s)", c.Lifetime, 0.01f, 0.0f, 60.0f);

    ImGui::SeparatorText("Motion");
    changed |= ImGui::DragFloat("Drag", &c.Drag, 0.01f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat2("Gravity", &c.Gravity.x, 1.0f);

    ImGui::SeparatorText("Playing");
    changed |= ImGui::DragFloat("Rate (per second)", &c.Rate, 0.5f, 0.0f, 10000.0f);
    i32 burst = static_cast<i32>(effect.Burst);
    if (ImGui::DragInt("Burst", &burst, 0.5f, 0, 10000)) {
        effect.Burst = static_cast<u32>(burst < 0 ? 0 : burst);
        changed = true;
    }
    bool additive = effect.Blend == BlendMode::Additive;
    if (ImGui::Checkbox("Additive (glow)", &additive)) {
        effect.Blend = additive ? BlendMode::Additive : BlendMode::Alpha;
        changed = true;
    }
    ImGui::PopItemWidth();
    ImGui::End();
    result.Changed |= changed;
    return result;
}

} // namespace Emerald

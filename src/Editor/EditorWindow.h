#pragma once

// Where the editor windows first appear (private to src/Editor, ImGui builds only).

#include <imgui.h>

#include "Emerald/Core/Defines.h"

namespace Emerald {

// Top-right, `fromRight` pixels in from the right edge (the apps' own windows sit at the left).
// The Tweaks window and the particle editor take the corner, the entity inspector starts next to
// it. Only where they first appear: the user moves them.
inline void PlaceNextEditorWindow(ImVec2 size, f32 fromRight)
{
    const ImGuiViewport* view = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(view->WorkPos.x + view->WorkSize.x - fromRight, view->WorkPos.y + 20.0f),
        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
}

} // namespace Emerald

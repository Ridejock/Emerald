// The entity inspector and the Edit widgets (ImGui builds only).

#include "Emerald/Editor/EntityInspector.h"

#include <cmath>
#include <cstdio>
#include <limits>

#include <imgui.h>

#include "EditorWindow.h"
#include "Emerald/Entity/Components.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

// --- Edit widgets ---

namespace Edit {

bool Float(const char* label, f32& value, f32 min, f32 max)
{
    if (max > min)
        return ImGui::SliderFloat(label, &value, min, max, "%.3f");
    return ImGui::DragFloat(label, &value, 0.01f + std::abs(value) * 0.005f, 0.0f, 0.0f, "%.3f");
}

bool Int(const char* label, i32& value, i32 min, i32 max)
{
    if (max > min)
        return ImGui::SliderInt(label, &value, min, max);
    return ImGui::DragInt(label, &value);
}

bool Bool(const char* label, bool& value)
{
    return ImGui::Checkbox(label, &value);
}

bool Vector(const char* label, Vec2& value, f32 speed)
{
    return ImGui::DragFloat2(label, &value.x, speed, 0.0f, 0.0f, "%.2f");
}

bool Color(const char* label, Vec4& value)
{
    return ImGui::ColorEdit4(label, &value.x);
}

bool Angle(const char* label, f32& radians)
{
    return ImGui::SliderAngle(label, &radians, -360.0f, 360.0f);
}

bool Choice(const char* label, i32& index, std::span<const char* const> items)
{
    return ImGui::Combo(label, &index, items.data(), static_cast<i32>(items.size()));
}

void Text(std::string_view text)
{
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

} // namespace Edit

// --- The engine's components ---

EntityInspector::EntityInspector()
{
    Add<Transform>("Transform", [](Transform& t) {
        Edit::Vector("Position", t.Position);
        Edit::Angle("Rotation", t.Rotation);
        Edit::Vector("Scale", t.Scale, 0.01f);
    });
    Add<Velocity>("Velocity", [](Velocity& v) {
        Edit::Vector("Linear", v.Linear);
        Edit::Float("Angular (rad/s)", v.Angular);
    });
    Add<SpriteRenderer>("SpriteRenderer", [](SpriteRenderer& s) {
        char size[64];
        std::snprintf(size, sizeof(size), "Image: %.0f x %.0f region%s",
                      static_cast<f64>(s.Image.Region.Size.x),
                      static_cast<f64>(s.Image.Region.Size.y),
                      s.Image.Source ? "" : " (no texture)");
        Edit::Text(size);
        Edit::Int("Layer", s.Layer);
        Edit::Color("Tint", s.Options.Tint);
        Edit::Vector("Scale", s.Options.Scale, 0.01f);
        Edit::Angle("Rotation", s.Options.Rotation);
        Edit::Vector("Origin", s.Options.Origin, 0.01f);
        Edit::Bool("Flip X", s.Options.FlipX);
        Edit::Bool("Flip Y", s.Options.FlipY);
        Edit::Bool("Pixel snap", s.Options.PixelSnap);
    });
    Add<Animator>("Animator", [](Animator& a) {
        const Animation* animation = a.GetAnimation();
        char line[128];
        std::snprintf(line, sizeof(line), "%s: frame %u of %zu, %u loops",
                      animation ? animation->Name.c_str() : "(none)", a.GetFrameIndex() + 1,
                      animation ? animation->Frames.size() : 0, a.GetLoopCount());
        Edit::Text(line);
        f32 speed = a.GetSpeed();
        if (Edit::Float("Speed", speed, 0.0f, 4.0f))
            a.SetSpeed(speed);
        bool playing = a.IsPlaying();
        if (Edit::Bool("Playing", playing)) {
            if (playing)
                a.Resume();
            else
                a.Stop();
        }
    });
    Add<Collider>("Collider", [](Collider& c) {
        static constexpr const char* kShapes[] = {"Circle", "Box"};
        i32 shape = static_cast<i32>(c.Shape);
        if (Edit::Choice("Shape", shape, kShapes))
            c.Shape = static_cast<ColliderShape>(shape);
        if (c.Shape == ColliderShape::Circle)
            Edit::Float("Radius", c.Radius);
        else
            Edit::Vector("Half size", c.HalfSize);
        Edit::Vector("Offset", c.Offset);
        char masks[64];
        std::snprintf(masks, sizeof(masks), "Layer 0x%08X  Mask 0x%08X", c.Layer, c.Mask);
        Edit::Text(masks);
    });
}

// --- Selection ---

Entity EntityInspector::SelectAt(World& world, Vec2 point, f32 radius)
{
    Entity best;
    f32 bestDistance = radius * radius;
    world.Each<Transform>([&](Entity e, const Transform& t) {
        const f32 d = LengthSquared(t.Position - point);
        if (d <= bestDistance) {
            bestDistance = d;
            best = e;
        }
    });
    m_Selected = best;
    return best;
}

void EntityInspector::DrawSelection(Renderer2D& r) const
{
    const Entity e = GetSelected();
    const Transform* t = e ? e.TryGet<Transform>() : nullptr;
    if (!t)
        return;
    Aabb box = Aabb::FromCenter(t->Position, Vec2(8.0f));
    if (const Collider* c = e.TryGet<Collider>())
        box = c->GetBounds(t->Position);
    const Vec2 pad(2.0f);
    r.DrawRect(box.Min - pad, box.Max - box.Min + pad * 2.0f, {1.0f, 0.85f, 0.2f, 1.0f});
}

std::vector<std::string_view> EntityInspector::GetComponentNames(Entity entity) const
{
    std::vector<std::string_view> names;
    if (!entity)
        return names;
    for (const Component& c : m_Components)
        if (c.Has(entity))
            names.push_back(c.Name);
    return names;
}

bool EntityInspector::Inspect(Entity entity, std::string_view component) const
{
    if (!entity)
        return false;
    for (const Component& c : m_Components) {
        if (c.Name == component && c.Has(entity)) {
            if (c.Draw)
                c.Draw(entity);
            return true;
        }
    }
    return false;
}

// --- The window ---

namespace {
// The entity's slot number, without the version bits ToU32 includes (#12, not #2097164).
u32 Index(Entity e)
{
    return static_cast<u32>(entt::to_entity(e.GetId()));
}
} // namespace

void EntityInspector::Show(World& world, const char* title)
{
    PlaceNextEditorWindow(ImVec2(360.0f, 520.0f), 410.0f);
    if (!ImGui::Begin(title)) {
        ImGui::End();
        return;
    }
    ShowList(world);
    ImGui::Separator();
    ShowSelected();
    ImGui::End();
}

void EntityInspector::ShowList(World& world)
{
    // The filter: every entity, or those with one registered component.
    std::vector<const char*> filters{"(all)"};
    for (const Component& c : m_Components)
        filters.push_back(c.Name.c_str());
    ImGui::Combo("With", &m_Filter, filters.data(), static_cast<i32>(filters.size()));
    const Component* only = m_Filter > 0 && m_Filter <= static_cast<i32>(m_Components.size())
                                ? &m_Components[static_cast<usize>(m_Filter - 1)]
                                : nullptr;

    m_Rows.clear();
    for (const entt::entity id : world.GetRegistry().view<entt::entity>()) {
        const Entity e = world.Wrap(id);
        if (e.IsValid() && (!only || only->Has(e)))
            m_Rows.push_back(id);
    }
    ImGui::Text("%zu entities", m_Rows.size());

    // Only the visible rows are built (the swarm has thousands).
    ImGui::BeginChild("list", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<i32>(m_Rows.size()));
    while (clipper.Step()) {
        for (i32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Entity e = world.Wrap(m_Rows[static_cast<usize>(row)]);
            std::string label = "#" + std::to_string(Index(e)) + " ";
            for (const std::string_view name : GetComponentNames(e))
                label += " " + std::string(name);
            if (ImGui::Selectable(label.c_str(), e == m_Selected))
                m_Selected = e;
        }
    }
    ImGui::EndChild();
}

void EntityInspector::ShowSelected()
{
    const Entity e = GetSelected();
    if (!e) {
        ImGui::TextDisabled(m_Selected.GetWorld() ? "The selected entity is gone"
                                                  : "Select an entity in the list");
        return;
    }
    ImGui::Text("Entity #%u", Index(e));
    ImGui::SameLine();
    if (ImGui::SmallButton("Destroy")) {
        e.Destroy();
        return;
    }
    for (const Component& c : m_Components) {
        if (!c.Has(e))
            continue;
        ImGui::PushID(c.Name.c_str());
        const bool open = ImGui::CollapsingHeader(
            c.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
        const bool remove = ImGui::SmallButton("Remove");
        if (open && c.Draw && !remove)
            c.Draw(e);
        if (remove)
            c.Remove(e);
        ImGui::PopID();
    }
}

} // namespace Emerald

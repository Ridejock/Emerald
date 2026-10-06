#pragma once

// The entity inspector (ImGui builds): lists a World's entities, and shows and edits the
// components of the selected one while the game runs. The engine's components (Transform,
// Velocity, SpriteRenderer, Animator, Collider) are registered already; a game adds its own with
// a small function built from the Edit widgets below, so it never needs imgui.h:
//
//   struct Health { i32 Points = 10; f32 Regen = 0.5f; };
//
//   m_Inspector.Add<Health>("Health", [](Health& h) {
//       Emerald::Edit::Int("Points", h.Points, 0, 100);
//       Emerald::Edit::Float("Regen", h.Regen, 0.0f, 5.0f);
//   });
//   m_Inspector.AddTag<Frozen>("Frozen");                   // an empty struct
//   // OnImGui:     m_Inspector.Show(m_World);
//   // OnRender2D:  m_Inspector.DrawSelection(r);            // an outline around the selection
//   // On a click:  m_Inspector.SelectAt(m_World, worldPoint); // the nearest entity
//
// Without ImGui every call compiles to nothing (the registration lambdas are never called).

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Entity/World.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {

class Renderer2D;

// Edit widgets for inspectors and other debug panels. Each returns true when the value was
// changed this frame. A range of 0..0 means "no limits" (a drag field instead of a slider).
namespace Edit {
#if EMERALD_WITH_IMGUI
bool Float(const char* label, f32& value, f32 min = 0.0f, f32 max = 0.0f);
bool Int(const char* label, i32& value, i32 min = 0, i32 max = 0);
bool Bool(const char* label, bool& value);
bool Vector(const char* label, Vec2& value, f32 speed = 1.0f);
bool Color(const char* label, Vec4& value);
bool Angle(const char* label, f32& radians); // shown in degrees
bool Choice(const char* label, i32& index, std::span<const char* const> items);
void Text(std::string_view text); // a read-only line
#else
inline bool Float(const char*, f32&, f32 = 0.0f, f32 = 0.0f)
{
    return false;
}
inline bool Int(const char*, i32&, i32 = 0, i32 = 0)
{
    return false;
}
inline bool Bool(const char*, bool&)
{
    return false;
}
inline bool Vector(const char*, Vec2&, f32 = 1.0f)
{
    return false;
}
inline bool Color(const char*, Vec4&)
{
    return false;
}
inline bool Angle(const char*, f32&)
{
    return false;
}
inline bool Choice(const char*, i32&, std::span<const char* const>)
{
    return false;
}
inline void Text(std::string_view)
{
}
#endif
} // namespace Edit

#if EMERALD_WITH_IMGUI

class EntityInspector {
public:
    EntityInspector(); // with the engine's components

    // Registers a component's inspector, shown for entities that have T: draw(T&) edits it.
    template <typename T, typename Fn> void Add(std::string name, Fn draw)
    {
        static_assert(!std::is_empty_v<T>, "EntityInspector::Add: use AddTag for empty structs");
        AddComponent<T>(std::move(name));
        m_Components.back().Draw = [draw = std::move(draw)](Entity e) { draw(e.Get<T>()); };
    }
    // An empty "tag" struct: listed (and removable) with nothing to edit.
    template <typename T> void AddTag(std::string name) { AddComponent<T>(std::move(name)); }

    // The "Entities" window: the list (filterable by component), then the selected entity's
    // components, each in its own section with a Remove button.
    void Show(World& world, const char* title = "Entities");
    // Outlines the selection (its collider, or a small box) in the current Renderer2D batch.
    void DrawSelection(Renderer2D& r) const;

    void Select(Entity entity) { m_Selected = entity; }
    [[nodiscard]] Entity GetSelected() const
    {
        return m_Selected.IsValid() ? m_Selected : Entity{};
    }
    // Selects the entity whose Transform is nearest to `point` within `radius` (or nothing).
    Entity SelectAt(World& world, Vec2 point, f32 radius = 16.0f);

    // The registered components `entity` has, in registration order (the window's sections).
    [[nodiscard]] std::vector<std::string_view> GetComponentNames(Entity entity) const;
    // Runs one component's inspector (the window does this in its section). False if the name
    // is unknown or the entity doesn't have it.
    bool Inspect(Entity entity, std::string_view component) const;

private:
    struct Component {
        std::string Name;
        std::function<bool(Entity)> Has;
        std::function<void(Entity)> Draw; // empty for tags
        std::function<void(Entity)> Remove;
    };

    template <typename T> void AddComponent(std::string name)
    {
        m_Components.push_back({.Name = std::move(name),
                                .Has = [](Entity e) { return e.Has<T>(); },
                                .Draw = {},
                                .Remove = [](Entity e) { e.Remove<T>(); }});
    }
    void ShowList(World& world);
    void ShowSelected();

    std::vector<Component> m_Components;
    Entity m_Selected;
    i32 m_Filter = 0;                 // 0: every entity; i: those with m_Components[i - 1]
    std::vector<entt::entity> m_Rows; // this frame's list (reused)
};

#else // without ImGui: nothing

class EntityInspector {
public:
    template <typename T, typename Fn> void Add(std::string_view, Fn&&) {}
    template <typename T> void AddTag(std::string_view) {}
    void Show(World&, const char* = nullptr) {}
    void DrawSelection(Renderer2D&) const {}
    void Select(Entity) {}
    [[nodiscard]] Entity GetSelected() const { return {}; }
    Entity SelectAt(World&, Vec2, f32 = 16.0f) { return {}; }
    [[nodiscard]] std::vector<std::string_view> GetComponentNames(Entity) const { return {}; }
    bool Inspect(Entity, std::string_view) const { return false; }
};

#endif

} // namespace Emerald

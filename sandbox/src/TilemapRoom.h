#pragma once

// Tilemap demo (Emerald/Tilemap): the sample room from tools/tilemaps/make_tilemaps.py, loaded
// through the asset manager (edit sandbox/assets/tilemaps/room.tmj or dungeon.tsj in Tiled while
// the debug sandbox runs and it reloads). The hero walks with tile collision, the camera follows
// inside the map, the hero is drawn between the layers below and above the "Objects" layer, and
// the ImGui panel toggles layers, the collision overlay and object outlines.
//
// Benchmark: --map <file.tmj> loads another map (e.g. the 500 x 500 one from the script's
// --bench option), --pan makes the camera sweep across it on its own, and --stats logs frame
// rate, tiles drawn and the time spent in Tilemap::DrawLayer every two seconds.

#include <cmath>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

#include <SDL3/SDL.h>

#include <Emerald/Emerald.h>

#if EMERALD_WITH_IMGUI
#include <imgui.h>
#endif

class TilemapRoom {
public:
    using Vec2 = Emerald::Vec2;
    using Vec4 = Emerald::Vec4;

    struct Options {
        std::filesystem::path Map = "tilemaps/room.tmj"; // relative to the asset root
        bool Pan = false;   // benchmark: the camera sweeps the map by itself
        bool Stats = false; // log frame rate and draw cost every two seconds
        f32 Zoom = 3.0f;
    };

    void Load(Emerald::Assets& assets, const Options& options)
    {
        m_Options = options;
        m_Map = assets.Load<Emerald::Tilemap>(options.Map);
        m_Camera.SetZoom(options.Zoom);
        m_Camera.GetFollowParams() = {.DeadZone = {24.0f, 16.0f}, .Damping = 6.0f};
        Respawn();
    }

    // The hero to the map's "spawn" object (or the middle of the map).
    void Respawn()
    {
        const Emerald::MapObject* spawn = m_Map->FindObject("spawn");
        const Emerald::Rect2D bounds = m_Map->GetBounds();
        m_Feet = spawn ? spawn->Position : (bounds.Min + bounds.Max) * 0.5f;
        m_Camera.SetPosition(m_Feet);
    }

    // Walks the hero (`direction` from the input axes, length up to 1) and moves the camera.
    // Returns whether the hero moved.
    bool Update(Vec2 direction, bool run, f32 dt, Vec2 viewSize)
    {
        m_Camera.SetViewSize(viewSize);
        m_Camera.SetBounds(m_Map->GetBounds()); // every frame: a reload may resize the map
        bool moved = false;
        if (m_Options.Pan) {
            // A slow diagonal back and forth over the whole map.
            m_PanTime += dt;
            const Emerald::Rect2D b = m_Map->GetBounds();
            const f32 t = 0.5f - 0.5f * std::cos(m_PanTime * 0.05f);
            m_Camera.SetPosition(b.Min + (b.Max - b.Min) *
                                             Vec2(t, 0.5f + 0.4f * std::sin(m_PanTime * 0.11f)));
        } else {
            const f32 speed = (run ? 150.0f : 80.0f) * dt; // map pixels per step
            const Emerald::Aabb box = GetHeroBox();
            const Emerald::TileMove move = m_Map->MoveAndCollide(box, direction * speed);
            m_Feet += move.Delta;
            moved = Emerald::Length(move.Delta) > 0.01f;
            if (direction.x != 0.0f)
                m_FacingLeft = direction.x < 0.0f;
            m_Camera.Follow(m_Feet, dt);
        }
        m_Camera.Update(dt);
        return moved;
    }

    // Draws the map around the hero (`drawHero(feet, facingLeft)` draws it, between the layers
    // below the "Objects" layer and the ones above it), then the debug overlays.
    template <typename DrawHero>
    void Draw(Emerald::Renderer2D& r, Vec2 viewSize, Vec2 targetSize, DrawHero&& drawHero)
    {
        m_Camera.SetViewSize(viewSize);
        m_Camera.SetTargetSize(targetSize);
        const Emerald::Tilemap& map = *m_Map;
        const auto& layers = map.GetLayers();
        if (m_LayerShown.size() != layers.size()) // first frame, or a reload changed the layers
            m_LayerShown.assign(layers.size(), true);

        // Culled to what the camera sees, or (for the benchmark) the whole map.
        const Emerald::Rect2D view = m_Cull ? m_Camera.GetVisibleBounds() : map.GetBounds();
        r.Begin(m_Camera);
        const u64 start = SDL_GetTicksNS();
        m_TilesDrawn = 0;
        bool heroDrawn = false;
        for (usize i = 0; i < layers.size(); ++i) {
            if (layers[i].Kind == Emerald::LayerKind::Objects && !heroDrawn) {
                drawHero(m_Feet, m_FacingLeft);
                heroDrawn = true;
            }
            if (layers[i].Visible && m_LayerShown[i])
                m_TilesDrawn += map.DrawLayer(r, i, view);
        }
        m_DrawMs = static_cast<f64>(SDL_GetTicksNS() - start) / 1e6;
        if (!heroDrawn)
            drawHero(m_Feet, m_FacingLeft);

        if (m_ShowCollision) {
            map.DrawCollision(r, view);
            const Emerald::Aabb hero = GetHeroBox();
            r.DrawRect(hero.Min, hero.Max - hero.Min, {0.3f, 1.0f, 0.4f, 1.0f});
        }
        if (m_ShowObjects)
            DrawObjects(r);
        r.End();
        UpdateStats();
    }

    // Command-line switches for screenshots and the benchmark.
    void SetOverlays(bool collision, bool objects, bool cull)
    {
        m_ShowCollision = collision;
        m_ShowObjects = objects;
        m_Cull = cull;
    }
    [[nodiscard]] u32 GetTilesDrawn() const { return m_TilesDrawn; }
    [[nodiscard]] const Emerald::Camera2D& GetCamera() const { return m_Camera; }

#if EMERALD_WITH_IMGUI
    void ShowImGui()
    {
        const Emerald::Tilemap& map = *m_Map;
        ImGui::Text("Tilemap %s: %d x %d tiles of %d px, %zu tilesets",
                    m_Options.Map.filename().string().c_str(), map.GetSize().x, map.GetSize().y,
                    map.GetTileSize().x, map.GetTilesets().size());
        ImGui::Text("Visible tiles drawn: %u (%.3f ms to batch)", m_TilesDrawn, m_DrawMs);
        const auto& layers = map.GetLayers();
        for (usize i = 0; i < layers.size() && i < m_LayerShown.size(); ++i) {
            bool shown = m_LayerShown[i];
            const std::string label =
                layers[i].Name +
                (layers[i].Kind == Emerald::LayerKind::Objects ? " (objects)" : "");
            if (ImGui::Checkbox(label.c_str(), &shown))
                m_LayerShown[i] = shown;
            if (i + 1 < layers.size())
                ImGui::SameLine();
        }
        ImGui::Checkbox("Collision overlay", &m_ShowCollision);
        ImGui::SameLine();
        ImGui::Checkbox("Objects", &m_ShowObjects);
        ImGui::SameLine();
        ImGui::Checkbox("Cull to camera", &m_Cull);
        f32 zoom = m_Camera.GetZoom();
        if (ImGui::SliderFloat("Zoom", &zoom, 0.25f, 6.0f))
            m_Camera.SetZoom(zoom);
        const Emerald::Vec2i tile = map.WorldToTile(m_Feet);
        ImGui::Text("Hero at tile %d, %d  (%.1f, %.1f)", tile.x, tile.y, static_cast<f64>(m_Feet.x),
                    static_cast<f64>(m_Feet.y));
        // The object the hero stands in, with its custom properties: what a game reads.
        if (const Emerald::MapObject* o = FindObjectAtHero()) {
            ImGui::Text("In object '%s' (class %s)", o->Name.c_str(), o->Type.c_str());
            for (const auto& [name, value] : o->Props.GetAll()) {
                if (const auto* s = std::get_if<std::string>(&value))
                    ImGui::BulletText("%s = \"%s\"", name.c_str(), s->c_str());
                else if (const auto* b = std::get_if<bool>(&value))
                    ImGui::BulletText("%s = %s", name.c_str(), *b ? "true" : "false");
                else if (const auto* n = std::get_if<i64>(&value))
                    ImGui::BulletText("%s = %lld", name.c_str(), static_cast<long long>(*n));
                else
                    ImGui::BulletText("%s = %g", name.c_str(), std::get<f64>(value));
            }
        } else {
            ImGui::TextDisabled("Walk into an object (sign, chest, pond, stairs) to see it");
        }
        if (ImGui::Button("Respawn"))
            Respawn();
        ImGui::Separator();
    }
#endif

private:
    // The hero collides with its feet: 10 x 6 pixels at the bottom of the 16 x 16 sprite.
    [[nodiscard]] Emerald::Aabb GetHeroBox() const
    {
        return {m_Feet - Vec2(5.0f, 6.0f), m_Feet + Vec2(5.0f, 0.0f)};
    }

    [[nodiscard]] const Emerald::MapObject* FindObjectAtHero() const
    {
        for (const Emerald::MapLayer& layer : m_Map->GetLayers())
            for (const Emerald::MapObject& o : layer.Objects)
                if (o.Shape != Emerald::ObjectShape::Point &&
                    Emerald::Overlaps(o.GetBounds(), GetHeroBox()))
                    return &o;
        return nullptr;
    }

    // Object outlines (points as small crosses), the one the hero is in highlighted.
    void DrawObjects(Emerald::Renderer2D& r) const
    {
        const Emerald::MapObject* current = FindObjectAtHero();
        for (const Emerald::MapLayer& layer : m_Map->GetLayers()) {
            for (const Emerald::MapObject& o : layer.Objects) {
                const Vec4 color =
                    &o == current ? Vec4(1.0f, 0.9f, 0.3f, 1.0f) : Vec4(0.4f, 0.8f, 1.0f, 0.8f);
                if (o.Shape == Emerald::ObjectShape::Point) {
                    r.DrawLine(o.Position - Vec2(4.0f, 0.0f), o.Position + Vec2(4.0f, 0.0f), color);
                    r.DrawLine(o.Position - Vec2(0.0f, 4.0f), o.Position + Vec2(0.0f, 4.0f), color);
                } else {
                    r.DrawRect(o.Position, o.Size, color);
                }
            }
        }
    }

    // --stats: averages over two seconds, logged.
    void UpdateStats()
    {
        if (!m_Options.Stats)
            return;
        const u64 now = SDL_GetTicksNS();
        if (m_StatsStart == 0)
            m_StatsStart = now;
        ++m_StatsFrames;
        m_StatsTiles += m_TilesDrawn;
        m_StatsDrawMs += m_DrawMs;
        const f64 seconds = static_cast<f64>(now - m_StatsStart) / 1e9;
        if (seconds < 2.0)
            return;
        const f64 frames = static_cast<f64>(m_StatsFrames);
        EM_INFO("Tilemap stats: {:.1f} fps ({:.2f} ms/frame), {:.0f} tiles drawn per frame, "
                "{:.3f} ms per frame in DrawLayer, zoom {:.2f}, cull {}",
                frames / seconds, seconds * 1000.0 / frames,
                static_cast<f64>(m_StatsTiles) / frames, m_StatsDrawMs / frames,
                static_cast<f64>(m_Camera.GetZoom()), m_Cull ? "on" : "off");
        m_StatsStart = now;
        m_StatsFrames = 0;
        m_StatsTiles = 0;
        m_StatsDrawMs = 0.0;
    }

    Options m_Options;
    Emerald::AssetHandle<Emerald::Tilemap> m_Map;
    Emerald::Camera2D m_Camera;
    Vec2 m_Feet{};
    bool m_FacingLeft = false;
    std::vector<bool> m_LayerShown; // ImGui toggles, by layer index
    bool m_ShowCollision = false;
    bool m_ShowObjects = false;
    bool m_Cull = true;
    u32 m_TilesDrawn = 0;
    f64 m_DrawMs = 0.0;
    f32 m_PanTime = 0.0f;
    u64 m_StatsStart = 0;
    u64 m_StatsFrames = 0;
    u64 m_StatsTiles = 0;
    f64 m_StatsDrawMs = 0.0;
};

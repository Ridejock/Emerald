#pragma once

// Lighting + post-effect chain demo (#13): a floor and a few props with normal maps, a warm point
// light and a cool spot that follows the mouse, ambient, and a Tint + optional CRT in the chain.
// From the title menu or --lighting. Lighting off (L) restores the unlit path (no extra cost).

#include <cmath>
#include <optional>
#include <vector>

#include <Emerald/Emerald.h>

#include "Shared.h"

class LightingScene final : public Emerald::Scene {
public:
    explicit LightingScene(SandboxShared& shared) : Scene("Lighting"), m_Shared(shared) {}

    void OnEnter() override
    {
        BuildSurfaces();
        // A mild grade in front of an optional CRT: shows add/reorder of the chain.
        if (!m_Shared.App.GetPostChain().Find("Tint")) {
            auto tint = std::make_unique<Emerald::TintEffect>();
            tint->GetParams() = {.Color = {1.05f, 1.0f, 0.95f}, .Vignette = 0.25f};
            m_Shared.App.GetPostChain().Add(std::move(tint));
        }
        m_Shared.App.GetRenderer2D().SetLightingEnabled(true);
        m_Shared.App.GetRenderer2D().SetAmbient({0.08f, 0.09f, 0.14f});
    }

    void OnExit() override
    {
        m_Shared.App.GetRenderer2D().SetLightingEnabled(false);
        m_Shared.App.GetRenderer2D().ClearLights();
        // Leave Tint in the chain only while this scene is up.
        Emerald::PostChain& chain = m_Shared.App.GetPostChain();
        for (usize i = 0; i < chain.GetCount(); ++i) {
            if (chain.Get(i)->GetName() == "Tint") {
                chain.Remove(i);
                break;
            }
        }
    }

    void OnUpdate(f32 dt) override
    {
        m_Time += dt;
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        if (input.WasActionPressed("Crt"))
            m_Shared.App.SetCrtEnabled(!m_Shared.App.IsCrtEnabled());
        if (input.WasActionPressed("Pulse")) // L is free; reuse Pulse (Space) to toggle lighting
            m_Lit = !m_Lit;
        // Swap Tint and CRT when both are present (shows reorder).
        if (input.WasActionPressed("Shake")) {
            Emerald::PostChain& chain = m_Shared.App.GetPostChain();
            const usize tint = IndexOf(chain, "Tint");
            const usize crt = IndexOf(chain, "CRT");
            if (tint != Emerald::PostChain::npos && crt != Emerald::PostChain::npos)
                chain.Move(tint, crt);
        }

        Emerald::Renderer2D& r = m_Shared.App.GetRenderer2D();
        r.SetLightingEnabled(m_Lit);
        r.ClearLights();
        if (!m_Lit)
            return;
        // Orbiting warm lamp.
        const Emerald::Vec2 orbit{m_View.x * 0.5f + 120.0f * std::cos(m_Time * 0.7f),
                                  m_View.y * 0.55f + 40.0f * std::sin(m_Time * 0.7f)};
        r.AddLight({.Kind = Emerald::LightKind::Point,
                    .Position = orbit,
                    .Z = 50.0f,
                    .Color = {1.0f, 0.75f, 0.45f},
                    .Intensity = 1.4f,
                    .Radius = 280.0f,
                    .Falloff = 1.2f});
        // Spot from above the mouse, aiming down-ish.
        const Emerald::Vec2 mouse = input.GetMouse().GetPosition();
        r.AddLight({.Kind = Emerald::LightKind::Spot,
                    .Position = {mouse.x, mouse.y - 80.0f},
                    .Z = 30.0f,
                    .Color = {0.45f, 0.7f, 1.0f},
                    .Intensity = 1.8f,
                    .Radius = 320.0f,
                    .Falloff = 1.0f,
                    .Direction = {0.0f, 1.0f},
                    .InnerAngle = 0.25f,
                    .OuterAngle = 0.65f});
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        using Emerald::Vec2;
        m_View = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(m_View.x, m_View.y));
        if (m_Floor) {
            r.DrawSprite(*m_Floor, m_View * 0.5f,
                         {.Size = m_View, .NormalMap = m_FloorNormal ? &*m_FloorNormal : nullptr});
        }
        for (const Prop& prop : m_Props) {
            r.DrawSprite(*m_Prop, prop.At,
                         {.Size = prop.Size,
                          .Rotation = prop.Spin * m_Time,
                          .NormalMap = m_PropNormal ? &*m_PropNormal : nullptr});
        }
        if (m_Shared.PixelFont) {
            // The text stays readable in the dark: lighting is per draw, so turn it off here.
            r.SetLightingEnabled(false);
            const char* hint =
                m_Lit ? "Lamps on  |  Space: lighting off  |  C: CRT  |  X: swap Tint/CRT"
                      : "Lighting off (unlit path)  |  Space: lighting on";
            r.DrawString(*m_Shared.PixelFont, hint, {12.0f, 12.0f}, {0.85f, 0.9f, 0.95f, 1.0f});
            r.DrawString(*m_Shared.PixelFont, "Lighting + post chain (#13)", {12.0f, 36.0f},
                         {0.35f, 0.95f, 0.55f, 1.0f});
            r.SetLightingEnabled(m_Lit);
        }
        r.End();
    }

private:
    struct Prop {
        Emerald::Vec2 At{};
        Emerald::Vec2 Size{};
        f32 Spin = 0.0f;
    };

    static usize IndexOf(Emerald::PostChain& chain, std::string_view name)
    {
        for (usize i = 0; i < chain.GetCount(); ++i)
            if (chain.Get(i)->GetName() == name)
                return i;
        return Emerald::PostChain::npos;
    }

    // Procedural albedo + matching normals (bump from a height pattern).
    void BuildSurfaces()
    {
        SDL_GPUDevice* device = m_Shared.App.GetRenderer().GetDevice();
        m_Floor = MakeBrick(device, 64, 64, false);
        m_FloorNormal = MakeBrick(device, 64, 64, true);
        m_Prop = MakeBlob(device, 32, 32, false);
        m_PropNormal = MakeBlob(device, 32, 32, true);
        m_Props = {{{200.0f, 300.0f}, {80.0f, 80.0f}, 0.4f},
                   {{500.0f, 220.0f}, {120.0f, 60.0f}, -0.2f},
                   {{700.0f, 400.0f}, {64.0f, 96.0f}, 0.15f}};
    }

    static std::optional<Emerald::Texture> MakeBrick(SDL_GPUDevice* device, i32 w, i32 h,
                                                     bool normal)
    {
        Emerald::Image image{.Width = w, .Height = h, .Pixels = std::vector<u8>(w * h * 4)};
        for (i32 y = 0; y < h; ++y) {
            for (i32 x = 0; x < w; ++x) {
                const f32 u = static_cast<f32>(x) / static_cast<f32>(w);
                const f32 v = static_cast<f32>(y) / static_cast<f32>(h);
                // Brick height: mortar grooves.
                const f32 brick =
                    std::fabs(std::fmod(u * 4.0f + ((y / 8) % 2) * 0.5f, 1.0f) - 0.5f);
                const f32 row = std::fabs(std::fmod(v * 4.0f, 1.0f) - 0.5f);
                const f32 height = brick > 0.42f || row > 0.42f ? 0.15f : 0.85f;
                u8* px = &image.Pixels[(y * w + x) * 4];
                if (normal) {
                    const f32 dx = (SampleHeight(u + 1.0f / w, v, true) -
                                    SampleHeight(u - 1.0f / w, v, true)) *
                                   4.0f;
                    const f32 dy = (SampleHeight(u, v + 1.0f / h, true) -
                                    SampleHeight(u, v - 1.0f / h, true)) *
                                   4.0f;
                    // Encode world-ish normal: (-dh/dx, -dh/dy, 1).
                    Emerald::Vec3 n = Emerald::Normalize(Emerald::Vec3{-dx, -dy, 1.0f});
                    px[0] = static_cast<u8>((n.x * 0.5f + 0.5f) * 255.0f);
                    px[1] = static_cast<u8>((n.y * 0.5f + 0.5f) * 255.0f);
                    px[2] = static_cast<u8>((n.z * 0.5f + 0.5f) * 255.0f);
                    px[3] = 255;
                } else {
                    const u8 c = static_cast<u8>(40.0f + height * 90.0f);
                    px[0] = c;
                    px[1] = static_cast<u8>(c * 0.85f);
                    px[2] = static_cast<u8>(c * 0.7f);
                    px[3] = 255;
                }
            }
        }
        return Emerald::Texture::Create(device, image, {.Filter = Emerald::TextureFilter::Linear});
    }

    static f32 SampleHeight(f32 u, f32 v, bool brick)
    {
        if (!brick) {
            const f32 dx = u - 0.5f;
            const f32 dy = v - 0.5f;
            return std::max(0.0f, 1.0f - 4.0f * (dx * dx + dy * dy));
        }
        const f32 bx =
            std::fabs(std::fmod(u * 4.0f + (static_cast<i32>(v * 4.0f) % 2) * 0.5f, 1.0f) - 0.5f);
        const f32 by = std::fabs(std::fmod(v * 4.0f, 1.0f) - 0.5f);
        return bx > 0.42f || by > 0.42f ? 0.15f : 0.85f;
    }

    static std::optional<Emerald::Texture> MakeBlob(SDL_GPUDevice* device, i32 w, i32 h,
                                                    bool normal)
    {
        Emerald::Image image{.Width = w, .Height = h, .Pixels = std::vector<u8>(w * h * 4)};
        for (i32 y = 0; y < h; ++y) {
            for (i32 x = 0; x < w; ++x) {
                const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(w);
                const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(h);
                const f32 height = SampleHeight(u, v, false);
                u8* px = &image.Pixels[(y * w + x) * 4];
                if (height <= 0.0f) {
                    px[0] = px[1] = px[2] = 0;
                    px[3] = 0;
                    continue;
                }
                if (normal) {
                    const f32 dx =
                        SampleHeight(u + 1.0f / w, v, false) - SampleHeight(u - 1.0f / w, v, false);
                    const f32 dy =
                        SampleHeight(u, v + 1.0f / h, false) - SampleHeight(u, v - 1.0f / h, false);
                    Emerald::Vec3 n =
                        Emerald::Normalize(Emerald::Vec3{-dx * 3.0f, -dy * 3.0f, 1.0f});
                    px[0] = static_cast<u8>((n.x * 0.5f + 0.5f) * 255.0f);
                    px[1] = static_cast<u8>((n.y * 0.5f + 0.5f) * 255.0f);
                    px[2] = static_cast<u8>((n.z * 0.5f + 0.5f) * 255.0f);
                    px[3] = 255;
                } else {
                    px[0] = static_cast<u8>(60.0f + height * 140.0f);
                    px[1] = static_cast<u8>(90.0f + height * 100.0f);
                    px[2] = static_cast<u8>(120.0f + height * 80.0f);
                    px[3] = 255;
                }
            }
        }
        return Emerald::Texture::Create(device, image, {.Filter = Emerald::TextureFilter::Linear});
    }

    SandboxShared& m_Shared;
    Emerald::Vec2 m_View{1280.0f, 720.0f};
    f32 m_Time = 0.0f;
    bool m_Lit = true;
    std::optional<Emerald::Texture> m_Floor, m_FloorNormal, m_Prop, m_PropNormal;
    std::vector<Prop> m_Props;
};

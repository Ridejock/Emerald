#pragma once

// Menu demo (Emerald/Tween): an overlay menu animated with tweens and timers. Opening it drops the
// title in with a bounce, slides the panel in from the right with a little overshoot and fades the
// items in one after another; the selection highlight glides between items. Closing plays a chain:
// the items fade out, then the panel slides away, then the title leaves and the menu hides. The
// title screen and the pause menu (TitleScene.h, PauseScene.h) each have one, with their own items.

#include <string>
#include <utility>
#include <vector>

#include <Emerald/Emerald.h>

// m_HighlightColor (a Vec4, tweened through a pointer) is 16-byte aligned, so the compiler pads
// this class; MSVC reports that at /W4 as warning C4324 (see SpriteOptions in Sprite.h).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
class MenuDemo {
public:
    using Vec2 = Emerald::Vec2;
    using Vec4 = Emerald::Vec4;

    MenuDemo(std::string title, std::vector<std::string> items, std::string prompt)
        : m_Title(std::move(title)), m_Items(std::move(items)), m_Prompt(std::move(prompt)),
          m_ItemAlpha(m_Items.size(), 0.0f) // sized once: the tweens point into it
    {
    }

    // Plays the intro from the start (also when it is already open).
    void Open()
    {
        using Emerald::Easing;
        m_Tweens.Clear();
        m_Timers.Clear();
        m_Visible = true;
        m_Closing = false;
        m_Tweens.FromTo(&m_Backdrop, 0.0f, 0.7f, 0.4f);
        m_Tweens.FromTo(&m_TitleY, -80.0f, 70.0f, 0.9f, {.Curve = Easing::BounceOut});
        m_Tweens.FromTo(&m_TitleAlpha, 0.0f, 1.0f, 0.4f);
        m_Tweens.FromTo(&m_PanelSlide, 0.0f, 1.0f, 0.6f, {.Curve = Easing::BackOut, .Delay = 0.3f});
        for (usize i = 0; i < m_Items.size(); ++i)
            m_Tweens.FromTo(&m_ItemAlpha[i], 0.0f, 1.0f, 0.3f,
                            {.Delay = 0.6f + 0.1f * static_cast<f32>(i)}); // one after another
        // The highlight breathes for as long as the menu is open.
        m_Tweens.FromTo(
            &m_Glow, 0.35f, 0.8f, 0.7f,
            {.Curve = Easing::QuadInOut, .Repeat = Emerald::Tweens::kForever, .Yoyo = true});
        m_HighlightY = GetItemY(m_Selected);
        // A blinking hint, once the items are in.
        m_PromptOn = false;
        m_Timers.After(1.2f, [this] {
            m_PromptOn = true;
            m_Timers.Every(0.5f, [this] { m_PromptOn = !m_PromptOn; });
        });
    }

    // Plays the outro as a chain of tweens; the menu hides when the last one completes.
    void Close()
    {
        using Emerald::Easing;
        if (!m_Visible || m_Closing)
            return;
        m_Closing = true;
        m_Tweens.Clear(); // stop the intro wherever it is
        m_Timers.Clear();
        m_PromptOn = false;
        Emerald::TweenId itemsOut;
        for (f32& alpha : m_ItemAlpha)
            itemsOut = m_Tweens.To(&alpha, 0.0f, 0.15f);
        const Emerald::TweenId panelOut =
            m_Tweens.To(&m_PanelSlide, 0.0f, 0.35f, {.Curve = Easing::BackIn, .After = itemsOut});
        m_Tweens.To(&m_Backdrop, 0.0f, 0.4f, {.After = itemsOut});
        m_Tweens.To(&m_TitleAlpha, 0.0f, 0.3f, {.After = panelOut});
        m_Tweens.To(&m_TitleY, -80.0f, 0.3f,
                    {.Curve = Easing::BackIn, .After = panelOut, .OnComplete = [this] {
                         m_Visible = false;
                         m_Closing = false;
                     }});
    }

    // Open and taking input (not while it is closing).
    [[nodiscard]] bool IsOpen() const { return m_Visible && !m_Closing; }

    void MoveSelection(i32 delta)
    {
        const i32 count = static_cast<i32>(m_Items.size());
        m_Selected = (m_Selected + delta + count) % count;
        // A new glide replaces the old one, so two tweens never fight over the highlight.
        m_Tweens.CancelTarget(&m_HighlightY);
        m_Tweens.To(&m_HighlightY, GetItemY(m_Selected), 0.2f,
                    {.Curve = Emerald::Easing::CubicOut});
        m_Tweens.CancelTarget(&m_HighlightColor);
        m_Tweens.FromTo(&m_HighlightColor, {1.0f, 1.0f, 1.0f, 1.0f}, kGreen, 0.3f);
    }

    // The selected item's index into the items given to the constructor.
    [[nodiscard]] i32 GetSelected() const { return m_Selected; }

    // Driven by the game's update dt, like everything else that moves.
    void Update(f32 dt)
    {
        m_Tweens.Update(dt);
        m_Timers.Update(dt);
    }

    // In screen space (window units), on top of everything else.
    void Draw(Emerald::Renderer2D& r, const Vec2& size, const Emerald::Font& title,
              const Emerald::Font& font, const Emerald::Font& small, const Emerald::Texture& white)
    {
        if (!m_Visible)
            return;
        using Emerald::TextAlign;
        const auto fill = [&](const Vec2& at, const Vec2& extent, const Vec4& color) {
            r.DrawSprite(white, at, {.Size = extent, .Origin = {0.0f, 0.0f}, .Tint = color});
        };
        fill({0.0f, 0.0f}, size, {0.0f, 0.02f, 0.04f, m_Backdrop});

        r.DrawString(title, m_Title, {size.x * 0.5f, m_TitleY}, {0.35f, 0.95f, 0.55f, m_TitleAlpha},
                     1.6f, TextAlign::Center);

        // The panel slides between just off the right edge (0) and its place (1).
        const Vec2 panelSize{kPanelWidth,
                             kTop * 2.0f + kItemStep * static_cast<f32>(m_Items.size())};
        const f32 x = Emerald::Lerp(size.x + 10.0f, size.x - kPanelWidth - 60.0f, m_PanelSlide);
        const f32 y = size.y * 0.5f - panelSize.y * 0.5f;
        fill({x, y}, panelSize, {0.05f, 0.1f, 0.12f, 0.92f});
        r.DrawRect({x, y}, panelSize, kGreen);

        const Vec4 highlight{m_HighlightColor.x, m_HighlightColor.y, m_HighlightColor.z, m_Glow};
        fill({x + 10.0f, y + m_HighlightY - 8.0f}, {kPanelWidth - 20.0f, kItemStep - 4.0f},
             highlight);
        for (usize i = 0; i < m_Items.size(); ++i) {
            const bool selected = static_cast<i32>(i) == m_Selected;
            const Vec4 color = selected ? Vec4(1.0f, 1.0f, 1.0f, m_ItemAlpha[i])
                                        : Vec4(0.7f, 0.8f, 0.8f, m_ItemAlpha[i]);
            r.DrawString(font, m_Items[i], {x + 30.0f, y + GetItemY(static_cast<i32>(i))}, color);
        }
        if (m_PromptOn)
            r.DrawString(small, m_Prompt, {x + kPanelWidth * 0.5f, y + panelSize.y + 14.0f},
                         {1.0f, 0.85f, 0.3f, 1.0f}, 1.0f, TextAlign::Center);
    }

    [[nodiscard]] usize GetTweenCount() const { return m_Tweens.GetCount(); }
    [[nodiscard]] usize GetTimerCount() const { return m_Timers.GetCount(); }
    [[nodiscard]] bool IsVisible() const { return m_Visible; }
    [[nodiscard]] bool IsClosing() const { return m_Closing; }

private:
    static constexpr Vec4 kGreen{0.18f, 0.8f, 0.44f, 1.0f};
    static constexpr f32 kPanelWidth = 300.0f;
    static constexpr f32 kTop = 24.0f;      // panel padding above the first item
    static constexpr f32 kItemStep = 36.0f; // between items

    // Item i's text top, relative to the panel's top.
    [[nodiscard]] static f32 GetItemY(i32 i)
    {
        return kTop + kItemStep * static_cast<f32>(i) + 8.0f;
    }

    // The tweens and timers live in this object, next to the values they animate and the `this`
    // their callbacks capture, so none of them can outlive what they point at.
    Emerald::Tweens m_Tweens;
    Emerald::Timers m_Timers;

    std::string m_Title;
    std::vector<std::string> m_Items;
    std::string m_Prompt; // blinks under the panel

    // Animated values (all written by m_Tweens).
    f32 m_Backdrop = 0.0f; // alpha of the dark overlay
    f32 m_TitleY = -80.0f; // window units
    f32 m_TitleAlpha = 0.0f;
    f32 m_PanelSlide = 0.0f; // 0 = off screen, 1 = in place
    std::vector<f32> m_ItemAlpha;
    f32 m_HighlightY = 0.0f; // relative to the panel's top
    Vec4 m_HighlightColor = kGreen;
    f32 m_Glow = 0.5f; // highlight alpha

    i32 m_Selected = 0;
    bool m_Visible = false;
    bool m_Closing = false;
    bool m_PromptOn = false;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

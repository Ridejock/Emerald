#pragma once

// Branching dialogue demo (#23): talk to the keeper (assets/dialogue/keeper.json). The answers
// branch, "Can I have the key?" only shows after asking about the gate (a flag), "Open the gate"
// only with the key, and "Anything else?" can be picked once. F5 saves the flags and the current
// card (also mid-conversation) through the save system, F9 restores them. Edit keeper.json while
// it runs (debug build) and the open card updates. From the title menu or --dialogue.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Emerald/Emerald.h>

#include "Shared.h"

class DialogueScene final : public Emerald::Scene {
public:
    explicit DialogueScene(SandboxShared& shared)
        : Scene("Dialogue"), m_Shared(shared),
          m_Deck(shared.App.GetAssets().Load<Emerald::DialogueDeck>("dialogue/keeper.json")),
          m_Talk(*m_Deck, m_Flags), m_Hero(shared.Hero)
    {
        m_Ui.GetStyle().Highlight = kHighlightColors[m_Shared.Highlight];
        m_Box.GetStyle().Highlight = kHighlightColors[m_Shared.Highlight];
    }

    void OnUpdate(f32 dt) override
    {
        const Emerald::Input& input = m_Shared.App.GetInput();
        if (input.WasActionPressed("Menu"))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));
        if (input.WasActionPressed("QuickSave"))
            Save();
        if (input.WasActionPressed("QuickLoad"))
            Load();
        m_Hero.Update(false, 1.0f, dt);
        m_Message = Emerald::Max(m_Message - dt, 0.0f);
        if (!m_Shared.PixelFont)
            return;

        // Decided before the box runs: the key that closes the conversation must not also press
        // "Talk" in the side panel on the same frame.
        const bool talking = m_Talk.IsActive();
        const Emerald::UiInput ui = Emerald::ReadUiInput(input, input.GetMouse().GetPosition());
        const Emerald::Vec2 size = m_Shared.GetViewSize();
        m_Box.Update(m_Talk, ui, *m_Shared.PixelFont, dt,
                     {.Position = {size.x * 0.5f, size.y - 16.0f}, .Width = 600.0f});
        if (!talking)
            UpdatePanel(ui, dt);
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        using Emerald::Vec2;
        const Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        r.FillRect({0.0f, 0.0f}, size, {0.05f, 0.07f, 0.09f, 1.0f});
        // The player on the left, the keeper (the hero, mirrored and tinted) on the right.
        if (m_Hero.IsLoaded()) {
            r.DrawSprite(m_Hero.GetAnimator(), {size.x * 0.35f, size.y * 0.42f},
                         {.Scale = Vec2(6.0f), .PixelSnap = true});
            r.DrawSprite(m_Hero.GetAnimator(), {size.x * 0.65f, size.y * 0.42f},
                         {.Scale = Vec2(6.0f),
                          .Tint = {0.6f, 1.0f, 0.75f, 1.0f},
                          .FlipX = true,
                          .PixelSnap = true});
        }
        if (!m_Shared.HasFonts()) {
            r.End();
            return;
        }
        const Emerald::Font& small = *m_Shared.SmallFont;
        if (!m_Talk.IsActive())
            m_Ui.Draw(r, *m_Shared.PixelFont);
        m_Box.Draw(r, *m_Shared.PixelFont, FindPortrait(), 4.0f);

        // The flag store, so the gates can be watched (wrapped to the right column).
        const std::vector<std::string> flags =
            Emerald::WrapText("Flags: " + Describe(), 300.0f,
                              [&small](std::string_view s) { return small.MeasureText(s).x; });
        f32 y = 24.0f;
        for (const std::string& line : flags) {
            r.DrawString(small, line, {size.x - 320.0f, y}, {0.65f, 0.7f, 0.75f, 1.0f});
            y += 12.0f;
        }
        r.DrawString(small, "F5 save   F9 load   M pause", {size.x - 320.0f, y + 8.0f},
                     {0.65f, 0.7f, 0.75f, 1.0f});
        if (m_Message > 0.0f)
            r.DrawString(small, m_Status, {size.x - 320.0f, y + 24.0f},
                         kHighlightColors[m_Shared.Highlight]);
        r.End();
    }

private:
    // The side panel, while nobody talks.
    void UpdatePanel(const Emerald::UiInput& ui, f32 dt)
    {
        m_Ui.Begin(ui, dt);
        m_Ui.BeginPanel("DIALOGUE", {.Position = {16.0f, 16.0f}, .Width = 360.0f});
        if (m_Ui.Button("Talk to the keeper"))
            m_Talk.Start();
        if (m_Ui.Button("Save (F5)"))
            Save();
        if (m_Ui.Button("Load (F9)"))
            Load();
        if (m_Ui.Button("Forget everything"))
            m_Flags.Reset();
        if (m_Ui.Button("Title") || m_Ui.WasBackPressed())
            GetStack()->ReplaceAll(m_Shared.Make(SceneId::Title), FadeBlack());
        m_Ui.EndPanel();
        m_Ui.End();
    }

    // The flags and the current card go into one slot, <per-user folder>/saves/dialogue.sav.
    void Save()
    {
        Emerald::SaveData data;
        m_Flags.Save(data, "flags");
        m_Talk.Save(data, "talk");
        Show(m_Shared.Saves.Save("dialogue", data, "Dialogue demo") ? "Saved" : "Saving failed");
    }

    void Load()
    {
        const Emerald::LoadResult loaded = m_Shared.Saves.Load("dialogue");
        if (!loaded) {
            Show("Nothing saved yet");
            return;
        }
        m_Flags.Load(loaded.Data, "flags");
        // Back into the saved card, or out of the conversation if it was saved outside one.
        m_Talk.Restore(loaded.Data, "talk");
        Show("Loaded");
    }

    void Show(std::string text)
    {
        m_Status = std::move(text);
        m_Message = 2.0f;
    }

    // The card's portrait names a sprite in the hero sheet.
    [[nodiscard]] const Emerald::Sprite* FindPortrait()
    {
        const Emerald::DialogueCard* card = m_Talk.GetCard();
        if (!card || card->Portrait.empty() || !m_Shared.Hero)
            return nullptr;
        m_Portrait = m_Shared.Hero->Find(card->Portrait);
        return m_Portrait ? &*m_Portrait : nullptr;
    }

    [[nodiscard]] std::string Describe() const
    {
        std::string text;
        for (const std::string& flag : m_Flags.GetFlags())
            text += flag + ' ';
        for (const auto& [name, value] : m_Flags.GetValues())
            text += name + '=' + std::to_string(value) + ' ';
        return text.empty() ? "(none)" : text;
    }

    SandboxShared& m_Shared;
    Emerald::AssetHandle<Emerald::DialogueDeck> m_Deck;
    Emerald::DialogueFlags m_Flags;
    Emerald::Dialogue m_Talk;
    Emerald::DialogueBox m_Box;
    Emerald::Ui m_Ui;
    HeroSprite m_Hero;
    std::optional<Emerald::Sprite> m_Portrait;
    std::string m_Status;
    f32 m_Message = 0.0f; // seconds the status message stays up
};

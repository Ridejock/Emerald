#pragma once

// Dropdown / pop-up demo (#42). A shop keeps a short menu on screen; Buy and Sell open a list
// over it (anchored to the row that opened it), and picking an entry opens a centered confirm
// on top of that list. Back or a click outside closes only the top one. The list scrolls.
// Look and the prompt words come from assets/ui/popup.json (re-read when the file changes).
// The glyphs on the prompt bar are the UiAccept / UiBack bindings. From the title or --popup.

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <Emerald/Emerald.h>

#include "Shared.h"

class PopupScene final : public Emerald::Scene {
public:
    explicit PopupScene(SandboxShared& shared) : Scene("Pop-ups"), m_Shared(shared)
    {
        ReloadStyle(true);
    }

    void OnUpdate(f32 dt) override
    {
        using Emerald::UiPopupAnchor;
        ReloadStyle(false);
        const Emerald::Input& input = m_Shared.App.GetInput();
        // Start pauses. M is also UiBack, so it closes a pop-up or leaves instead of pausing.
        if (input.WasActionPressed("Menu") && !input.WasActionPressed(Emerald::kUiBack))
            GetStack()->Push(m_Shared.Make(SceneId::Pause));

        const Emerald::Vec2 size = m_Shared.GetViewSize();
        const Emerald::UiInput ui = Emerald::ReadUiInput(input, input.GetMouse().GetPosition());
        const Emerald::UiPopupOptions list{
            .Width = 420.0f, .Anchor = UiPopupAnchor::Opener, .Screen = size};
        const Emerald::UiPopupOptions confirm{.Width = 460.0f, .Screen = size};

        m_Ui.Begin(ui, dt);
        m_Ui.BeginPanel(
            "SHOP",
            {.Position = {size.x * 0.28f, size.y * 0.5f}, .Width = 360.0f, .Pivot = {0.5f, 0.5f}});
        m_Ui.Label(m_Note.empty() ? "Buy or sell. Lists open on top." : m_Note,
                   Emerald::TextAlign::Center);
        m_Ui.Space(8.0f);
        if (m_Ui.Button("Buy")) {
            m_Trade = Trade::Buy;
            m_Ui.OpenPopup("Wares");
        }
        if (m_Ui.Button("Sell")) {
            m_Trade = Trade::Sell;
            m_Ui.OpenPopup("Wares");
        }
        m_Ui.Space(8.0f);
        m_Ui.Label("Style  assets/ui/popup.json", Emerald::TextAlign::Center);
        if ((m_Ui.Button("Leave") || m_Ui.WasBackPressed()) && !m_Leaving)
            Leave();
        m_Ui.EndPanel();

        const std::span<const std::string_view> wares =
            m_Trade == Trade::Sell ? std::span<const std::string_view>(kSell)
                                   : std::span<const std::string_view>(kWares);
        if (m_Ui.BeginPopup("Wares", list)) {
            for (i32 i = 0; i < static_cast<i32>(wares.size()); ++i) {
                if (!m_Ui.Button(wares[static_cast<usize>(i)]))
                    continue;
                m_Picked = i;
                m_Ui.OpenPopup("Confirm");
            }
            m_Ui.EndPopup();
        }
        if (m_Ui.BeginPopup("Confirm", confirm)) {
            const std::string_view item = m_Picked >= 0 && m_Picked < static_cast<i32>(wares.size())
                                              ? wares[static_cast<usize>(m_Picked)]
                                              : std::string_view("that");
            m_Ui.Label(std::string(m_Trade == Trade::Sell ? "Sell " : "Buy ") + std::string(item) +
                           "?",
                       Emerald::TextAlign::Center);
            m_Ui.Columns(2);
            if (m_Ui.Button("Yes")) {
                m_Note =
                    std::string(m_Trade == Trade::Sell ? "Sold " : "Bought ") + std::string(item);
                m_Ui.ClosePopup();
            }
            if (m_Ui.Button("No"))
                m_Ui.ClosePopup();
            m_Ui.EndPopup();
        }
        m_Ui.End();
    }

    void OnRender2D(Emerald::Renderer2D& r) override
    {
        if (!m_Shared.PixelFont)
            return;
        const Emerald::Vec2 size = m_Shared.GetViewSize();
        r.Begin(Emerald::Mat4::OrthoPixelSpace(size.x, size.y));
        r.FillRect({0.0f, 0.0f}, size, {0.03f, 0.05f, 0.06f, 1.0f});
        m_Ui.Draw(r, *m_Shared.PixelFont);
        r.End();
    }

private:
    enum class Trade : u8 { Buy, Sell };

    void Leave()
    {
        m_Leaving = true;
        if (GetStack()->GetCount() > 1)
            GetStack()->Pop();
        else
            m_Shared.App.Quit();
    }

    // `force` reads the file even when its timestamp has not moved (the first frame).
    void ReloadStyle(bool force)
    {
        const std::filesystem::path path = m_Shared.App.GetAssets().GetRoot() / "ui" / "popup.json";
        std::error_code error;
        const auto time = std::filesystem::last_write_time(path, error);
        if (error) {
            if (force) {
                Emerald::UiStyle style = m_Ui.GetStyle();
                // Logs on failure. A missing file leaves the defaults in place.
                if (Emerald::LoadUiStyleFile(style, path))
                    m_Ui.GetStyle() = std::move(style);
            }
            return;
        }
        if (!force && time == m_StyleTime)
            return;
        Emerald::UiStyle style = m_Ui.GetStyle();
        if (!Emerald::LoadUiStyleFile(style, path))
            return;
        m_Ui.GetStyle() = std::move(style);
        m_StyleTime = time;
    }

    static constexpr std::array<std::string_view, 12> kWares = {
        "Potion",       "Hi-Potion",  "Ether", "Elixir",  "Antidote",   "Echo Herb",
        "Phoenix Down", "Golden Pin", "Tent",  "Cottage", "Smoke Bomb", "Megalixir"};
    static constexpr std::array<std::string_view, 4> kSell = {"Potion", "Ether", "Tent",
                                                              "Phoenix Down"};

    SandboxShared& m_Shared;
    Emerald::Ui m_Ui;
    Trade m_Trade = Trade::Buy;
    i32 m_Picked = -1;
    std::string m_Note;
    std::filesystem::file_time_type m_StyleTime{};
    bool m_Leaving = false;
};

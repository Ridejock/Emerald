#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Tween/Tween.h"
#include "Emerald/UI/UI.h"

namespace Emerald {

class Dialogue;
class Font;
class Renderer2D;
struct Sprite;

struct DialogueBoxOptions {
    Vec2 Position{}; // where the box's pivot goes, in UI units
    f32 Width = 480.0f;
    Vec2 Pivot{0.5f, 1.0f};     // (0.5, 1): Position is the bottom center
    f32 CharsPerSecond = 45.0f; // the typewriter's speed; 0 shows the text at once
    std::string_view ContinueLabel = "Continue";
    std::string_view CloseLabel = "Close";
};

// Shows a Dialogue in a UI panel (UI.h): the speaker as the title, the text typed out by a
// tween, then the answers as buttons. Mouse, keyboard and gamepad work through the UI actions
// (rebindable, see BindDefaultUiActions):
//
// - While the text types, Accept (or a click) shows the whole text at once.
// - Then the answers can be picked; a card without choices shows Continue (or Close at the end).
//
//   // OnUpdate:
//   m_Box.Update(m_Talk, ReadUiInput(input, mouse), *m_Font, dt, {.Position = {w * 0.5f, h - 8}});
//   // OnRender2D, in a pixel-space projection:
//   m_Box.Draw(r, *m_Font, portraitSprite); // the portrait (optional) sits on the box's top left
//
// It keeps no pointer to the Dialogue between calls. Not copyable or movable: the typewriter
// tween writes to a member.
class DialogueBox {
public:
    explicit DialogueBox(const UiStyle& style = {}) : m_Ui(style) {}
    DialogueBox(const DialogueBox&) = delete;
    DialogueBox& operator=(const DialogueBox&) = delete;

    // Describes this frame's box (nothing if the dialogue isn't active) and acts on the input:
    // finishing the line, choosing an answer, continuing. `font` measures the text for wrapping.
    void Update(Dialogue& dialogue, const UiInput& input, const Font& font, f32 dt,
                const DialogueBoxOptions& options = {});
    // `portraitScale` enlarges small pixel-art portraits.
    void Draw(Renderer2D& r, const Font& font, const Sprite* portrait = nullptr,
              f32 portraitScale = 1.0f) const;

    [[nodiscard]] bool IsTyping() const;
    [[nodiscard]] bool IsOpen() const { return m_Open; }
    // The text shown so far (wrapped lines joined by '\n'); for tests and voice / sound cues.
    [[nodiscard]] std::string GetShownText() const;

    [[nodiscard]] Ui& GetUi() { return m_Ui; }
    [[nodiscard]] UiStyle& GetStyle() { return m_Ui.GetStyle(); }

private:
    void StartTyping(const Dialogue& dialogue, const Font& font, const DialogueBoxOptions& options);

    Ui m_Ui;
    Tweens m_Tweens;
    std::vector<std::string> m_Lines; // the card's text, wrapped to the box
    usize m_Length = 0;               // characters (bytes) in m_Lines
    f32 m_Shown = 0.0f;               // how many of them are visible; tweened
    u32 m_Step = 0;                   // the dialogue step m_Lines belongs to
    bool m_Open = false;
    bool m_TextDone = false;   // the text was complete last frame: the answers may show
    bool m_FocusFirst = false; // focus the first answer when the answers appear
};

// Breaks `text` into lines no wider than `maxWidth` at spaces (and at '\n'). A word wider than
// the line gets a line of its own. `measure` gives a string's width.
[[nodiscard]] std::vector<std::string>
WrapText(std::string_view text, f32 maxWidth, const std::function<f32(std::string_view)>& measure);

} // namespace Emerald

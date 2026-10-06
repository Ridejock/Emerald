#include "Emerald/Dialogue/DialogueBox.h"

#include <algorithm>

#include "Emerald/Dialogue/Dialogue.h"
#include "Emerald/Renderer/Font.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {

namespace {

// Moves `n` back to the start of a UTF-8 character, so typing never shows half of one.
usize Utf8Boundary(std::string_view text, usize n)
{
    while (n > 0 && n < text.size() && (static_cast<u8>(text[n]) & 0xC0) == 0x80)
        --n;
    return n;
}

} // namespace

std::vector<std::string> WrapText(std::string_view text, f32 maxWidth,
                                  const std::function<f32(std::string_view)>& measure)
{
    std::vector<std::string> lines;
    while (true) {
        const usize newline = text.find('\n');
        const std::string_view paragraph = text.substr(0, newline);
        std::string line;
        usize pos = 0;
        while (pos < paragraph.size()) {
            const usize start = paragraph.find_first_not_of(' ', pos);
            if (start == std::string_view::npos)
                break;
            const usize end = std::min(paragraph.find(' ', start), paragraph.size());
            const std::string_view word = paragraph.substr(start, end - start);
            const std::string candidate =
                line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && measure(candidate) > maxWidth) {
                lines.push_back(std::move(line)); // the word starts the next line
                line = word;
            } else {
                line = candidate;
            }
            pos = end;
        }
        lines.push_back(std::move(line));
        if (newline == std::string_view::npos)
            break;
        text.remove_prefix(newline + 1);
    }
    return lines;
}

bool DialogueBox::IsTyping() const
{
    return m_Open && m_Shown < static_cast<f32>(m_Length);
}

std::string DialogueBox::GetShownText() const
{
    std::string shown;
    usize left = static_cast<usize>(m_Shown);
    for (usize i = 0; i < m_Lines.size(); ++i) {
        const std::string& line = m_Lines[i];
        const usize n = Utf8Boundary(line, std::min(left, line.size()));
        left -= std::min(left, line.size());
        shown += (i > 0 ? "\n" : "") + line.substr(0, n);
    }
    return shown;
}

void DialogueBox::StartTyping(const Dialogue& dialogue, const Font& font,
                              const DialogueBoxOptions& options)
{
    m_Step = dialogue.GetStep();
    const f32 scale = m_Ui.GetStyle().TextScale;
    const f32 width = options.Width - 2.0f * m_Ui.GetStyle().Padding;
    m_Lines.clear();
    if (!dialogue.GetText().empty()) // a menu card without text gets no empty row
        m_Lines = WrapText(dialogue.GetText(), width,
                           [&](std::string_view s) { return font.MeasureText(s, scale).x; });
    m_Length = 0;
    for (const std::string& line : m_Lines)
        m_Length += line.size();
    m_FocusFirst = true;
    m_TextDone = false;

    // The typewriter: the number of visible characters goes up at a steady rate.
    m_Tweens.CancelTarget(&m_Shown);
    m_Shown = 0.0f;
    if (options.CharsPerSecond <= 0.0f || m_Length == 0)
        m_Shown = static_cast<f32>(m_Length);
    else
        m_Tweens.To(&m_Shown, static_cast<f32>(m_Length),
                    static_cast<f32>(m_Length) / options.CharsPerSecond, {.Curve = Easing::Linear});
}

void DialogueBox::Update(Dialogue& dialogue, const UiInput& input, const Font& font, f32 dt,
                         const DialogueBoxOptions& options)
{
    dialogue.Refresh(); // a hot-reloaded deck
    m_Ui.Begin(input, dt);
    const DialogueCard* card = dialogue.GetCard();
    m_Open = card != nullptr;
    if (!m_Open) {
        m_Tweens.Update(dt);
        m_Ui.End();
        return;
    }
    if (dialogue.GetStep() != m_Step)
        StartTyping(dialogue, font, options);
    m_Tweens.Update(dt); // after StartTyping, so a new card shows its first letters at once

    // While typing, Accept or a click shows the rest. The answers appear a frame after the text
    // is complete, so a press that finishes the text (or lands as it finishes) can't pick one.
    const bool typing = IsTyping();
    if (typing && (input.Accept.Pressed || input.Click.Pressed)) {
        m_Tweens.CancelTarget(&m_Shown);
        m_Shown = static_cast<f32>(m_Length);
    }
    const bool ready = !typing && m_TextDone;
    m_TextDone = !typing;

    // The "##" keeps the panel's id the same for every speaker (UI.h).
    m_Ui.BeginPanel(card->Speaker + "##dialogue",
                    {.Position = options.Position, .Width = options.Width, .Pivot = options.Pivot});
    // Every line gets its row from the start (empty until typed), so the box doesn't grow.
    usize left = static_cast<usize>(m_Shown);
    for (const std::string& line : m_Lines) {
        const usize n = Utf8Boundary(line, std::min(left, line.size()));
        left -= std::min(left, line.size());
        m_Ui.Label(std::string_view(line).substr(0, n));
    }

    if (ready) {
        m_Ui.Space(4.0f);
        if (dialogue.HasChoices()) {
            for (usize i = 0; i < dialogue.GetAnswerCount(); ++i) {
                const DialogueAnswer& answer = dialogue.GetAnswer(i);
                if (answer.Text.empty())
                    continue; // a textless answer among choices isn't offered
                m_Ui.PushId(std::to_string(i));
                if (m_FocusFirst) {
                    m_Ui.SetFocus(m_Ui.GetId(answer.Text));
                    m_FocusFirst = false;
                }
                const bool chosen = m_Ui.Button(answer.Text);
                m_Ui.PopId();
                if (chosen) {
                    dialogue.Choose(i);
                    break; // the answers belong to the old card now
                }
            }
        } else {
            const std::string_view label =
                dialogue.GetAnswerCount() > 0 ? options.ContinueLabel : options.CloseLabel;
            if (m_FocusFirst) {
                m_Ui.SetFocus(m_Ui.GetId(label));
                m_FocusFirst = false;
            }
            if (m_Ui.Button(label))
                dialogue.Advance();
        }
    }
    m_Ui.EndPanel();
    m_Ui.End();
}

void DialogueBox::Draw(Renderer2D& r, const Font& font, const Sprite* portrait,
                       f32 portraitScale) const
{
    if (!m_Open)
        return;
    m_Ui.Draw(r, font);
    // The portrait stands on the box's top edge, at its left.
    const std::vector<UiItem>& items = m_Ui.GetItems();
    if (portrait && portrait->Source && !items.empty() && items.front().Kind == UiKind::Panel) {
        const UiRect& box = items.front().Rect;
        r.DrawSprite(*portrait, box.Position + Vec2(m_Ui.GetStyle().Padding, 0.0f),
                     {.Scale = Vec2(portraitScale), .Origin = {0.0f, 1.0f}});
    }
}

} // namespace Emerald
